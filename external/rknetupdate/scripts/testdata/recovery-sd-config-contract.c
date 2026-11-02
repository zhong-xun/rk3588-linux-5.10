/* Host-only contract test for the Recovery TF-card configuration parser. */

#include <stdarg.h>

#ifndef SDBOOT_SOURCE
#error "SDBOOT_SOURCE must name the recovery sdboot.c under test"
#endif

#include SDBOOT_SOURCE

void Log(const char *file, int line, LogPriority level, const char *format, ...)
{
    (void)file;
    (void)line;
    (void)level;
    (void)format;
}

int ensure_path_mounted(const char *path)
{
    (void)path;
    return 0;
}

size_t strlcpy(char *destination, const char *source, size_t destination_size)
{
    size_t source_length = strlen(source);

    if (destination_size > 0) {
        size_t copy_length = source_length >= destination_size ?
            destination_size - 1 : source_length;
        memcpy(destination, source, copy_length);
        destination[copy_length] = '\0';
    }
    return source_length;
}

size_t strlcat(char *destination, const char *source, size_t destination_size)
{
    size_t destination_length = strnlen(destination, destination_size);

    if (destination_length == destination_size)
        return destination_size + strlen(source);
    return destination_length +
        strlcpy(destination + destination_length, source,
                destination_size - destination_length);
}

static void write_config(const char *path, const char *content)
{
    FILE *stream = fopen(path, "w");

    if (stream == NULL || fputs(content, stream) == EOF || fclose(stream) != 0) {
        perror(path);
        exit(1);
    }
}

static void write_overlong_config(const char *path)
{
    FILE *stream = fopen(path, "w");
    int index;

    if (stream == NULL || fputs("fw_update=1", stream) == EOF) {
        perror(path);
        exit(1);
    }
    for (index = 0; index < MAX_LINE + 32; index++) {
        if (fputc(' ', stream) == EOF) {
            perror(path);
            exit(1);
        }
    }
    if (fputs("truncated\nfw_update=1\n", stream) == EOF ||
        fclose(stream) != 0) {
        perror(path);
        exit(1);
    }
}

int main(int argc, char **argv)
{
    char value[10];

    if (argc != 2)
        return 2;

    if (!cmdline_has_flag("root=/dev/mmcblk0 sdfwupdate console=ttyS2",
                          "sdfwupdate") ||
        !cmdline_has_flag("sdfwupdate", "sdfwupdate") ||
        cmdline_has_flag("root=/dev/mmcblk0 nosdfwupdate console=ttyS2",
                         "sdfwupdate") ||
        cmdline_has_flag("sdfwupdate=1", "sdfwupdate"))
        return 3;

    write_config(argv[1],
                 "# fw_update=0\n"
                 "not_fw_update=0\n"
                 "fw_update_extra=0\n"
                 "  fw_update = 1 # factory update\n");
    if (get_cfg_item(argv[1], "fw_update", value, sizeof(value)) != 0 ||
        strcmp(value, "1") != 0)
        return 4;

    write_config(argv[1], "fw_update=1234567890\nfw_update=1\n");
    if (get_cfg_item(argv[1], "fw_update", value, sizeof(value)) == 0 ||
        value[0] != '\0')
        return 5;

    write_config(argv[1], "not_fw_update=1\n");
    if (get_cfg_item(argv[1], "fw_update", value, sizeof(value)) == 0)
        return 6;

    write_overlong_config(argv[1]);
    if (get_cfg_item(argv[1], "fw_update", value, sizeof(value)) == 0 ||
        value[0] != '\0')
        return 7;

    unlink(argv[1]);
    return 0;
}
