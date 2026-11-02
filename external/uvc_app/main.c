#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include "uvc_control.h"
#include "uvc_video.h"
#include "drm.h"

#define ALIGN(x, a)         (((x)+(a)-1)&~((a)-1))

int fill_image(uint8_t *buf, uint32_t width, uint32_t height, uint32_t hor_stride, uint32_t ver_stride) {
    uint8_t *buf_y = buf;
    uint8_t *buf_c = buf + hor_stride * ver_stride;
    uint32_t x, y, i;
    int frame_count = 0;

    uint8_t *p = buf_y;

    for (y = 0; y < height; y++, p += hor_stride) {
        for (x = 0; x < width; x++) {
            p[x] = x + y + frame_count * 3;
        }
    }

    p = buf + hor_stride * ver_stride;
    for (y = 0; y < height / 2; y++, p += hor_stride) {
        for (x = 0; x < width / 2; x++) {
            p[x * 2 + 0] = 128 + y + frame_count * 2;
            p[x * 2 + 1] = 64  + x + frame_count * 5;
        }
    }
}

struct camera_param {
    int width;
    int height;
    int fcc;
    int fps;
};

static struct camera_param g_param;
static pthread_t g_th;
static bool g_run;

int run_uvc(int width, int height, int fcc, int fps)
{
    int fd;
    int ret;
    unsigned int handle;
    char *buffer;
    int handle_fd;
    size_t size;
    int i = 0;
    char file_name[128];
    int extra_cnt = 0;

    snprintf(file_name, sizeof(file_name), "%dx%d.jpg", width, height);
    if (fcc == V4L2_PIX_FMT_MJPEG) {
        if (access(file_name, F_OK)) {
            printf("file %s not exist.\n", file_name);
            exit(0);
        }
    }

    fd = drm_open();
    if (fd < 0)
        return -1;

    size = ALIGN(width, 16) * ALIGN(height, 16) * 3 / 2;
    ret = drm_alloc(fd, size, 16, &handle, 0);
    if (ret)
        return -1;

    ret = drm_handle_to_fd(fd, handle, &handle_fd, 0);
    if (ret)
        return -1;

    buffer = (char*)drm_map_buffer(fd, handle, size);
    if (!buffer) {
        printf("drm map buffer fail.\n");
        return -1;
    }

    if (fcc == V4L2_PIX_FMT_MJPEG) {
        struct stat st;
        size_t file_size = stat(file_name, &st);
        if (file_size < size) {
            FILE *fp = fopen(file_name, "rb");
            size = st.st_size;
            fread(buffer, 1, size, fp);
            fclose(fp);
        }
    } else {
        fill_image(buffer, width, height, width, height);
    }

    while (g_run)
        uvc_read_camera_buffer(buffer, handle_fd, size, NULL, 0);

    drm_unmap_buffer(buffer, size);
    drm_free(fd, handle);
    drm_close(fd);
    return 0;
}

void *uvc_thread(void *arg)
{
    struct camera_param *param = (struct camera_param *)arg;
    run_uvc(param->width, param->height, param->fcc, param->fps);
    pthread_exit(NULL);
}

int open_uvc(int width, int height, int fcc, int fps)
{
    g_run = true;
    g_param.width = width;
    g_param.height = height;
    g_param.fcc = fcc;
    g_param.fps = fps;
    return pthread_create(&g_th, NULL, uvc_thread, &g_param);
}

void close_uvc(void)
{
    g_run = false;
    pthread_join(g_th, NULL);
}

int main(int argc, char* argv[])
{
    register_uvc_open_camera(open_uvc);
    register_uvc_close_camera(close_uvc);

    uint32_t flags = UVC_CONTROL_LOOP_ONCE;
    uvc_control_run(flags);
    while(1)
        sleep(5);

    uvc_control_join(flags);
    return 0;
}
