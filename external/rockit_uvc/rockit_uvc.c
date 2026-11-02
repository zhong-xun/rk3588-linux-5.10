/*
 * Copyright (C) 2020 Rockchip Electronics Co., Ltd.
 * author: Zhihua Wang, hogan.wang@rock-chips.com
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL), available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include "uvc_control.h"
#include "uvc_video.h"

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <pthread.h>
#include <stdbool.h>

#include "rk_debug.h"
#include "rk_defines.h"
#include "rk_mpi_adec.h"
#include "rk_mpi_aenc.h"
#include "rk_mpi_ai.h"
#include "rk_mpi_ao.h"
#include "rk_mpi_avs.h"
#include "rk_mpi_cal.h"
#include "rk_mpi_ivs.h"
#include "rk_mpi_mb.h"
#include "rk_mpi_rgn.h"
#include "rk_mpi_sys.h"
#include "rk_mpi_tde.h"
#include "rk_mpi_vdec.h"
#include "rk_mpi_venc.h"
#include "rk_mpi_vi.h"
#include "rk_mpi_vo.h"
#include "rk_mpi_vpss.h"

struct camera_param {
    int width;
    int height;
    int fcc;
    int fps;
};

static struct camera_param g_param;
static pthread_t g_th;
static bool g_run;

RK_U64 TEST_COMM_GetNowUs() {
	struct timespec time = {0, 0};
	clock_gettime(CLOCK_MONOTONIC, &time);
	return (RK_U64)time.tv_sec * 1000000 + (RK_U64)time.tv_nsec / 1000; /* microseconds */
}

// demo板dev默认都是0，根据不同的channel 来选择不同的vi节点
int vi_dev_init() {
    printf("%s\n", __func__);
    int ret = 0;
    int devId = 0;
    int pipeId = devId;

    VI_DEV_ATTR_S stDevAttr;
    VI_DEV_BIND_PIPE_S stBindPipe;
    memset(&stDevAttr, 0, sizeof(stDevAttr));
    memset(&stBindPipe, 0, sizeof(stBindPipe));
    // 0. get dev config status
    ret = RK_MPI_VI_GetDevAttr(devId, &stDevAttr);
    if (ret == RK_ERR_VI_NOT_CONFIG) {
        // 0-1.config dev
        ret = RK_MPI_VI_SetDevAttr(devId, &stDevAttr);
        if (ret != RK_SUCCESS) {
            printf("RK_MPI_VI_SetDevAttr %x\n", ret);
            return -1;
        }
    } else {
        printf("RK_MPI_VI_SetDevAttr already\n");
    }
    // 1.get dev enable status
    ret = RK_MPI_VI_GetDevIsEnable(devId);
    if (ret != RK_SUCCESS) {
        // 1-2.enable dev
        ret = RK_MPI_VI_EnableDev(devId);
        if (ret != RK_SUCCESS) {
            printf("RK_MPI_VI_EnableDev %x\n", ret);
            return -1;
        }
        // 1-3.bind dev/pipe
        stBindPipe.u32Num = 1;
        stBindPipe.PipeId[0] = pipeId;
        ret = RK_MPI_VI_SetDevBindPipe(devId, &stBindPipe);
        if (ret != RK_SUCCESS) {
            printf("RK_MPI_VI_SetDevBindPipe %x\n", ret);
            return -1;
        }
    } else {
        printf("RK_MPI_VI_EnableDev already\n");
    }

    return 0;
}

int vi_chn_init(int channelId, int width, int height, int depth) {
    int ret;
    int buf_cnt = 4;
    // VI init
    VI_CHN_ATTR_S vi_chn_attr;
    memset(&vi_chn_attr, 0, sizeof(vi_chn_attr));
    vi_chn_attr.stIspOpt.u32BufCount = buf_cnt;
    vi_chn_attr.stIspOpt.enMemoryType =
        VI_V4L2_MEMORY_TYPE_DMABUF; // VI_V4L2_MEMORY_TYPE_MMAP;
    vi_chn_attr.stSize.u32Width = width;
    vi_chn_attr.stSize.u32Height = height;
    vi_chn_attr.enPixelFormat = RK_FMT_YUV420SP;
    vi_chn_attr.enCompressMode = COMPRESS_MODE_NONE; // COMPRESS_AFBC_16x16;
    vi_chn_attr.u32Depth = depth; //0, get fail, 1 - u32BufCount, can get, if bind to other device, must be < u32BufCount
    ret = RK_MPI_VI_SetChnAttr(0, channelId, &vi_chn_attr);
    ret |= RK_MPI_VI_EnableChn(0, channelId);
    if (ret) {
        printf("ERROR: create VI error! ret=%d\n", ret);
        return ret;
    }

    return ret;
}

static RK_S32 test_venc_init(int chnId, int width, int height, RK_CODEC_ID_E enType) {
    VENC_RECV_PIC_PARAM_S stRecvParam;
    VENC_CHN_ATTR_S stAttr;
    memset(&stAttr, 0, sizeof(VENC_CHN_ATTR_S));

    stAttr.stVencAttr.enType = enType;
    stAttr.stVencAttr.enPixelFormat = RK_FMT_YUV420SP;
    stAttr.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
    stAttr.stRcAttr.stH264Cbr.u32BitRate = 10 * 1024;
    stAttr.stRcAttr.stH264Cbr.u32Gop = 60;
    stAttr.stVencAttr.u32PicWidth = width;
    stAttr.stVencAttr.u32PicHeight = height;
    stAttr.stVencAttr.u32VirWidth = width;
    stAttr.stVencAttr.u32VirHeight = height;
    stAttr.stVencAttr.u32StreamBufCnt = 2;
    stAttr.stVencAttr.u32BufSize = width * height * 3 / 2;

    RK_MPI_VENC_CreateChn(chnId, &stAttr);

    memset(&stRecvParam, 0, sizeof(VENC_RECV_PIC_PARAM_S));
    stRecvParam.s32RecvPicNum = -1;
    RK_MPI_VENC_StartRecvFrame(chnId, &stRecvParam);

    return 0;
}

static RK_S32 test_venc_exit(int chnId) {
    RK_S32 s32Ret;
    s32Ret = RK_MPI_VENC_StopRecvFrame(chnId);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("RK_MPI_VENC_StopRecvFrame fail %x", s32Ret);
        return s32Ret;
    }
    s32Ret = RK_MPI_VENC_DestroyChn(chnId);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("RK_MPI_VDEC_DestroyChn fail %x", s32Ret);
        return s32Ret;
    }
    return s32Ret;
}

int rockit_uvc(int width, int height, int fcc, int fps)
{
    int s32Ret;
    int pipeId = 0;
    int channelId = 0;

    if (!g_run)
        return -1;

    printf("%s enter\n", __func__);
    vi_dev_init();
    vi_chn_init(channelId, width, height, fcc == V4L2_PIX_FMT_YUYV ? 2 : 0);

    if (fcc == V4L2_PIX_FMT_YUYV) {
        VIDEO_FRAME_INFO_S stViFrame;
        RK_S32 waitTime = 1000;
        do {
            s32Ret = RK_MPI_VI_GetChnFrame(pipeId, channelId, &stViFrame, waitTime);
            if (s32Ret == RK_SUCCESS) {
                void *data = RK_MPI_MB_Handle2VirAddr(stViFrame.stVFrame.pMbBlk);
                int s32Fd = RK_MPI_MB_Handle2Fd(stViFrame.stVFrame.pMbBlk);
                RK_MPI_SYS_MmzFlushCache(stViFrame.stVFrame.pMbBlk, RK_TRUE);
                uvc_read_camera_buffer(data, s32Fd, width * height * 3 / 2, NULL, 0);

                s32Ret = RK_MPI_VI_ReleaseChnFrame(pipeId, channelId, &stViFrame);
                if (s32Ret != RK_SUCCESS) {
                    RK_LOGE("RK_MPI_VI_ReleaseChnFrame fail %x", s32Ret);
                }
            } else {
                RK_LOGE("RK_MPI_VI_GetChnFrame timeout %x", s32Ret);
            }
        } while (g_run);
    } else if (fcc == V4L2_PIX_FMT_MJPEG || fcc == V4L2_PIX_FMT_H264) {
        RK_CODEC_ID_E enType;
        if (fcc == V4L2_PIX_FMT_MJPEG)
            enType = RK_VIDEO_ID_JPEG;
        else if (fcc == V4L2_PIX_FMT_H264)
            enType = RK_VIDEO_ID_AVC;
        test_venc_init(0, width, height, enType);

        MPP_CHN_S stSrcChn, stDestChn;
        // bind vi to venc
        stSrcChn.enModId = RK_ID_VI;
        stSrcChn.s32DevId = 0;
        stSrcChn.s32ChnId = channelId;

        stDestChn.enModId = RK_ID_VENC;
        stDestChn.s32DevId = 0;
        stDestChn.s32ChnId = 0;
        printf("====RK_MPI_SYS_Bind vi0 to venc0====\n");
        s32Ret = RK_MPI_SYS_Bind(&stSrcChn, &stDestChn);
        if (s32Ret != RK_SUCCESS) {
            RK_LOGE("bind 0 ch venc failed");
            goto __FAILED;
        }

        VENC_STREAM_S stFrame;
        stFrame.pstPack = malloc(sizeof(VENC_PACK_S));
        int loopCount = 0;
        while (g_run) {
            s32Ret = RK_MPI_VENC_GetStream(0, &stFrame, -1);
            if (s32Ret == RK_SUCCESS) {
                void *pData = RK_MPI_MB_Handle2VirAddr(stFrame.pstPack->pMbBlk);
                RK_S32 fd = RK_MPI_MB_Handle2Fd(stFrame.pstPack->pMbBlk);
                RK_U64 nowUs = TEST_COMM_GetNowUs();

                RK_LOGD("chn:0, loopCount:%d enc->seq:%d wd:%d pts=%lld delay=%lldus\n",
                        loopCount, stFrame.u32Seq, stFrame.pstPack->u32Len,
                        stFrame.pstPack->u64PTS, nowUs - stFrame.pstPack->u64PTS);

                uvc_read_camera_buffer(pData, fd, stFrame.pstPack->u32Len, NULL, 0);

                s32Ret = RK_MPI_VENC_ReleaseStream(0, &stFrame);
                if (s32Ret != RK_SUCCESS) {
                    RK_LOGE("RK_MPI_VENC_ReleaseStream fail %x", s32Ret);
                }
                loopCount++;
            } else {
                RK_LOGE("RK_MPI_VENC_GetStream fail %x", s32Ret);
            }
        }
        free(stFrame.pstPack);

        s32Ret = RK_MPI_SYS_UnBind(&stSrcChn, &stDestChn);
        if (s32Ret != RK_SUCCESS) {
            RK_LOGE("RK_MPI_SYS_UnBind fail %x", s32Ret);
        }

        test_venc_exit(0);
    } else {
        printf("fcc %d is not support by rockit.\n", fcc);
    }

__FAILED:
    printf("%s exit\n", __func__);
    s32Ret = RK_MPI_VI_DisableChn(0, channelId);
    RK_LOGE("RK_MPI_VI_DisableChn %x", s32Ret);

    s32Ret = RK_MPI_VI_DisableDev(0);
    RK_LOGE("RK_MPI_VI_DisableDev %x", s32Ret);
    return 0;
}

void *rockit_uvc_thread(void *arg)
{
    struct camera_param *param = (struct camera_param *)arg;
    while (!rockit_uvc(param->width, param->height, param->fcc, param->fps))
        usleep(100000);
    pthread_exit(NULL);
}

int open_rockit_uvc(int width, int height, int fcc, int fps)
{
    g_run = true;
    g_param.width = width;
    g_param.height = height;
    g_param.fcc = fcc;
    g_param.fps = fps;
    return pthread_create(&g_th, NULL, rockit_uvc_thread, &g_param);
}

void close_rockit_uvc(void)
{
    g_run = false;
    pthread_join(g_th, NULL);
}

int main(int argc, char* argv[])
{
    uint32_t flags = 0;

    register_uvc_open_camera(open_rockit_uvc);
    register_uvc_close_camera(close_rockit_uvc);

    if (RK_MPI_SYS_Init() != RK_SUCCESS) {
        RK_LOGE("rk mpi sys init fail!");
        exit(0);
    }

    flags = UVC_CONTROL_LOOP_ONCE;
    uvc_control_run(flags);

    while (1)
        sleep(5);

    RK_MPI_SYS_Exit();
    uvc_control_join(flags);

    return 0;
}
