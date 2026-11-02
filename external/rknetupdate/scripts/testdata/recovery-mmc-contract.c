/* Host-only contract test for Rockchip recovery MMC-to-block-device mapping. */

#include <stdarg.h>

#ifndef RKTOOLS_SOURCE
#error "RKTOOLS_SOURCE must name the recovery rktools.c under test"
#endif

#include RKTOOLS_SOURCE

void Log(const char *file, int line, LogPriority level, const char *format, ...)
{
    (void)file;
    (void)line;
    (void)level;
    (void)format;
}

int main(int argc, char **argv)
{
    char expected[PATH_MAX];

    if (argc != 3)
        return 2;

    setenv("RK_RECOVERY_MMC_ROOT", argv[1], 1);
    setenv("RK_RECOVERY_BLOCK_ROOT", argv[2], 1);
    init_sd_emmc_point();

    snprintf(expected, sizeof(expected), "%s/mmcblk2", argv[2]);
    if (strcmp(result_point[SD], expected) != 0)
        return 3;

    snprintf(expected, sizeof(expected), "%s/mmcblk9", argv[2]);
    if (strcmp(result_point[MMC], expected) != 0)
        return 4;

    if (result_point[SDIO][0] != '\0' || result_point[SDcombo][0] != '\0')
        return 5;

    return 0;
}
