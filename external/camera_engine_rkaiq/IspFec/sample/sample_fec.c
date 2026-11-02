/*
 *  Copyright (c) 2024 Rockchip Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "sample_fec.h"

#include <ctype.h>
#include <drm/drm_mode.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <linux/videodev2.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xf86drm.h>

#include "rk_ispfec_api.h"

struct drm_buf {
    int fb_id;
    uint32_t handle;
    uint32_t size;
    uint32_t pitch;
    char* map;
    int dmabuf_fd;
};

#define CLEAR(x) memset(&(x), 0, sizeof(x))

#define FEC_STATIC_FPS_CALCULATION(objname, count)                                     \
    do {                                                                               \
        static uint32_t num_frame = 0;                                                 \
        static struct timeval last_sys_time;                                           \
        static struct timeval first_sys_time;                                          \
        static bool b_last_sys_time_init = false;                                      \
        if (!b_last_sys_time_init) {                                                   \
            gettimeofday(&last_sys_time, NULL);                                        \
            gettimeofday(&first_sys_time, NULL);                                       \
            b_last_sys_time_init = true;                                               \
        } else {                                                                       \
            if ((num_frame % count) == 0) {                                            \
                double total, current;                                                 \
                struct timeval cur_sys_time;                                           \
                gettimeofday(&cur_sys_time, NULL);                                     \
                total = (cur_sys_time.tv_sec - first_sys_time.tv_sec) * 1.0f +         \
                        (cur_sys_time.tv_usec - first_sys_time.tv_usec) / 1000000.0f;  \
                current = (cur_sys_time.tv_sec - last_sys_time.tv_sec) * 1.0f +        \
                          (cur_sys_time.tv_usec - last_sys_time.tv_usec) / 1000000.0f; \
                info("%s Current fps: %.2f, Total avg fps: %.2f\n", #objname,          \
                     ((float)(count)) / current, (float)num_frame / total);            \
                last_sys_time = cur_sys_time;                                          \
            }                                                                          \
        }                                                                              \
        ++num_frame;                                                                   \
    } while (0)

#define IN_IMAGE_FILE "/data/imx415_4k_nv12.yuv"

static const char* file_from;
static const char* file_to;
static unsigned stream_count;
static char options[OptLast];

static int width;
static int height;
static int format;

static rk_ispfec_ctx_t* g_ispfec_ctx = NULL;
static rk_ispfec_cfg_t g_ispfec_cfg;
struct drm_buf g_drm_buf_pic_in;
struct drm_buf g_drm_buf_pic_out;
struct drm_buf g_drm_buf_xint;
struct drm_buf g_drm_buf_xfra;
struct drm_buf g_drm_buf_yint;
struct drm_buf g_drm_buf_yfra;

static struct option long_options[] = {
    // clang-format off
    {"help", no_argument, 0, OptHelp},
    {"silent", no_argument, 0, OptSilent},
    {"width",  required_argument, 0, OptSetWidth},
    {"height", required_argument, 0, OptSetHeight},
    {"format", required_argument, 0, OptSetFormat},
    {"stream-count", required_argument, NULL, OptStreamCount},
    {"stream-to", required_argument, NULL, OptStreamTo},
    {"stream-from", required_argument, NULL, OptStreamFrom},
    {0, 0, 0, 0}
    // clang-format on
};

void common_usage(void) {
    printf(
        "\nGeneral/Common options:\n"
        "  --width <size>     width of image.\n"
        "  --height <size>    height of image.\n"
        "  --format <fmt>     fourcc of format.\n"
        "  --stream-count <count>\n"
        "                     stream <count> buffers. The default is to keep streaming\n"
        "                     forever. This count does not include the number of initial\n"
        "                     skipped buffers as is passed by --stream-skip.\n"
        "  --stream-from <file>\n"
        "                     stream from this file.\n"
        "  --stream-to <file> stream to this file. The default is to discard the\n"
        "                     data.\n"
        "  -s, --silent       subpress debug log.\n"
        "  -h, --help         display this help message.\n");
}

int alloc_drm_buffer(int fd, int width, int height, int bpp, struct drm_buf* buf) {
    struct drm_mode_create_dumb alloc_arg;
    struct drm_mode_map_dumb mmap_arg;
    struct drm_mode_destroy_dumb destory_arg;
    void* map;
    int ret;

    memset(&alloc_arg, 0, sizeof(alloc_arg));
    alloc_arg.bpp    = bpp;
    alloc_arg.width  = width;
    alloc_arg.height = height;

    ret = drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &alloc_arg);
    if (ret) {
        info("failed to create dumb buffer\n");
        return ret;
    }

    memset(&mmap_arg, 0, sizeof(mmap_arg));
    mmap_arg.handle = alloc_arg.handle;
    ret             = drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mmap_arg);
    if (ret) {
        info("failed to create map dumb\n");
        ret = -EINVAL;
        goto destory_dumb;
    }
    map = mmap(0, alloc_arg.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, mmap_arg.offset);
    if (map == MAP_FAILED) {
        info("failed to mmap buffer\n");
        ret = -EINVAL;
        goto destory_dumb;
    }
    ret = drmPrimeHandleToFD(fd, alloc_arg.handle, 0, &buf->dmabuf_fd);
    if (ret) {
        info("failed to get dmabuf fd\n");
        munmap(map, alloc_arg.size);
        ret = -EINVAL;
        goto destory_dumb;
    }
    buf->size = alloc_arg.size;
    buf->map  = (char*)map;

destory_dumb:
    memset(&destory_arg, 0, sizeof(destory_arg));
    destory_arg.handle = alloc_arg.handle;
    drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destory_arg);
    return ret;
}

int free_drm_buffer(int fd, struct drm_buf* buf) {
    if (buf) {
        close(buf->dmabuf_fd);
        return munmap(buf->map, buf->size);
    }
    return -EINVAL;
}

int init_ispfec_bufs(rk_ispfec_cfg_t* cfg) {
    int ret    = 0;
    int drm_fd = drmOpen("rockchip", NULL);
    if (drm_fd < 0) {
        info("failed to open rockchip drm\n");
        return -1;
    }

    int mesh_size = rk_ispfec_api_calFecMeshsize(cfg->in_width, cfg->in_height);

    info("\nmesh_size:%d\n", mesh_size);
    ret = alloc_drm_buffer(drm_fd, mesh_size * 2, 1, 8, &g_drm_buf_xint);
    if (ret) goto close_drm_fd;
    info("xint fd:%d size:%d\n", g_drm_buf_xint.dmabuf_fd, g_drm_buf_xint.size);

    ret = alloc_drm_buffer(drm_fd, mesh_size, 1, 8, &g_drm_buf_xfra);
    if (ret) goto free_drm_buf_xint;
    info("xfra fd:%d size:%d\n", g_drm_buf_xfra.dmabuf_fd, g_drm_buf_xfra.size);

    ret = alloc_drm_buffer(drm_fd, mesh_size * 2, 1, 8, &g_drm_buf_yint);
    if (ret) goto free_drm_buf_xfra;
    info("yint fd:%d size:%d\n", g_drm_buf_yint.dmabuf_fd, g_drm_buf_yint.size);

    ret = alloc_drm_buffer(drm_fd, mesh_size, 1, 8, &g_drm_buf_yfra);
    if (ret) goto free_drm_buf_yint;
    info("yfra fd:%d size:%d\n", g_drm_buf_yfra.dmabuf_fd, g_drm_buf_yfra.size);

    ret = alloc_drm_buffer(drm_fd, cfg->in_width, cfg->in_height * 3 / 2, 8, &g_drm_buf_pic_in);
    if (ret) goto free_drm_buf_yfra;
    info("in pic fd:%d size:%d\n", g_drm_buf_pic_in.dmabuf_fd, g_drm_buf_pic_in.size);

    ret = alloc_drm_buffer(drm_fd, cfg->out_width, cfg->out_height * 3 / 2, 8, &g_drm_buf_pic_out);
    if (ret) goto free_drm_buf_pic_in;
    info("out pic fd:%d size:%d\n", g_drm_buf_pic_out.dmabuf_fd, g_drm_buf_pic_out.size);

    cfg->mesh_xint.dmaFd    = g_drm_buf_xint.dmabuf_fd;
    cfg->mesh_xint.size     = g_drm_buf_xint.size;
    cfg->mesh_xint.vir_addr = g_drm_buf_xint.map;

    cfg->mesh_xfra.dmaFd    = g_drm_buf_xfra.dmabuf_fd;
    cfg->mesh_xfra.size     = g_drm_buf_xfra.size;
    cfg->mesh_xfra.vir_addr = g_drm_buf_xfra.map;

    cfg->mesh_yint.dmaFd    = g_drm_buf_yint.dmabuf_fd;
    cfg->mesh_yint.size     = g_drm_buf_yint.size;
    cfg->mesh_yint.vir_addr = g_drm_buf_yint.map;

    cfg->mesh_yfra.dmaFd    = g_drm_buf_yfra.dmabuf_fd;
    cfg->mesh_yfra.size     = g_drm_buf_yfra.size;
    cfg->mesh_yfra.vir_addr = g_drm_buf_yfra.map;

    goto close_drm_fd;

free_drm_buf_pic_in:
    free_drm_buffer(drm_fd, &g_drm_buf_pic_in);
free_drm_buf_yfra:
    free_drm_buffer(drm_fd, &g_drm_buf_yfra);
free_drm_buf_yint:
    free_drm_buffer(drm_fd, &g_drm_buf_yfra);
free_drm_buf_xfra:
    free_drm_buffer(drm_fd, &g_drm_buf_xfra);
free_drm_buf_xint:
    free_drm_buffer(drm_fd, &g_drm_buf_xint);
close_drm_fd:
    close(drm_fd);

    return ret;
}

void deinit_ispfec_bufs() {
    free_drm_buffer(-1, &g_drm_buf_pic_out);
    free_drm_buffer(-1, &g_drm_buf_yfra);
    free_drm_buffer(-1, &g_drm_buf_yfra);
    free_drm_buffer(-1, &g_drm_buf_xfra);
    free_drm_buffer(-1, &g_drm_buf_xint);
}

size_t read_image_from_file(const char* file_path, char* buffer, size_t buffer_size) {
    FILE* file = fopen(file_path, "r");
    if (file == NULL) {
        stderr_info("Could not open %s for reading\n", file_path);
        return 0;
    }

    size_t bytes_read = fread(buffer, 1, buffer_size - 1, file);

    fclose(file);
    return bytes_read;
}

FILE* open_output_file() {
    FILE* fout = NULL;

    if (file_to) {
        fout = fopen(file_to, "w+");
        if (!fout) stderr_info("Could not open %s for writing\n", file_to);
        return fout;
    }

    return fout;
}

size_t write_buffer_to_file(FILE* fout, const char* buffer, size_t buffer_size) {
    size_t bytes_written = fwrite(buffer, 1, buffer_size, fout);
    if (bytes_written != buffer_size) {
        stderr_info("Error writing to file");
    }
    return bytes_written;
}

int do_handle_fec(FILE* fout) {
    char ch = '<';
    int ret;

    ret = rk_ispfec_api_process(g_ispfec_ctx, g_drm_buf_pic_in.dmabuf_fd,
                                g_drm_buf_pic_out.dmabuf_fd);
    if (ret) {
        stderr_info("rk_ispfec_api_process failed: %s\n", strerror(errno));
        return -1;
    }

    if (fout) {
        ret = write_buffer_to_file(fout, g_drm_buf_pic_out.map, g_drm_buf_pic_out.size);
        if (ret <= 0) {
            stderr_info("write_buffer_to_file failed: %s\n", strerror(errno));
            return -1;
        }

        stderr_info("%c", ch);
        fflush(stderr);
    }

    if (stream_count == 0) return 0;

    if (--stream_count == 0) return -1;

    return 0;
}

int32_t main(int argc, char** argv) {
    int32_t i;

    /* command args */
    int32_t ch;
    char short_options[6 * 2 * 3 + 1];
    int32_t idx = 0;
    FILE* fout  = NULL;

    if (argc == 1) {
        common_usage();
        return 0;
    }
    for (i = 0; long_options[i].name; i++) {
        if (!isalpha(long_options[i].val)) continue;
        short_options[idx++] = long_options[i].val;
        if (long_options[i].has_arg == required_argument) {
            short_options[idx++] = ':';
        } else if (long_options[i].has_arg == optional_argument) {
            short_options[idx++] = ':';
            short_options[idx++] = ':';
        }
    }
    while (1) {
        int option_index = 0;

        short_options[idx] = 0;
        ch                 = getopt_long(argc, argv, short_options, long_options, &option_index);
        if (ch == -1) break;

        options[(int)ch] = 1;
        switch (ch) {
            case OptHelp:
                common_usage();
                return 0;
            case OptSetWidth:
                width = atoi(optarg);
                break;
            case OptSetHeight:
                height = atoi(optarg);
                break;
            case OptSetFormat:
                format = v4l2_fourcc(optarg[0], optarg[1], optarg[2], optarg[3]);
                break;
            case OptStreamCount:
                stream_count = strtoul(optarg, NULL, 0);
                break;
            case OptStreamTo:
                file_to = optarg;
                break;
            case OptStreamFrom:
                file_from = optarg;
                break;
            default:
                break;
        }
    }
    if (optind < argc) {
        printf("unknown arguments: ");
        while (optind < argc) printf("%s ", argv[optind++]);
        printf("\n");
        common_usage();
        return 1;
    }

    g_ispfec_cfg.in_width   = width;
    g_ispfec_cfg.in_height  = height;
    g_ispfec_cfg.out_width  = width;
    g_ispfec_cfg.out_height = height;
    g_ispfec_cfg.in_fourcc  = 0;
    g_ispfec_cfg.out_fourcc = 0;

#if 0
    g_ispfec_cfg.mesh_upd_mode = RK_ISPFEC_UPDATE_MESH_FROM_FILE;
    strcpy(g_ispfec_cfg.u.mesh_file_path, "/etc/iqfiles/FEC_mesh_3840_2160_imx415_3.6mm/");
    strcpy(g_ispfec_cfg.mesh_xint.mesh_file, "meshxi_level0.bin");
    strcpy(g_ispfec_cfg.mesh_xfra.mesh_file, "meshxf_level0.bin");
    strcpy(g_ispfec_cfg.mesh_yint.mesh_file, "meshyi_level0.bin");
    strcpy(g_ispfec_cfg.mesh_yfra.mesh_file, "meshyf_level0.bin");
#else
    g_ispfec_cfg.mesh_upd_mode                 = RK_ISPFEC_UPDATE_MESH_ONLINE;
    g_ispfec_cfg.u.mesh_online.light_center[0] = 1956.3909119999998438;
    g_ispfec_cfg.u.mesh_online.light_center[1] = 1140.6355200000000422;
    g_ispfec_cfg.u.mesh_online.coeff[0]        = -2819.4072493821618081;
    g_ispfec_cfg.u.mesh_online.coeff[1]        = 0.0000316126581792;
    g_ispfec_cfg.u.mesh_online.coeff[2]        = 0.0000000688410142;
    g_ispfec_cfg.u.mesh_online.coeff[3]        = -0.0000000000130686;
    g_ispfec_cfg.u.mesh_online.correct_level   = 250;
    g_ispfec_cfg.u.mesh_online.direction       = RK_ISPFEC_CORRECT_DIRECTION_XY;
    g_ispfec_cfg.u.mesh_online.style           = RK_ISPFEC_KEEP_ASPECT_RATIO_REDUCE_FOV;
#endif

    int ret = init_ispfec_bufs(&g_ispfec_cfg);
    if (ret < 0) {
        stderr_info("init_ispfec_bufs failed: %s\n", strerror(errno));
        return 1;
    }

    g_ispfec_ctx = rk_ispfec_api_init(&g_ispfec_cfg);
    if (!g_ispfec_ctx) {
        stderr_info("rk_ispfec_api_init failed: %s\n", strerror(errno));
        goto error_init;
    }

    if (!file_from) file_from = IN_IMAGE_FILE;

    ret = read_image_from_file(file_from, g_drm_buf_pic_in.map, g_drm_buf_pic_in.size);
    if (ret <= 0) {
        stderr_info("read_image_from_file failed: %s\n", strerror(errno));
        goto clean_up;
    }

    fout = open_output_file();

    while (1) {
        ret = do_handle_fec(fout);
        if (ret < 0) break;

        FEC_STATIC_FPS_CALCULATION(sample_fec, 30);
    }

clean_up:
    if (fout) fclose(fout);
    rk_ispfec_api_deinit(g_ispfec_ctx);
error_init:
    deinit_ispfec_bufs();
    g_ispfec_ctx = NULL;

    return 0;
}
