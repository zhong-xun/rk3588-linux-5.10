/*
 * Copyright (C) 2023 Rockchip Electronics Co., Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <libgen.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <math.h>
#include "common.h"
#include "install.h"
#include "bootloader.h"

#define URL_MAX_LENGTH 512
#define CMDLINE_LENGTH 2048
extern bool bSDBootUpdate;
extern bool bUdiskUpdate;

static bool parse_fraction(const char *text, float *value_out)
{
    char *end;
    float value;

    if (text == NULL || value_out == NULL)
        return false;
    errno = 0;
    value = strtof(text, &end);
    if (text == end || errno == ERANGE || *end != '\0' ||
        !isfinite(value) || value < 0.0f || value > 1.0f)
        return false;
    *value_out = value;
    return true;
}

static bool parse_nonnegative_seconds(const char *text, int *value_out)
{
    char *end;
    long value;

    if (text == NULL || value_out == NULL)
        return false;
    errno = 0;
    value = strtol(text, &end, 10);
    if (text == end || errno == ERANGE || *end != '\0' || value < 0 ||
        value > INT_MAX)
        return false;
    *value_out = (int)value;
    return true;
}

static void terminate_child_after_pipe_error(pid_t pid)
{
    int status;
    int i;

    if (kill(pid, SIGTERM) < 0 && errno != ESRCH)
        LOGE("failed to terminate update process: %s\n", strerror(errno));
    for (i = 0; i < 20; i++) {
        pid_t waited = waitpid(pid, &status, WNOHANG);

        if (waited == pid || (waited < 0 && errno == ECHILD))
            return;
        if (waited < 0 && errno != EINTR)
            break;
        usleep(50000);
    }
    if (kill(pid, SIGKILL) < 0 && errno != ESRCH)
        LOGE("failed to kill update process: %s\n", strerror(errno));
    do {
        if (waitpid(pid, &status, 0) >= 0 || errno == ECHILD)
            break;
    } while (errno == EINTR);
}

static int start_main (const char *binary, char *args[], int* pipefd)
{
    if (binary == NULL || args == NULL || pipefd == NULL) {
        LOGE("invalid update process arguments\n");
        return INSTALL_ERROR;
    }
    pid_t pid = fork();
    if (pid < 0) {
        LOGE("fork error for %s: %s\n", binary, strerror(errno));
        close(pipefd[0]);
        close(pipefd[1]);
        return INSTALL_ERROR;
    }
    if (pid == 0) {
        close(pipefd[0]);
        execv(binary, args);
        LOGI("E:Can't run %s (%s)\n", binary, strerror(errno));
        _exit(-1);
    }
    close(pipefd[1]);

    char buffer[1024];
    FILE* from_child = fdopen(pipefd[0], "r");
    if (from_child == NULL) {
        int saved_errno = errno;

        close(pipefd[0]);
        LOGE("fdopen failed for %s: %s\n", binary, strerror(saved_errno));
        terminate_child_after_pipe_error(pid);
        return INSTALL_ERROR;
    }
    while (fgets(buffer, sizeof(buffer), from_child) != NULL) {
        char* command = strtok(buffer, " \n");
        if (command == NULL) {
            continue;
        } else if (strcmp(command, "progress") == 0) {
            char* fraction_s = strtok(NULL, " \n");
            char* seconds_s = strtok(NULL, " \n");

            if (fraction_s == NULL || seconds_s == NULL) {
                LOGE("malformed progress command\n");
                continue;
            }

            float fraction;
            int seconds;

            if (!parse_fraction(fraction_s, &fraction) ||
                !parse_nonnegative_seconds(seconds_s, &seconds) ||
                strtok(NULL, " \n") != NULL) {
                LOGE("malformed progress command\n");
                continue;
            }

            ui_show_progress(fraction * (1 - VERIFICATION_PROGRESS_FRACTION), seconds);
        } else if (strcmp(command, "set_progress") == 0) {
            char* fraction_s = strtok(NULL, " \n");

            if (fraction_s == NULL) {
                LOGE("malformed set_progress command\n");
                continue;
            }
            float fraction;

            if (!parse_fraction(fraction_s, &fraction) ||
                strtok(NULL, " \n") != NULL) {
                LOGE("malformed set_progress command\n");
                continue;
            }

            ui_set_progress(fraction);
        } else if (strcmp(command, "ui_print") == 0) {
            char* str = strtok(NULL, "\n");
            if (str) {
                LOGI(" >>>>>> %s <<<<<<\n", str);
                ui_print("%s", str);
            } else {
                ui_print("\n");
            }
        } else {
            LOGE("unknown command [%s]\n", command);
        }
    }

    fclose(from_child);

    int status;
    pid_t waited;
    do {
        waited = waitpid(pid, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited < 0) {
        LOGE("waitpid failed for %s: %s\n", binary, strerror(errno));
        return INSTALL_ERROR;
    }
    if (WIFEXITED(status)) {
        if (WEXITSTATUS(status) == 0)
            return INSTALL_SUCCESS;
        LOGE("Error in %s\n(exit status %d)\n", binary,
             WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
        LOGE("Error in %s\n(terminated by signal %d)\n", binary,
             WTERMSIG(status));
    } else {
        LOGE("Error in %s\n(status 0x%x)\n", binary, status);
    }
    return INSTALL_ERROR;
}

static int format_argument(char *buffer, size_t buffer_size,
                            const char *format, const char *value)
{
    if (buffer == NULL || format == NULL || value == NULL || buffer_size == 0)
        return INSTALL_ERROR;
    int written = snprintf(buffer, buffer_size, format, value);

    if (written < 0 || (size_t)written >= buffer_size) {
        LOGE("argument is too long\n");
        return INSTALL_ERROR;
    }
    return INSTALL_SUCCESS;
}

int do_rk_updateEngine(const char *binary, const char *path)
{
    if (binary == NULL || path == NULL || path[0] == '\0') {
        LOGE("invalid updateEngine binary or image path\n");
        return INSTALL_ERROR;
    }
    LOGI("[%s] start with main.\n", __func__);
    char *update = "--update";
    char *update_sdboot = "--update=sdboot";
    char *update_udisk = "--update=usbboot";
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        LOGE("pipe error: %s\n", strerror(errno));
        return INSTALL_ERROR;
    }

    //updateEngine --update --image_url=path --partition=0x3a0000
    char *args[6];
    char args_1[32] = {0};
    char args_2[URL_MAX_LENGTH + 32] = {0};
    char args_4[32] = {0};
    args[0] = (char* )binary;
    args[1] = args_1;
    int written = snprintf(args[1], sizeof(args_1), "--pipefd=%d", pipefd[1]);
    if (written < 0 || (size_t)written >= sizeof(args_1)) {
        close(pipefd[0]);
        close(pipefd[1]);
        return INSTALL_ERROR;
    }
    args[2] = args_2;
    if (format_argument(args[2], sizeof(args_2), "--image_url=%s", path) !=
        INSTALL_SUCCESS) {
        close(pipefd[0]);
        close(pipefd[1]);
        return INSTALL_ERROR;
    }

    if (bSDBootUpdate || bUdiskUpdate) {
        char path_second[PATH_MAX] = {0};
        char path_second_dir[PATH_MAX] = {0};

        if (snprintf(path_second_dir, sizeof(path_second_dir), "%s", path) < 0 ||
            strlen(path) >= sizeof(path_second_dir) ||
            dirname(path_second_dir) == NULL) {
            close(pipefd[0]);
            close(pipefd[1]);
            return INSTALL_ERROR;
        }

        written = snprintf(path_second, sizeof(path_second), "%s/update_ab.img",
                           path_second_dir);
        if (written < 0 || (size_t)written >= sizeof(path_second)) {
            close(pipefd[0]);
            close(pipefd[1]);
            return INSTALL_ERROR;
        }
        if (access(path_second, F_OK) == 0) {
            if (format_argument(args[2], sizeof(args_2), "--image_url=%s",
                                path_second) != INSTALL_SUCCESS) {
                close(pipefd[0]);
                close(pipefd[1]);
                return INSTALL_ERROR;
            }
        } else {
            written = snprintf(path_second, sizeof(path_second),
                               "%s/update_ota.img", path_second_dir);
            if (written < 0 || (size_t)written >= sizeof(path_second)) {
                close(pipefd[0]);
                close(pipefd[1]);
                return INSTALL_ERROR;
            }
            if (access(path_second, F_OK) == 0) {
                if (format_argument(args[2], sizeof(args_2), "--image_url=%s",
                                    path_second) != INSTALL_SUCCESS) {
                    close(pipefd[0]);
                    close(pipefd[1]);
                    return INSTALL_ERROR;
                }
            } else {
                if (format_argument(args[2], sizeof(args_2), "--image_url=%s",
                                    path) != INSTALL_SUCCESS) {
                    close(pipefd[0]);
                    close(pipefd[1]);
                    return INSTALL_ERROR;
                }
            }
        }
    }
    LOGI("SDcard update data: [%s]\n", args[2]);

    if (bSDBootUpdate) {
        args[3] = (char *)update_sdboot;
    } else if (bUdiskUpdate) {
        args[3] = (char *)update_udisk;
    } else {
        args[3] = (char *)update;
    }

    args[4] = args_4;
    args[5] = NULL;

    if (bSDBootUpdate || bUdiskUpdate) {
        // If SD boot, ignore --partition
        written = snprintf(args[4], sizeof(args_4), "--partition=0x%s",
                           "FFFF00");
    } else {
        struct bootloader_message boot;
        int needupdate;

        memset(&boot, 0, sizeof(boot));
        if (get_bootloader_message(&boot) != 0) {
            LOGE("failed to read bootloader message\n");
            close(pipefd[0]);
            close(pipefd[1]);
            return INSTALL_ERROR;
        }
        memcpy(&needupdate, boot.needupdate, sizeof(needupdate));
        written = snprintf(args[4], sizeof(args_4), "--partition=0x%X",
                           (unsigned int)needupdate);
    }
    if (written < 0 || (size_t)written >= sizeof(args_4)) {
        close(pipefd[0]);
        close(pipefd[1]);
        return INSTALL_ERROR;
    }

    return start_main(binary, args, pipefd);

}
int do_rk_update(const char *binary, const char *path)
{
    if (binary == NULL || path == NULL || path[0] == '\0') {
        LOGE("invalid rkupdate binary or image path\n");
        return INSTALL_ERROR;
    }
    LOGI("[%s] start with main.\n", __func__);
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        LOGE("pipe error: %s\n", strerror(errno));
        return INSTALL_ERROR;
    }

    char* args[6];
    args[0] = (char* )binary;
    args[1] = "Version 1.0";
    char args_2[32] = {0};
    args[2] = args_2;
    int written = snprintf(args[2], sizeof(args_2), "%d", pipefd[1]);
    if (written < 0 || (size_t)written >= sizeof(args_2)) {
        close(pipefd[0]);
        close(pipefd[1]);
        return INSTALL_ERROR;
    }
    args[3] = (char*)path;
    char args_4[8] = {0};
    args[4] = args_4;
    written = snprintf(args[4], sizeof(args_4), "%d", (int)bSDBootUpdate);
    if (written < 0 || (size_t)written >= sizeof(args_4)) {
        close(pipefd[0]);
        close(pipefd[1]);
        return INSTALL_ERROR;
    }
    args[5] = NULL;
    return start_main(binary, args, pipefd);
}
