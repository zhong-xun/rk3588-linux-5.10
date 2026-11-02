/**
 * Copyright (C) 2018 Fuzhou Rockchip Electronics Co., Ltd
 * author: Chad.ma <Chad.ma@rock-chips.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "roots.h"
#include "sdboot.h"
#include "common.h"

extern size_t strlcpy(char *dst, const char *src, size_t dsize);
extern size_t strlcat(char *dst, const char *src, size_t dsize);

static bool cmdline_has_flag(const char *cmdline, const char *flag)
{
    const char *match = cmdline;
    size_t flag_length = strlen(flag);

    while ((match = strstr(match, flag)) != NULL) {
        bool valid_start = match == cmdline ||
            isspace((unsigned char)match[-1]);
        bool valid_end = match[flag_length] == '\0' ||
            isspace((unsigned char)match[flag_length]);

        if (valid_start && valid_end)
            return true;
        match += flag_length;
    }
    return false;
}

bool is_boot_from_SD(void)
{
    bool bSDBoot = false;
    char param[1024] = {0};
    ssize_t length;
    int read_errno = 0;
    int fd;
    LOGI("read cmdline\n");

    fd = open("/proc/cmdline", O_RDONLY);
    if (fd < 0) {
        LOGE("cannot open /proc/cmdline: %s\n", strerror(errno));
        return false;
    }
    do {
        length = read(fd, param, sizeof(param) - 1);
    } while (length < 0 && errno == EINTR);
    if (length < 0)
        read_errno = errno;
    close(fd);
    if (length < 0) {
        LOGE("cannot read /proc/cmdline: %s\n", strerror(read_errno));
        return false;
    }
    param[length] = '\0';

    if (cmdline_has_flag(param, "sdfwupdate")) {
        bSDBoot = true;
        LOGI(">>> Boot from SDcard\n");
    } else {
        bSDBoot = false;
        LOGI(">>> Boot from non-SDcard\n");
    }

    return bSDBoot;
}

void ensure_sd_mounted(bool *bSDMounted)
{
    int i;
    for (i = 0; i < 3; i++) {
        if (0 == ensure_path_mounted(EX_SDCARD_ROOT)) {
            *bSDMounted = true;
            break;
        } else {
            LOGI("delay 1sec\n");
            sleep(1);
        }
    }

    if (i == 3)
        *bSDMounted = false;
}

#define MAX_LINE 1024
static int get_cfg_item(const char *file_name, const char *key,
                        char *value, size_t value_size)
{
    char line[MAX_LINE];
    size_t key_length = strlen(key);
    FILE *stream;
    int result = -1;

    if (value_size == 0)
        return -1;
    value[0] = '\0';

    stream = fopen(file_name, "r");
    if (stream == NULL)
        return -1;

    while (fgets(line, sizeof(line), stream) != NULL) {
        char *cursor = line;
        char *end;
        size_t length;

        /* A truncated line must never be interpreted as fw_update=1. */
        if (strchr(line, '\n') == NULL && !feof(stream)) {
            int character;

            do {
                character = fgetc(stream);
            } while (character != '\n' && character != EOF);
            break;
        }

        while (isspace((unsigned char)*cursor))
            cursor++;
        if (*cursor == '\0' || *cursor == '#')
            continue;
        if (strncmp(cursor, key, key_length) != 0)
            continue;
        cursor += key_length;
        while (isspace((unsigned char)*cursor))
            cursor++;
        if (*cursor++ != '=')
            continue;
        while (isspace((unsigned char)*cursor))
            cursor++;

        end = cursor;
        while (*end != '\0' && !isspace((unsigned char)*end) &&
               *end != '#')
            end++;
        length = (size_t)(end - cursor);
        if (length == 0 || length >= value_size)
            break;

        memcpy(value, cursor, length);
        value[length] = '\0';
        result = 0;
        break;
    }

    fclose(stream);
    return result;
}

bool is_sdcard_update(void)
{
    int  ret = 0;
    bool bSdMounted = false;
    char configFile[64] = {0};
    char str_val[10] = {0};
    const char *str_key = "fw_update";

    LOGI("%s in\n", __func__);
    ensure_sd_mounted(&bSdMounted);
    if (!bSdMounted) {
        LOGE("Error! SDcard not mounted\n");
        return false;
    }

    strlcpy(configFile, EX_SDCARD_ROOT, sizeof(configFile));
    strlcat(configFile, "/sd_boot_config.config", sizeof(configFile));
    LOGI("configFile = %s \n", configFile);
    ret = get_cfg_item(configFile, str_key, str_val, sizeof(str_val));

    if (ret != 0) {
        LOGI("func get_cfg_Item err:%d \n", ret);
        return false;
    }

    LOGI("\n %s:%s \n", str_key, str_val);

    if (strcmp(str_val, "1") != 0) {
        return false;
    }

    LOGI("firmware update will from SDCARD. \n");
    LOGI("%s out\n", __func__);
    return true;
}
