/* Host-only contract test for the managed recovery completion indicator. */

#define open recovery_notice_test_open
#define close recovery_notice_test_close
#define ioctl recovery_notice_test_ioctl
#define write recovery_notice_test_write
#include "../../packaging/recovery/sdk/recovery-notice/recovery_notice.c"
#undef open
#undef close
#undef ioctl
#undef write

#include <signal.h>
#include <stdarg.h>
#include <sys/wait.h>

extern int open(const char *path, int flags, ...);
extern int close(int fd);
extern ssize_t write(int fd, const void *buffer, size_t length);

static char input_root[PATH_MAX - 64];
static char media_path[PATH_MAX - 64];
static int fd_event[FD_SETSIZE];
static int beeper_writes;
static int selected_event;

static int event_for_path(const char *path)
{
    const char *name = strrchr(path, '/');

    if (name == NULL || strncmp(path, input_root, strlen(input_root)) != 0)
        return -1;
    name++;
    if (strcmp(name, "event1") == 0)
        return 1;
    if (strcmp(name, "event2") == 0)
        return 2;
    if (strcmp(name, "event3") == 0)
        return 3;
    if (strcmp(name, "event4") == 0)
        return 4;
    if (strcmp(name, "event+0") == 0)
        return 0;
    if (strcmp(name, "event20") == 0)
        return 20;
    return -1;
}

int recovery_notice_test_open(const char *path, int flags, ...)
{
    int fd;

    if (flags & O_CREAT) {
        va_list args;
        mode_t mode;

        va_start(args, flags);
        mode = (mode_t)va_arg(args, int);
        va_end(args);
        fd = open(path, flags, mode);
    } else {
        fd = open(path, flags);
    }
    if (fd >= 0 && fd < FD_SETSIZE)
        fd_event[fd] = event_for_path(path);
    return fd;
}

int recovery_notice_test_close(int fd)
{
    if (fd >= 0 && fd < FD_SETSIZE)
        fd_event[fd] = -1;
    return close(fd);
}

int recovery_notice_test_ioctl(int fd, unsigned long request, ...)
{
    va_list args;
    void *value;
    int event = fd >= 0 && fd < FD_SETSIZE ? fd_event[fd] : -1;

    va_start(args, request);
    value = va_arg(args, void *);
    va_end(args);
    if (event < 0) {
        errno = ENOTTY;
        return -1;
    }
    if (request == EVIOCGNAME(NOTICE_SELECTION_SIZE)) {
        const char *name = event == 0 ? "pwm-beeper" :
                           event == 1 ? "pwm-beeper-extra" :
                           event == 3 ? "pwm beeper" :
                           event == 20 ? "pwm-beeper" :
                           event == 2 ? "pwm-beeper" :
                           "pcspkr";
        snprintf(value, NOTICE_SELECTION_SIZE, "%s", name);
        return 0;
    }
    if (request == EVIOCGBIT(0, sizeof(unsigned long) * BITS_TO_LONGS(EV_MAX + 1))) {
        unsigned long *bits = value;
        memset(bits, 0, sizeof(unsigned long) * BITS_TO_LONGS(EV_MAX + 1));
        bits[EV_SND / BITS_PER_LONG] |= 1UL << (EV_SND % BITS_PER_LONG);
        return 0;
    }
    if (request == EVIOCGBIT(EV_SND,
                             sizeof(unsigned long) * BITS_TO_LONGS(SND_MAX + 1))) {
        unsigned long *bits = value;
        memset(bits, 0, sizeof(unsigned long) * BITS_TO_LONGS(SND_MAX + 1));
        if (event == 2)
            bits[SND_BELL / BITS_PER_LONG] |= 1UL << (SND_BELL % BITS_PER_LONG);
        else
            bits[SND_TONE / BITS_PER_LONG] |= 1UL << (SND_TONE % BITS_PER_LONG);
        return 0;
    }
    errno = EINVAL;
    return -1;
}

ssize_t recovery_notice_test_write(int fd, const void *buffer, size_t length)
{
    int event = fd >= 0 && fd < FD_SETSIZE ? fd_event[fd] : -1;

    if (event >= 0) {
        const struct input_event *events = buffer;

        if (length != sizeof(struct input_event) * 2 ||
            events[0].type != EV_SND || events[0].code != SND_TONE ||
            events[1].type != EV_SYN || events[1].code != SYN_REPORT) {
            errno = EINVAL;
            return -1;
        }
        beeper_writes++;
        selected_event = event;
        return (ssize_t)length;
    }
    return write(fd, buffer, length);
}

static void make_file(const char *path, const char *content)
{
    int fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0644);

    if (fd < 0 || write(fd, content, strlen(content)) != (ssize_t)strlen(content)) {
        perror(path);
        exit(1);
    }
    close(fd);
}

static void make_dir(const char *path)
{
    if (mkdir(path, 0755) != 0) {
        perror(path);
        exit(1);
    }
}

static long long monotonic_elapsed_ms(const struct timespec *start,
                                      const struct timespec *end)
{
    return (end->tv_sec - start->tv_sec) * 1000LL +
           (end->tv_nsec - start->tv_nsec) / 1000000LL;
}

int main(void)
{
    char config_contents[NOTICE_IO_SIZE * 2 + 64];
    char config_path[PATH_MAX];
    char path[PATH_MAX];
    char long_selector[NOTICE_SELECTION_SIZE + 1];
    char led_brightness[32];
    char led_root[PATH_MAX - 64];
    char root[PATH_MAX - 128];
    struct notice_config config;
    struct notice_beeper explicit_beeper;
    struct notice_config explicit_config;
    struct timespec finished;
    struct timespec started;
    int index;
    pid_t remover;

    if (snprintf(root, sizeof(root), "%s", getenv("TEST_ROOT")) >= (int)sizeof(root))
        return 2;
    for (index = 0; index < FD_SETSIZE; index++)
        fd_event[index] = -1;
    snprintf(input_root, sizeof(input_root), "%s/input", root);
    snprintf(led_root, sizeof(led_root), "%s/leds", root);
    snprintf(media_path, sizeof(media_path), "%s/mmcblk1", root);
    make_dir(input_root);
    make_dir(led_root);
    snprintf(path, sizeof(path), "%s/status", led_root);
    make_dir(path);
    snprintf(path, sizeof(path), "%s/status/brightness", led_root);
    make_file(path, "0");
    snprintf(path, sizeof(path), "%s/status/max_brightness", led_root);
    make_file(path, "1");
    snprintf(config_path, sizeof(config_path), "%s/recovery-notice.conf", root);
    setenv("RK_RECOVERY_NOTICE_CONFIG", config_path, 1);
    unsetenv("RK_RECOVERY_NOTICE_LED");
    unsetenv("RK_RECOVERY_NOTICE_BEEPER");

    /* A complete line must remain parseable after its newline is trimmed. */
    make_file(config_path, "led=none\nbeeper=none\n");
    notice_load_config(&config);
    if (strcmp(config.led, "none") != 0 ||
        strcmp(config.beeper, "none") != 0) {
        fprintf(stderr, "complete recovery notice configuration was skipped\n");
        return 1;
    }

    strcpy(config_contents, "led=");
    memset(config_contents + 4, 'x', NOTICE_IO_SIZE + 16);
    strcpy(config_contents + 4 + NOTICE_IO_SIZE + 16, "\nbeeper=none\n");
    make_file(config_path, config_contents);
    notice_load_config(&config);
    if (strcmp(config.led, "auto") != 0 ||
        strcmp(config.beeper, "none") != 0) {
        fprintf(stderr, "overlong configuration line was partially parsed\n");
        return 1;
    }

    memset(long_selector, 'x', sizeof(long_selector) - 1);
    long_selector[sizeof(long_selector) - 1] = '\0';
    snprintf(config_contents, sizeof(config_contents), "led=%s\nbeeper=%s\n",
             long_selector, long_selector);
    make_file(config_path, config_contents);
    notice_load_config(&config);
    if (strcmp(config.led, "none") != 0 ||
        strcmp(config.beeper, "none") != 0) {
        fprintf(stderr, "overlong configuration selectors were not disabled\n");
        return 1;
    }

    make_file(config_path, "led=auto\nbeeper=auto\n");
    setenv("RK_RECOVERY_NOTICE_LED", long_selector, 1);
    setenv("RK_RECOVERY_NOTICE_BEEPER", long_selector, 1);
    notice_load_config(&config);
    if (strcmp(config.led, "none") != 0 ||
        strcmp(config.beeper, "none") != 0) {
        fprintf(stderr, "overlong environment selectors were not disabled\n");
        return 1;
    }

    for (index = 0; index < 6; index++) {
        int number = index == 0 ? 1 : index == 1 ? 2 : index == 2 ? 3 :
                     index == 3 ? 4 : index == 4 ? 20 : -1;
        if (number >= 0)
            snprintf(path, sizeof(path), "%s/event%d", input_root, number);
        else
            snprintf(path, sizeof(path), "%s/event+0", input_root);
        make_file(path, "");
    }
    {
        unsigned int event_number;

        if (!notice_event_number("event0", &event_number) || event_number != 0 ||
            !notice_event_number("event10", &event_number) || event_number != 10 ||
            notice_event_number("event+0", &event_number) ||
            notice_event_number("event 0", &event_number) ||
            notice_event_number("event-1", &event_number) ||
            notice_event_number("event1x", &event_number)) {
            fprintf(stderr, "eventN parser accepted a non-ASCII-numeric name\n");
            return 1;
        }
    }
    make_file(media_path, "media");
    setenv("RK_RECOVERY_NOTICE_INPUT_ROOT", input_root, 1);
    setenv("RK_RECOVERY_NOTICE_LED_ROOT", led_root, 1);
    setenv("RK_RECOVERY_NOTICE_BEEPER", "auto", 1);
    setenv("RK_RECOVERY_NOTICE_LED", "auto", 1);
    setenv("RK_RECOVERY_NOTICE_CONFIG", "/no/such/config", 1);
    beeper_writes = 0;
    selected_event = -1;
    remover = fork();
    if (remover == 0) {
        usleep(70000);
        unlink(media_path);
        usleep(150000);
        make_file(media_path, "media");
        usleep(300000);
        unlink(media_path);
        _exit(0);
    }
    clock_gettime(CLOCK_MONOTONIC, &started);
    if (remover < 0 || recovery_notice_wait_for_removal("TF card", media_path) != 0 ||
        selected_event != 3 || beeper_writes < 2) {
        fprintf(stderr, "pwm-beeper selection or removal contract failed\n");
        return 1;
    }
    clock_gettime(CLOCK_MONOTONIC, &finished);
    if (monotonic_elapsed_ms(&started, &finished) <
        NOTICE_REMOVAL_CONFIRM_MS + 300) {
        fprintf(stderr, "transient media-node loss was treated as removal\n");
        return 1;
    }
    waitpid(remover, NULL, 0);
    snprintf(path, sizeof(path), "%s/status/brightness", led_root);
    if (notice_read_text(path, led_brightness, sizeof(led_brightness)) != 0 ||
        strcmp(led_brightness, "0") != 0) {
        fprintf(stderr, "LED state was not restored after media removal\n");
        return 1;
    }

    snprintf(path, sizeof(path), "%s/event4", input_root);
    setenv("RK_RECOVERY_NOTICE_BEEPER", path, 1);
    notice_load_config(&explicit_config);
    notice_open_beeper(&explicit_config, &explicit_beeper);
    if (explicit_beeper.fd >= 0) {
        notice_close_beeper(&explicit_beeper);
        fprintf(stderr, "explicit non-pwm input device must be rejected\n");
        return 1;
    }

    setenv("RK_RECOVERY_NOTICE_BEEPER", "pwm", 1);
    notice_load_config(&explicit_config);
    notice_open_beeper(&explicit_config, &explicit_beeper);
    if (explicit_beeper.fd >= 0) {
        notice_close_beeper(&explicit_beeper);
        fprintf(stderr, "partial pwm-beeper name must be rejected\n");
        return 1;
    }

    snprintf(input_root, sizeof(input_root), "%s/missing-input", root);
    snprintf(led_root, sizeof(led_root), "%s/missing-leds", root);
    setenv("RK_RECOVERY_NOTICE_INPUT_ROOT", input_root, 1);
    setenv("RK_RECOVERY_NOTICE_LED_ROOT", led_root, 1);
    setenv("RK_RECOVERY_NOTICE_BEEPER", "auto", 1);
    selected_event = -1;
    make_file(media_path, "media");
    remover = fork();
    if (remover == 0) {
        usleep(70000);
        unlink(media_path);
        _exit(0);
    }
    if (remover < 0) {
        perror("fork");
        return 1;
    }
    if (recovery_notice_wait_for_removal("TF card", media_path) != 0 ||
        selected_event != -1) {
        fprintf(stderr, "missing hardware must be nonfatal\n");
        return 1;
    }
    waitpid(remover, NULL, 0);

    remover = fork();
    if (remover == 0)
        _exit(recovery_notice_wait_for_removal("TF card", NULL));
    if (remover < 0) {
        perror("fork");
        return 1;
    }
    usleep(70000);
    if (waitpid(remover, NULL, WNOHANG) != 0) {
        fprintf(stderr, "unknown media path must disable automatic return\n");
        return 1;
    }
    kill(remover, SIGTERM);
    waitpid(remover, NULL, 0);

    puts("recovery notice contract checks passed");
    return 0;
}
