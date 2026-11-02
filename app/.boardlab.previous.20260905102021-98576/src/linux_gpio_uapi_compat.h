/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef BOARDLAB_LINUX_GPIO_UAPI_COMPAT_H
#define BOARDLAB_LINUX_GPIO_UAPI_COMPAT_H

#include <stddef.h>

#include <linux/gpio.h>
#include <linux/ioctl.h>
#include <linux/types.h>

/*
 * Some Rockchip SDK sysroots ship an old linux/gpio.h. GCC 6 toolchains found
 * in 4.19 SDKs may even predate the GPIO line-handle ABI v1, while newer
 * kernels implement ABI v2. Keep the SDK header authoritative and backfill
 * only the stable UAPI that BoardLab uses when the matching request ioctl is
 * absent.
 *
 * The declarations and ioctl encodings below match Linux v5.10 UAPI. They are
 * a userspace ABI contract, not a private mirror of kernel implementation
 * details. A v1-only kernel rejects the v2 ioctls at runtime, after which
 * BoardLab retries with GPIO character-device ABI v1.
 */
#ifndef GPIO_MAX_NAME_SIZE
#define GPIO_MAX_NAME_SIZE 32
#endif

#ifndef GPIO_GET_LINEHANDLE_IOCTL
#define BOARDLAB_GPIO_V1_UAPI_BACKFILL 1

#ifndef GPIOHANDLES_MAX
#define GPIOHANDLES_MAX 64
#endif

#define GPIOHANDLE_REQUEST_INPUT       (1UL << 0)
#define GPIOHANDLE_REQUEST_OUTPUT      (1UL << 1)
#define GPIOHANDLE_REQUEST_ACTIVE_LOW  (1UL << 2)
#define GPIOHANDLE_REQUEST_OPEN_DRAIN  (1UL << 3)
#define GPIOHANDLE_REQUEST_OPEN_SOURCE (1UL << 4)

struct gpiohandle_request {
    __u32 lineoffsets[GPIOHANDLES_MAX];
    __u32 flags;
    __u8 default_values[GPIOHANDLES_MAX];
    char consumer_label[GPIO_MAX_NAME_SIZE];
    __u32 lines;
    int fd;
};

struct gpiohandle_data {
    __u8 values[GPIOHANDLES_MAX];
};

#define GPIO_GET_LINEHANDLE_IOCTL \
    _IOWR(0xB4, 0x03, struct gpiohandle_request)
#define GPIOHANDLE_GET_LINE_VALUES_IOCTL \
    _IOWR(0xB4, 0x08, struct gpiohandle_data)
#define GPIOHANDLE_SET_LINE_VALUES_IOCTL \
    _IOWR(0xB4, 0x09, struct gpiohandle_data)

#else
#define BOARDLAB_GPIO_V1_UAPI_BACKFILL 0
#endif

#ifndef GPIO_V2_GET_LINE_IOCTL
#define BOARDLAB_GPIO_V2_UAPI_BACKFILL 1

#define GPIO_V2_LINES_MAX 64
#define GPIO_V2_LINE_NUM_ATTRS_MAX 10

enum gpio_v2_line_flag {
    GPIO_V2_LINE_FLAG_USED = (1ULL << 0),
    GPIO_V2_LINE_FLAG_ACTIVE_LOW = (1ULL << 1),
    GPIO_V2_LINE_FLAG_INPUT = (1ULL << 2),
    GPIO_V2_LINE_FLAG_OUTPUT = (1ULL << 3),
    GPIO_V2_LINE_FLAG_EDGE_RISING = (1ULL << 4),
    GPIO_V2_LINE_FLAG_EDGE_FALLING = (1ULL << 5),
    GPIO_V2_LINE_FLAG_OPEN_DRAIN = (1ULL << 6),
    GPIO_V2_LINE_FLAG_OPEN_SOURCE = (1ULL << 7),
    GPIO_V2_LINE_FLAG_BIAS_PULL_UP = (1ULL << 8),
    GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN = (1ULL << 9),
    GPIO_V2_LINE_FLAG_BIAS_DISABLED = (1ULL << 10),
};

struct gpio_v2_line_values {
    __aligned_u64 bits;
    __aligned_u64 mask;
};

enum gpio_v2_line_attr_id {
    GPIO_V2_LINE_ATTR_ID_FLAGS = 1,
    GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES = 2,
    GPIO_V2_LINE_ATTR_ID_DEBOUNCE = 3,
};

struct gpio_v2_line_attribute {
    __u32 id;
    __u32 padding;
    union {
        __aligned_u64 flags;
        __aligned_u64 values;
        __u32 debounce_period_us;
    };
};

struct gpio_v2_line_config_attribute {
    struct gpio_v2_line_attribute attr;
    __aligned_u64 mask;
};

struct gpio_v2_line_config {
    __aligned_u64 flags;
    __u32 num_attrs;
    __u32 padding[5];
    struct gpio_v2_line_config_attribute attrs[GPIO_V2_LINE_NUM_ATTRS_MAX];
};

struct gpio_v2_line_request {
    __u32 offsets[GPIO_V2_LINES_MAX];
    char consumer[GPIO_MAX_NAME_SIZE];
    struct gpio_v2_line_config config;
    __u32 num_lines;
    __u32 event_buffer_size;
    __u32 padding[5];
    __s32 fd;
};

struct gpio_v2_line_info {
    char name[GPIO_MAX_NAME_SIZE];
    char consumer[GPIO_MAX_NAME_SIZE];
    __u32 offset;
    __u32 num_attrs;
    __aligned_u64 flags;
    struct gpio_v2_line_attribute attrs[GPIO_V2_LINE_NUM_ATTRS_MAX];
    __u32 padding[4];
};

#define GPIO_V2_GET_LINEINFO_IOCTL \
    _IOWR(0xB4, 0x05, struct gpio_v2_line_info)
#define GPIO_V2_GET_LINEINFO_WATCH_IOCTL \
    _IOWR(0xB4, 0x06, struct gpio_v2_line_info)
#define GPIO_V2_GET_LINE_IOCTL \
    _IOWR(0xB4, 0x07, struct gpio_v2_line_request)
#define GPIO_V2_LINE_SET_CONFIG_IOCTL \
    _IOWR(0xB4, 0x0D, struct gpio_v2_line_config)
#define GPIO_V2_LINE_GET_VALUES_IOCTL \
    _IOWR(0xB4, 0x0E, struct gpio_v2_line_values)
#define GPIO_V2_LINE_SET_VALUES_IOCTL \
    _IOWR(0xB4, 0x0F, struct gpio_v2_line_values)

#else
#define BOARDLAB_GPIO_V2_UAPI_BACKFILL 0
#endif

#ifdef __cplusplus
/* Guard the copied UAPI against layout drift on every supported toolchain. */
static_assert(sizeof(gpiohandle_request) == 364,
              "gpiohandle_request UAPI size changed");
static_assert(offsetof(gpiohandle_request, flags) == 256,
              "gpiohandle_request UAPI layout changed");
static_assert(offsetof(gpiohandle_request, default_values) == 260,
              "gpiohandle_request UAPI layout changed");
static_assert(offsetof(gpiohandle_request, consumer_label) == 324,
              "gpiohandle_request UAPI layout changed");
static_assert(offsetof(gpiohandle_request, fd) == 360,
              "gpiohandle_request UAPI layout changed");
static_assert(sizeof(gpiohandle_data) == 64,
              "gpiohandle_data UAPI size changed");
static_assert(_IOC_TYPE(GPIO_GET_LINEHANDLE_IOCTL) == 0xB4,
              "GPIO v1 UAPI ioctl type changed");
static_assert(_IOC_NR(GPIO_GET_LINEHANDLE_IOCTL) == 0x03,
              "GPIO v1 request ioctl number changed");
static_assert(_IOC_SIZE(GPIO_GET_LINEHANDLE_IOCTL) == sizeof(gpiohandle_request),
              "GPIO v1 request ioctl size changed");
static_assert(_IOC_SIZE(GPIOHANDLE_GET_LINE_VALUES_IOCTL) == sizeof(gpiohandle_data),
              "GPIO v1 values ioctl size changed");
static_assert(sizeof(gpio_v2_line_values) == 16,
              "gpio_v2_line_values UAPI size changed");
static_assert(offsetof(gpio_v2_line_values, mask) == 8,
              "gpio_v2_line_values UAPI layout changed");
static_assert(sizeof(gpio_v2_line_attribute) == 16,
              "gpio_v2_line_attribute UAPI size changed");
static_assert(offsetof(gpio_v2_line_attribute, flags) == 8,
              "gpio_v2_line_attribute UAPI layout changed");
static_assert(sizeof(gpio_v2_line_config_attribute) == 24,
              "gpio_v2_line_config_attribute UAPI size changed");
static_assert(offsetof(gpio_v2_line_config_attribute, mask) == 16,
              "gpio_v2_line_config_attribute UAPI layout changed");
static_assert(sizeof(gpio_v2_line_config) == 272,
              "gpio_v2_line_config UAPI size changed");
static_assert(offsetof(gpio_v2_line_config, attrs) == 32,
              "gpio_v2_line_config UAPI layout changed");
static_assert(sizeof(gpio_v2_line_request) == 592,
              "gpio_v2_line_request UAPI size changed");
static_assert(offsetof(gpio_v2_line_request, config) == 288,
              "gpio_v2_line_request UAPI layout changed");
static_assert(offsetof(gpio_v2_line_request, fd) == 588,
              "gpio_v2_line_request UAPI layout changed");
static_assert(sizeof(gpio_v2_line_info) == 256,
              "gpio_v2_line_info UAPI size changed");
static_assert(offsetof(gpio_v2_line_info, flags) == 72,
              "gpio_v2_line_info UAPI layout changed");
static_assert(offsetof(gpio_v2_line_info, attrs) == 80,
              "gpio_v2_line_info UAPI layout changed");
static_assert(_IOC_TYPE(GPIO_V2_GET_LINE_IOCTL) == 0xB4,
              "GPIO v2 UAPI ioctl type changed");
static_assert(_IOC_NR(GPIO_V2_GET_LINE_IOCTL) == 0x07,
              "GPIO v2 request ioctl number changed");
static_assert(_IOC_SIZE(GPIO_V2_GET_LINE_IOCTL) == sizeof(gpio_v2_line_request),
              "GPIO v2 request ioctl size changed");
static_assert(_IOC_SIZE(GPIO_V2_GET_LINEINFO_IOCTL) == sizeof(gpio_v2_line_info),
              "GPIO v2 line-info ioctl size changed");
static_assert(_IOC_SIZE(GPIO_V2_LINE_GET_VALUES_IOCTL) == sizeof(gpio_v2_line_values),
              "GPIO v2 values ioctl size changed");
#endif

#endif
