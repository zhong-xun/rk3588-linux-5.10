/*
 * RKNETUPDATE-MANAGED-RECOVERY-NOTICE-V1
 * Copyright (C) 2026 Beijing Topeet Technology Co., Ltd.
 *
 * Completion indicators must use kernel device classes.  Board GPIO numbers
 * and PWM controller indexes are deliberately not guessed here: polarity,
 * pinmux, and PWM ownership remain the responsibility of the board DTS.
 */

#include "recovery_notice.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/input.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define NOTICE_CONFIG_PATH "/etc/recovery-notice.conf"
#define NOTICE_LED_ROOT "/sys/class/leds"
#define NOTICE_INPUT_ROOT "/dev/input"
#define NOTICE_SELECTION_SIZE 128
#define NOTICE_IO_SIZE 512
#define NOTICE_REMOVAL_CONFIRM_MS 1000U

#define BITS_PER_LONG (sizeof(unsigned long) * 8U)
#define BITS_TO_LONGS(count) (((count) + BITS_PER_LONG - 1U) / BITS_PER_LONG)

struct notice_config {
    char led[NOTICE_SELECTION_SIZE];
    char beeper[NOTICE_SELECTION_SIZE];
    unsigned int blink_ms;
    unsigned int beep_count;
    unsigned int beep_hz;
    unsigned int beep_on_ms;
    unsigned int beep_off_ms;
};

struct notice_led {
    bool active;
    bool restore;
    bool state;
    int max_brightness;
    char name[NAME_MAX + 1];
    char brightness_path[PATH_MAX];
    char trigger_path[PATH_MAX];
    char previous_brightness[32];
    char previous_trigger[64];
};

struct notice_beeper {
    int fd;
    unsigned short code;
    char name[NOTICE_SELECTION_SIZE];
    char path[PATH_MAX];
};

enum notice_media_state {
    NOTICE_MEDIA_PRESENT,
    NOTICE_MEDIA_REMOVED,
    NOTICE_MEDIA_UNKNOWN,
};

static const char *notice_env_or_default(const char *name,
                                         const char *default_value)
{
    const char *value = getenv(name);

    return value != NULL && value[0] != '\0' ? value : default_value;
}

static void notice_copy(char *dst, size_t dst_size, const char *src)
{
    if (dst_size == 0)
        return;

    if (src == NULL)
        src = "";
    snprintf(dst, dst_size, "%s", src);
}

/* An invalid explicit selector must not become a different truncated device. */
static void notice_set_selector(char *dst, size_t dst_size, const char *value,
                                const char *kind)
{
    if (value == NULL || strlen(value) >= dst_size) {
        printf("recovery-notice: %s selector is too long; disabling %s\n",
               kind, kind);
        notice_copy(dst, dst_size, "none");
        return;
    }

    notice_copy(dst, dst_size, value);
}

static char *notice_trim(char *text)
{
    char *end;

    while (isspace((unsigned char)*text))
        text++;

    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1]))
        *--end = '\0';

    return text;
}

static bool notice_parse_uint(const char *text, unsigned int minimum,
                              unsigned int maximum, unsigned int *value)
{
    char *end = NULL;
    unsigned long parsed;

    errno = 0;
    parsed = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *notice_trim(end) != '\0' ||
        parsed < minimum || parsed > maximum)
        return false;

    *value = (unsigned int)parsed;
    return true;
}

static void notice_set_config_value(struct notice_config *config,
                                    const char *key, const char *value)
{
    if (strcmp(key, "led") == 0) {
        notice_set_selector(config->led, sizeof(config->led), value, "LED");
    } else if (strcmp(key, "beeper") == 0) {
        notice_set_selector(config->beeper, sizeof(config->beeper), value,
                            "beeper");
    } else if (strcmp(key, "blink_ms") == 0) {
        notice_parse_uint(value, 50, 5000, &config->blink_ms);
    } else if (strcmp(key, "beep_count") == 0) {
        notice_parse_uint(value, 0, 100, &config->beep_count);
    } else if (strcmp(key, "beep_hz") == 0) {
        notice_parse_uint(value, 20, 20000, &config->beep_hz);
    } else if (strcmp(key, "beep_on_ms") == 0) {
        notice_parse_uint(value, 20, 5000, &config->beep_on_ms);
    } else if (strcmp(key, "beep_off_ms") == 0) {
        notice_parse_uint(value, 20, 5000, &config->beep_off_ms);
    }
}

static void notice_load_config(struct notice_config *config)
{
    const char *config_path;
    const char *override;
    char line[NOTICE_IO_SIZE];
    FILE *stream;

    memset(config, 0, sizeof(*config));
    notice_copy(config->led, sizeof(config->led), "auto");
    notice_copy(config->beeper, sizeof(config->beeper), "auto");
    config->blink_ms = 250;
    config->beep_count = 10;
    config->beep_hz = 1000;
    config->beep_on_ms = 150;
    config->beep_off_ms = 150;

    config_path = notice_env_or_default("RK_RECOVERY_NOTICE_CONFIG",
                                        NOTICE_CONFIG_PATH);
    stream = fopen(config_path, "r");
    if (stream != NULL) {
        while (fgets(line, sizeof(line), stream) != NULL) {
            char *key;
            char *separator;
            char *value;

            /* Never interpret the first fragment of an overlong line. */
            if (strchr(line, '\n') == NULL && !feof(stream)) {
                int character;

                do {
                    character = fgetc(stream);
                } while (character != '\n' && character != EOF);
                continue;
            }

            key = notice_trim(line);
            if (key[0] == '\0' || key[0] == '#')
                continue;

            separator = strchr(key, '=');
            if (separator == NULL)
                continue;
            *separator = '\0';
            value = notice_trim(separator + 1);
            key = notice_trim(key);
            notice_set_config_value(config, key, value);
        }
        fclose(stream);
    }

    override = getenv("RK_RECOVERY_NOTICE_LED");
    if (override != NULL && override[0] != '\0')
        notice_set_selector(config->led, sizeof(config->led), override, "LED");
    override = getenv("RK_RECOVERY_NOTICE_BEEPER");
    if (override != NULL && override[0] != '\0')
        notice_set_selector(config->beeper, sizeof(config->beeper), override,
                            "beeper");
}

static int notice_write_text(const char *path, const char *value)
{
    size_t length = strlen(value);
    ssize_t written;
    int fd;

    fd = open(path, O_WRONLY);
    if (fd < 0)
        return -1;

    do {
        written = write(fd, value, length);
    } while (written < 0 && errno == EINTR);
    close(fd);

    return written == (ssize_t)length ? 0 : -1;
}

static int notice_read_text(const char *path, char *value, size_t value_size)
{
    ssize_t length;
    int fd;

    if (value_size == 0)
        return -1;

    fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;

    do {
        length = read(fd, value, value_size - 1);
    } while (length < 0 && errno == EINTR);
    close(fd);

    if (length < 0)
        return -1;
    value[length] = '\0';
    notice_trim(value);
    return 0;
}

static void notice_lowercase(char *dst, size_t dst_size, const char *src)
{
    size_t index;

    if (dst_size == 0)
        return;

    for (index = 0; index + 1 < dst_size && src[index] != '\0'; index++)
        dst[index] = (char)tolower((unsigned char)src[index]);
    dst[index] = '\0';
}

static int notice_led_score(const char *name)
{
    char lower[NAME_MAX + 1];

    notice_lowercase(lower, sizeof(lower), name);
    if (strcmp(lower, "recovery-status") == 0 ||
        strcmp(lower, "recovery_status") == 0)
        return 1000;
    if (strstr(lower, "recovery") != NULL)
        return 900;
    if (strcmp(lower, "status") == 0 || strcmp(lower, "status_led") == 0 ||
        strcmp(lower, "work") == 0 || strcmp(lower, "work_led") == 0)
        return 800;
    if (strncmp(lower, "status", 6) == 0 ||
        strncmp(lower, "work", 4) == 0)
        return 700;
    if (strcmp(lower, "user") == 0 || strcmp(lower, "user_led") == 0 ||
        strcmp(lower, "sys") == 0 || strcmp(lower, "sys_led") == 0)
        return 600;
    if (strstr(lower, "user") != NULL || strstr(lower, "system") != NULL)
        return 500;

    return 0;
}

static bool notice_led_exists(const char *root, const char *name)
{
    char path[PATH_MAX];

    if (strchr(name, '/') != NULL)
        return false;
    if (snprintf(path, sizeof(path), "%s/%s/brightness", root, name) >=
        (int)sizeof(path))
        return false;

    return access(path, F_OK) == 0;
}

static int notice_find_led(const char *selector, char *name, size_t name_size)
{
    const char *root = notice_env_or_default("RK_RECOVERY_NOTICE_LED_ROOT",
                                             NOTICE_LED_ROOT);
    struct dirent *entry;
    DIR *directory;
    int best_score = 0;
    char best_name[NAME_MAX + 1] = {0};

    if (strcmp(selector, "none") == 0)
        return -1;
    if (strcmp(selector, "auto") != 0) {
        if (!notice_led_exists(root, selector)) {
            printf("recovery-notice: configured LED '%s' is unavailable\n",
                   selector);
            return -1;
        }
        notice_copy(name, name_size, selector);
        return 0;
    }

    directory = opendir(root);
    if (directory == NULL)
        return -1;

    while ((entry = readdir(directory)) != NULL) {
        int score;

        if (entry->d_name[0] == '.' ||
            !notice_led_exists(root, entry->d_name))
            continue;
        score = notice_led_score(entry->d_name);
        if (score > best_score ||
            (score == best_score && score > 0 &&
             strcmp(entry->d_name, best_name) < 0)) {
            best_score = score;
            notice_copy(best_name, sizeof(best_name), entry->d_name);
        }
    }
    closedir(directory);

    if (best_score == 0)
        return -1;
    notice_copy(name, name_size, best_name);
    return 0;
}

static bool notice_save_trigger(struct notice_led *led)
{
    char triggers[NOTICE_IO_SIZE];
    char *begin;
    char *end;

    if (notice_read_text(led->trigger_path, triggers, sizeof(triggers)) != 0)
        return false;

    begin = strchr(triggers, '[');
    end = begin == NULL ? NULL : strchr(begin + 1, ']');
    if (begin == NULL || end == NULL)
        return false;

    *end = '\0';
    notice_copy(led->previous_trigger, sizeof(led->previous_trigger), begin + 1);
    return led->previous_trigger[0] != '\0';
}

static void notice_restore_led(struct notice_led *led)
{
    if (!led->restore)
        return;

    if (led->previous_brightness[0] != '\0')
        notice_write_text(led->brightness_path, led->previous_brightness);
    if (led->previous_trigger[0] != '\0' &&
        strcmp(led->previous_trigger, "none") != 0)
        notice_write_text(led->trigger_path, led->previous_trigger);
    led->restore = false;
}

static void notice_open_led(const struct notice_config *config,
                            struct notice_led *led)
{
    const char *root = notice_env_or_default("RK_RECOVERY_NOTICE_LED_ROOT",
                                             NOTICE_LED_ROOT);
    char max_brightness[32];

    memset(led, 0, sizeof(*led));
    if (notice_find_led(config->led, led->name, sizeof(led->name)) != 0) {
        printf("recovery-notice: no status LED selected\n");
        return;
    }

    if (snprintf(led->brightness_path, sizeof(led->brightness_path),
                 "%s/%s/brightness", root, led->name) >=
            (int)sizeof(led->brightness_path) ||
        snprintf(led->trigger_path, sizeof(led->trigger_path),
                 "%s/%s/trigger", root, led->name) >=
            (int)sizeof(led->trigger_path))
        return;

    if (notice_read_text(led->brightness_path, led->previous_brightness,
                         sizeof(led->previous_brightness)) != 0) {
        printf("recovery-notice: cannot read LED '%s' state\n", led->name);
        return;
    }

    led->max_brightness = 1;
    {
        char max_path[PATH_MAX];

        if (snprintf(max_path, sizeof(max_path), "%s/%s/max_brightness",
                     root, led->name) < (int)sizeof(max_path) &&
            notice_read_text(max_path, max_brightness,
                             sizeof(max_brightness)) == 0) {
            long parsed = strtol(max_brightness, NULL, 10);
            if (parsed > 0 && parsed <= INT_MAX)
                led->max_brightness = (int)parsed;
        }
    }

    if (access(led->trigger_path, F_OK) == 0) {
        if (!notice_save_trigger(led)) {
            printf("recovery-notice: cannot disable LED '%s' trigger\n",
                   led->name);
            return;
        }
        led->restore = true;
        if (notice_write_text(led->trigger_path, "none") != 0) {
            printf("recovery-notice: cannot disable LED '%s' trigger\n",
                   led->name);
            notice_restore_led(led);
            return;
        }
    }
    if (notice_write_text(led->brightness_path, "0") != 0) {
        printf("recovery-notice: cannot control LED '%s'\n", led->name);
        notice_restore_led(led);
        return;
    }

    led->restore = true;
    led->active = true;
    printf("recovery-notice: blinking LED '%s'\n", led->name);
}

static void notice_set_led(struct notice_led *led, bool enabled)
{
    char value[32];

    if (!led->active)
        return;
    snprintf(value, sizeof(value), "%d", enabled ? led->max_brightness : 0);
    if (notice_write_text(led->brightness_path, value) != 0) {
        printf("recovery-notice: LED '%s' stopped responding\n", led->name);
        led->active = false;
        return;
    }
    led->state = enabled;
}

static void notice_close_led(struct notice_led *led)
{
    if (led->active)
        notice_set_led(led, false);
    notice_restore_led(led);
    led->active = false;
}

static bool notice_test_bit(const unsigned long *bits, unsigned int bit)
{
    return (bits[bit / BITS_PER_LONG] &
            (1UL << (bit % BITS_PER_LONG))) != 0;
}

static bool notice_input_has_beeper(int fd, unsigned short *code)
{
    unsigned long event_bits[BITS_TO_LONGS(EV_MAX + 1)];
    unsigned long sound_bits[BITS_TO_LONGS(SND_MAX + 1)];

    memset(event_bits, 0, sizeof(event_bits));
    memset(sound_bits, 0, sizeof(sound_bits));
    if (ioctl(fd, EVIOCGBIT(0, sizeof(event_bits)), event_bits) < 0 ||
        !notice_test_bit(event_bits, EV_SND) ||
        ioctl(fd, EVIOCGBIT(EV_SND, sizeof(sound_bits)), sound_bits) < 0)
        return false;

    if (!notice_test_bit(sound_bits, SND_TONE))
        return false;

    *code = SND_TONE;
    return true;
}

static int notice_try_beeper(const char *path, const char *selector,
                             bool automatic, struct notice_beeper *beeper)
{
    char name[NOTICE_SELECTION_SIZE] = {0};
    unsigned short code;
    int fd;

    fd = open(path, O_RDWR | O_NONBLOCK);
    if (fd < 0)
        return -1;
    if (!notice_input_has_beeper(fd, &code)) {
        close(fd);
        return -1;
    }

    if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) < 0)
        notice_copy(name, sizeof(name), strrchr(path, '/') != NULL ?
                    strrchr(path, '/') + 1 : path);
    if (!automatic && strcmp(selector, path) != 0 &&
        strcmp(selector, strrchr(path, '/') != NULL ?
               strrchr(path, '/') + 1 : path) != 0 &&
        strcasecmp(name, selector) != 0) {
        close(fd);
        return -1;
    }

    beeper->fd = fd;
    beeper->code = code;
    notice_copy(beeper->name, sizeof(beeper->name), name);
    notice_copy(beeper->path, sizeof(beeper->path), path);
    return 0;
}

static bool notice_is_pwm_beeper(const struct notice_beeper *beeper)
{
    return strcasecmp(beeper->name, "pwm-beeper") == 0 ||
           strcasecmp(beeper->name, "pwm beeper") == 0;
}

static bool notice_event_number(const char *name, unsigned int *number)
{
    const char *digit;
    unsigned int parsed = 0;

    if (strncmp(name, "event", 5) != 0 || name[5] == '\0')
        return false;

    for (digit = name + 5; *digit != '\0'; digit++) {
        unsigned int value;

        if (*digit < '0' || *digit > '9')
            return false;
        value = (unsigned int)(*digit - '0');
        if (parsed > (UINT_MAX - value) / 10U)
            return false;
        parsed = parsed * 10U + value;
    }

    *number = parsed;
    return true;
}

static void notice_close_beeper(struct notice_beeper *beeper);

static void notice_move_beeper(struct notice_beeper *destination,
                               struct notice_beeper *source)
{
    notice_close_beeper(destination);
    *destination = *source;
    source->fd = -1;
}

static void notice_open_beeper(const struct notice_config *config,
                               struct notice_beeper *beeper)
{
    const char *root = notice_env_or_default("RK_RECOVERY_NOTICE_INPUT_ROOT",
                                             NOTICE_INPUT_ROOT);
    bool automatic = strcmp(config->beeper, "auto") == 0;
    struct dirent *entry;
    DIR *directory;

    unsigned int selected_event = UINT_MAX;

    memset(beeper, 0, sizeof(*beeper));
    beeper->fd = -1;
    if (strcmp(config->beeper, "none") == 0 || config->beep_count == 0)
        return;

    if (!automatic && config->beeper[0] == '/') {
        notice_try_beeper(config->beeper, config->beeper, false, beeper);
        if (beeper->fd >= 0 && !notice_is_pwm_beeper(beeper))
            notice_close_beeper(beeper);
    } else {
        directory = opendir(root);
        if (directory != NULL) {
            while ((entry = readdir(directory)) != NULL) {
                char path[PATH_MAX];
                unsigned int event_number;

                if (!notice_event_number(entry->d_name, &event_number) ||
                    snprintf(path, sizeof(path), "%s/%s", root,
                             entry->d_name) >= (int)sizeof(path))
                    continue;
                {
                    struct notice_beeper candidate;

                    memset(&candidate, 0, sizeof(candidate));
                    candidate.fd = -1;
                    if (notice_try_beeper(path, config->beeper, automatic,
                                          &candidate) != 0)
                        continue;

                    if (notice_is_pwm_beeper(&candidate) &&
                        event_number < selected_event) {
                        notice_move_beeper(beeper, &candidate);
                        selected_event = event_number;
                    }
                    notice_close_beeper(&candidate);
                }
            }
            closedir(directory);
        }
    }

    if (beeper->fd >= 0) {
        printf("recovery-notice: beeping input device '%s' (%s)\n",
               beeper->name, beeper->path);
    } else {
        printf("recovery-notice: no input beeper selected\n");
    }
}

static int notice_set_beeper(struct notice_beeper *beeper, bool enabled,
                             unsigned int frequency)
{
    struct input_event events[2];
    ssize_t written;

    if (beeper->fd < 0)
        return -1;

    memset(events, 0, sizeof(events));
    events[0].type = EV_SND;
    events[0].code = beeper->code;
    events[0].value = enabled ?
        (beeper->code == SND_TONE ? (int)frequency : 1) : 0;
    events[1].type = EV_SYN;
    events[1].code = SYN_REPORT;

    do {
        written = write(beeper->fd, events, sizeof(events));
    } while (written < 0 && errno == EINTR);

    return written == (ssize_t)sizeof(events) ? 0 : -1;
}

static void notice_close_beeper(struct notice_beeper *beeper)
{
    if (beeper->fd < 0)
        return;
    notice_set_beeper(beeper, false, 0);
    close(beeper->fd);
    beeper->fd = -1;
}

struct notice_busy_state {
    bool active;
    bool led_state;
    unsigned int completed_beeps;
    struct notice_config config;
    struct notice_led led;
    struct notice_beeper beeper;
};

static struct notice_busy_state busy_notice;

static void notice_sleep_ms(unsigned int milliseconds)
{
    struct timespec requested;
    struct timespec remaining;

    requested.tv_sec = milliseconds / 1000;
    requested.tv_nsec = (long)(milliseconds % 1000) * 1000000L;
    while (nanosleep(&requested, &remaining) < 0 && errno == EINTR)
        requested = remaining;
}

void recovery_notice_busy_start(const char *media_name)
{
    if (busy_notice.active)
        recovery_notice_busy_stop();

    memset(&busy_notice, 0, sizeof(busy_notice));
    notice_load_config(&busy_notice.config);
    notice_open_led(&busy_notice.config, &busy_notice.led);
    notice_open_beeper(&busy_notice.config, &busy_notice.beeper);
    busy_notice.active = true;
    printf("recovery-notice: update medium '%s' is busy; indicators active\n",
           media_name != NULL && media_name[0] != '\0' ? media_name :
           "update medium");
}

void recovery_notice_busy_tick(void)
{
    if (!busy_notice.active)
        return;

    if (busy_notice.led.active) {
        busy_notice.led_state = !busy_notice.led_state;
        notice_set_led(&busy_notice.led, busy_notice.led_state);
    }
    if (busy_notice.beeper.fd >= 0 &&
        busy_notice.completed_beeps < busy_notice.config.beep_count) {
        if (notice_set_beeper(&busy_notice.beeper, true,
                              busy_notice.config.beep_hz) == 0) {
            notice_sleep_ms(busy_notice.config.beep_on_ms > 250 ?
                            250 : busy_notice.config.beep_on_ms);
            notice_set_beeper(&busy_notice.beeper, false, 0);
            busy_notice.completed_beeps++;
        } else {
            printf("recovery-notice: busy-state beeper stopped responding\n");
            notice_close_beeper(&busy_notice.beeper);
        }
    }
}

void recovery_notice_busy_stop(void)
{
    if (!busy_notice.active)
        return;
    notice_close_beeper(&busy_notice.beeper);
    notice_close_led(&busy_notice.led);
    memset(&busy_notice, 0, sizeof(busy_notice));
}

static enum notice_media_state notice_probe_media(const char *device_path,
                                                   int *probe_errno)
{
    struct stat device_stat;

    if (stat(device_path, &device_stat) == 0) {
        *probe_errno = 0;
        return NOTICE_MEDIA_PRESENT;
    }

    *probe_errno = errno;
    if (errno == ENOENT || errno == ENODEV)
        return NOTICE_MEDIA_REMOVED;
    return NOTICE_MEDIA_UNKNOWN;
}

int recovery_notice_wait_for_removal(const char *media_name,
                                     const char *device_path)
{
    struct notice_config config;
    struct notice_beeper beeper;
    struct notice_led led;
    unsigned long long elapsed_ms = 0;
    unsigned long long next_led_ms = 0;
    unsigned long long next_beep_ms = 0;
    unsigned long long next_probe_log_ms = 0;
    unsigned long long removed_since_ms = ULLONG_MAX;
    unsigned int completed_beeps = 0;
    const unsigned int tick_ms = 25;
    bool beeper_on = false;
    bool monitor_device;
    int last_probe_errno = 0;

    if (media_name == NULL || media_name[0] == '\0')
        media_name = "update medium";
    monitor_device = device_path != NULL && device_path[0] != '\0';

    notice_load_config(&config);
    notice_open_led(&config, &led);
    notice_open_beeper(&config, &beeper);

    printf("recovery-notice: update complete; remove %s to reboot\n",
           media_name);
    if (!monitor_device) {
        printf("recovery-notice: %s device path is unknown; automatic "
               "reboot is disabled\n", media_name);
    }

    for (;;) {
        if (monitor_device) {
            int probe_errno;
            enum notice_media_state media_state =
                notice_probe_media(device_path, &probe_errno);

            if (media_state == NOTICE_MEDIA_REMOVED) {
                if (removed_since_ms == ULLONG_MAX) {
                    removed_since_ms = elapsed_ms;
                    printf("recovery-notice: %s removal detected; "
                           "confirming device absence\n", media_name);
                }
                if (elapsed_ms - removed_since_ms >=
                    NOTICE_REMOVAL_CONFIRM_MS)
                    break;
            } else {
                removed_since_ms = ULLONG_MAX;
            }
            if (media_state == NOTICE_MEDIA_UNKNOWN &&
                (probe_errno != last_probe_errno ||
                 elapsed_ms >= next_probe_log_ms)) {
                printf("recovery-notice: cannot verify %s at '%s' (%s); "
                       "continuing to wait\n", media_name, device_path,
                       strerror(probe_errno));
                next_probe_log_ms = elapsed_ms + 5000;
            }
            last_probe_errno = probe_errno;
        }

        if (led.active && elapsed_ms >= next_led_ms) {
            notice_set_led(&led, !led.state);
            next_led_ms = elapsed_ms + config.blink_ms;
        }

        if (beeper.fd >= 0 && completed_beeps < config.beep_count &&
            elapsed_ms >= next_beep_ms) {
            if (!beeper_on) {
                if (notice_set_beeper(&beeper, true, config.beep_hz) != 0) {
                    printf("recovery-notice: input beeper stopped responding\n");
                    notice_close_beeper(&beeper);
                    completed_beeps = config.beep_count;
                } else {
                    beeper_on = true;
                    next_beep_ms = elapsed_ms + config.beep_on_ms;
                }
            } else {
                notice_set_beeper(&beeper, false, 0);
                beeper_on = false;
                completed_beeps++;
                next_beep_ms = elapsed_ms + config.beep_off_ms;
            }
        }

        notice_sleep_ms(tick_ms);
        elapsed_ms += tick_ms;
    }

    notice_close_beeper(&beeper);
    notice_close_led(&led);
    printf("recovery-notice: %s removed; reboot may proceed\n", media_name);
    return 0;
}
