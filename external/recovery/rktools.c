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

#include <ctype.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/types.h>
#include <dirent.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include "rktools.h"
#include "common.h"

static int read_text_file(const char *path, char *buffer, size_t buffer_size)
{
    ssize_t length;
    int read_errno = 0;
    int fd;

    if (buffer_size == 0)
        return -1;

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        LOGE("cannot open %s: %s\n", path, strerror(errno));
        return -1;
    }
    do {
        length = read(fd, buffer, buffer_size - 1);
    } while (length < 0 && errno == EINTR);
    if (length < 0)
        read_errno = errno;
    close(fd);
    if (length < 0) {
        LOGE("cannot read %s: %s\n", path, strerror(read_errno));
        return -1;
    }

    buffer[length] = '\0';
    return 0;
}

/**
 * 从/proc/cmdline 获取串口的节点
 *
*/
char *getSerial()
{
    const size_t answer_size = 64;
    char param[1024] = {0};
    const char *console;
    const char *end;
    char *answer;
    size_t length;

    answer = malloc(answer_size);
    if (answer == NULL)
        return NULL;
    if (read_text_file("/proc/cmdline", param, sizeof(param)) != 0) {
        free(answer);
        return NULL;
    }
    LOGI("cmdline=%s\n", param);
    console = strstr(param, "console=");
    if (console == NULL) {
        LOGI("no found console in cmdline\n");
        free(answer);
        return NULL;
    }

    console += strlen("console=");
    end = console;
    while (*end != '\0' && !isspace((unsigned char)*end) && *end != ',')
        end++;
    length = (size_t)(end - console);
    if (length == 0 || length + strlen("/dev/") + 1 > answer_size) {
        LOGE("invalid console device in cmdline\n");
        free(answer);
        return NULL;
    }

    snprintf(answer, answer_size, "/dev/%.*s", (int)length, console);
    LOGI("read console from cmdline is %s\n", answer);
    return answer;
}

/**
 *  设置flash 节点
 */
static char result_point[4][PATH_MAX] = {'\0'}; //0-->emmc, 1-->sdcard, 2-->SDIO, 3-->SDcombo

static const char *rktools_env_or_default(const char *name,
                                          const char *default_value)
{
    const char *value = getenv(name);

    return value != NULL && value[0] != '\0' ? value : default_value;
}

static bool is_mmc_block_name(const char *name)
{
    char *end = NULL;

    if (strncmp(name, "mmcblk", 6) != 0 || name[6] == '\0')
        return false;

    errno = 0;
    strtoul(name + 6, &end, 10);
    return errno == 0 && end != name + 6 && *end == '\0';
}

static int find_mmc_block_device(const char *card_name, char *device_path,
                                 size_t device_path_size)
{
    const char *mmc_root =
        rktools_env_or_default("RK_RECOVERY_MMC_ROOT",
                               "/sys/bus/mmc/devices");
    const char *device_root =
        rktools_env_or_default("RK_RECOVERY_BLOCK_ROOT", "/dev");
    char block_root[PATH_MAX];
    char block_name[NAME_MAX + 1] = {0};
    struct dirent *entry;
    DIR *directory;

    if (snprintf(block_root, sizeof(block_root), "%s/%s/block",
                 mmc_root, card_name) >= (int)sizeof(block_root))
        return -1;

    directory = opendir(block_root);
    if (directory == NULL)
        return -1;

    while ((entry = readdir(directory)) != NULL) {
        if (!is_mmc_block_name(entry->d_name))
            continue;
        if (block_name[0] == '\0' ||
            strcmp(entry->d_name, block_name) < 0)
            snprintf(block_name, sizeof(block_name), "%s", entry->d_name);
    }
    closedir(directory);

    if (block_name[0] == '\0' ||
        snprintf(device_path, device_path_size, "%s/%s",
                 device_root, block_name) >= (int)device_path_size)
        return -1;

    return 0;
}

int readFile(DIR* dir, char* filename)
{
    char name[30] = {'\0'};
    char resultBuf[10] = {'\0'};
    ssize_t length;
    int read_errno = 0;
    int fd;
    int i;

    if (snprintf(name, sizeof(name), "%s/type", filename) >=
        (int)sizeof(name)) {
        LOGE("Error: MMC type path is too long: %s.\n", filename);
        return -1;
    }

    fd = openat(dirfd(dir), name, O_RDONLY);
    if (fd == -1) {
        LOGE("Error: openat %s error %s.\n", name, strerror(errno));
        return -1;
    }

    do {
        length = read(fd, resultBuf, sizeof(resultBuf) - 1);
    } while (length < 0 && errno == EINTR);
    if (length < 0)
        read_errno = errno;
    close(fd);
    if (length < 1) {
        LOGE("Error: read %s error %s.\n", name,
             length < 0 ? strerror(read_errno) : "empty type");
        return -1;
    }
    resultBuf[length] = '\0';
    for (i = 0; resultBuf[i] != '\0'; i++) {
        if (resultBuf[i] == '\n') {
            resultBuf[i] = '\0';
            break;
        }
    }
    for (i = 0; i < 4; i++) {
        if (strcmp(typeName[i], resultBuf) == 0) {
            //printf("type is %s.\n", typeName[i]);
            return i;
        }
    }

    LOGE("Error:no found type!\n");
    return -1;
}

void init_sd_emmc_point()
{
    const char *mmc_root =
        rktools_env_or_default("RK_RECOVERY_MMC_ROOT",
                               "/sys/bus/mmc/devices");
    DIR* dir = opendir(mmc_root);
    struct dirent* de;

    memset(result_point, 0, sizeof(result_point));
    if (dir == NULL) {
        LOGE("Error: cannot open MMC device directory: %s.\n",
             strerror(errno));
        return;
    }

    while ((de = readdir(dir))) {
        char device_path[PATH_MAX];
        int type_index;

        if (strncmp(de->d_name, "mmc", 3) != 0)
            continue;

        type_index = readFile(dir, de->d_name);
        if (type_index < 0 ||
            type_index >= (int)(sizeof(result_point) / sizeof(result_point[0])))
            continue;
        if (find_mmc_block_device(de->d_name, device_path,
                                  sizeof(device_path)) != 0) {
            LOGW("cannot resolve block device for MMC card %s.\n",
                 de->d_name);
            continue;
        }

        if (result_point[type_index][0] == '\0' ||
            strcmp(device_path, result_point[type_index]) < 0)
            snprintf(result_point[type_index],
                     sizeof(result_point[type_index]), "%s", device_path);
    }
    closedir(dir);
}

static void wait_for_device(const char* fn)
{
    int tries = 0;
    int ret;
    struct stat buf;
    do {
        ++tries;
        ret = stat(fn, &buf);
        if (ret) {
            LOGI("stat %s try %d: %s\n", fn, tries, strerror(errno));
            sleep(1);
        }
    } while (ret && tries < 10);
    if (ret) {
        LOGI("failed to stat %s\n", fn);
    }
}

void setFlashPoint()
{
    const char *emmc_point;
    const char *sd_point;
    const char *sd_point_2;
    char name_t[PATH_MAX] = {0};
    struct stat device_stat;

    if (!isMtdDevice())
        wait_for_device(MISC_PARTITION_NAME_BLOCK);

    unsetenv(EMMC_POINT_NAME);
    unsetenv(SD_POINT_NAME);
    unsetenv(SD_POINT_NAME_2);
    init_sd_emmc_point();
    if (result_point[MMC][0] != '\0' &&
        stat(result_point[MMC], &device_stat) == 0 &&
        S_ISBLK(device_stat.st_mode))
        setenv(EMMC_POINT_NAME, result_point[MMC], 1);
    //SDcard 有两个挂载点

    if (result_point[SD][0] != '\0' &&
        stat(result_point[SD], &device_stat) == 0 &&
        S_ISBLK(device_stat.st_mode))
        setenv(SD_POINT_NAME_2, result_point[SD], 1);
    if (result_point[SD][0] != '\0' &&
        snprintf(name_t, sizeof(name_t), "%sp1", result_point[SD]) <
            (int)sizeof(name_t) &&
        stat(name_t, &device_stat) == 0 && S_ISBLK(device_stat.st_mode))
        setenv(SD_POINT_NAME, name_t, 1);

    emmc_point = getenv(EMMC_POINT_NAME);
    sd_point = getenv(SD_POINT_NAME);
    sd_point_2 = getenv(SD_POINT_NAME_2);
    LOGI("emmc_point is %s\n", emmc_point != NULL ? emmc_point : "(unset)");
    LOGI("sd_point is %s\n", sd_point != NULL ? sd_point : "(unset)");
    LOGI("sd_point_2 is %s\n", sd_point_2 != NULL ? sd_point_2 : "(unset)");
}

#define MTD_PATH "/proc/mtd"
//判断是MTD还是block 设备
bool isMtdDevice()
{
    char param[2048] = {0};
    char *s = NULL;

    if (read_text_file("/proc/cmdline", param, sizeof(param)) != 0)
        return false;
    s = strstr(param, "storagemedia");
    if (s == NULL) {
        LOGI("no found storagemedia in cmdline, default is not MTD.\n");
        return false;
    } else {
        s = strstr(s, "=");
        if (s == NULL) {
            LOGI("no found storagemedia in cmdline, default is not MTD.\n");
            return false;
        }

        s++;
        while (*s == ' ') {
            s++;
        }

        if (strncmp(s, "mtd", 3) == 0 ) {
            LOGI("Now is MTD.\n");
            return true;
        } else if (strncmp(s, "sd", 2) == 0) {
            LOGI("Now is SD.\n");
            if ( !access(MTD_PATH, F_OK) ) {
                if (read_text_file(MTD_PATH, param, sizeof(param)) != 0)
                    return false;

                s = strstr(param, "mtd");
                if (s == NULL) {
                    LOGI("no found mtd.\n");
                    return false;
                }
                LOGI("Now is MTD.\n");
                return true;
            }
        }
    }
    LOGI("devices is not MTD.\n");
    return false;
}
