/*
 * V4L2 Codec decoding example application
 * Kamil Debski <k.debski@samsung.com>
 * Mateusz Krawczuk <m.krawczuk@samsung.com>
 *
 * DRM IPP operations
 *
 * Copyright 2015 Samsung Electronics Co., Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <linux/videodev2.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include "common.h"
#include "fimc.h"
#include <drm/exynos_drm.h>

#include <errno.h>
#include <drm/drm_fourcc.h>
#include "drm.h"
#include "gem.h"

#define MAX_BUF 3

#ifndef DRM_FORMAT_NV12MT
#define DRM_FORMAT_NV12MT	fourcc_code('T', 'M', '1', '2') /* 2x2 subsampled Cr:Cb plane 64x32 macroblocks */
#endif

static drmEventContext event_ctx;

/*
 * exynos_drm_ipp_do_buffer is for enqueue or dequeue buffor of both types
 *
 */
int exynos_drm_ipp_do_buffer(int index,int buf_type, int type,struct instance *inst)
{
    struct drm_exynos_ipp_queue_buf* qbuf ;
    int d, ret;
    for(d=0; d < DRM_IPP_MAX_BUF; d++) {
        if (type == EXYNOS_DRM_OPS_SRC) {
            qbuf = inst->ipp.src_buff[d];
            qbuf->handle[EXYNOS_DRM_PLANAR_Y] = inst->drm.gem_src[index*2].handle;
            qbuf->handle[EXYNOS_DRM_PLANAR_CB] = inst->drm.gem_src[(index*2)+1].handle;
        }
        else
            qbuf = inst->ipp.dst_buff[d];

        qbuf->ops_id = type;
        qbuf->buf_type = buf_type;
        qbuf->prop_id = inst->ipp.prop.prop_id;
        qbuf->user_data = 0;
        qbuf->buf_id = 0;
        ret = ioctl(inst->drm.fd, DRM_IOCTL_EXYNOS_IPP_QUEUE_BUF, qbuf);
    }
    if (ret) {
        err("Failed to do queue the buffer.\n");
        return -EINVAL;
    }
    return 0;
}

/*
 * exynos_drm_ipp_cmd_ctrl should be launched after all buffors are queued
 * Command for controling ipp
 * IPP_CTRL_PLAY
 * IPP_CTRL_STOP
 * IPP_CTRL_PAUSE
 * IPP_CTRL_RESUME
 */
int exynos_drm_ipp_cmd_ctrl(enum drm_exynos_ipp_ctrl ctrl, struct instance *inst)
{
    struct drm_exynos_ipp_cmd_ctrl cmd_ctrl = {0,};

    cmd_ctrl.prop_id = 1;
    cmd_ctrl.ctrl = ctrl;

    if (ioctl(inst->drm.fd, DRM_IOCTL_EXYNOS_IPP_CMD_CTRL, &cmd_ctrl) < 0) {
        err("Failed to set the control\n");
        return -EINVAL;
    }
    return 0;
}
/*
 *
 */
int exynos_drm_ipp_display_flip(int index,struct instance *inst)
{
    struct timeval timeout = {.tv_sec = 3, .tv_usec = 0};
    fd_set fds;
    int ret;

    ret = drmModePageFlip(inst->drm.fd, inst->drm.crtc[0], inst->drm.fb[0],
                          DRM_MODE_PAGE_FLIP_EVENT, (void*)(unsigned long)index);
    if(ret) {

        err("Failed to page flip: %d\n",ret);
        return -EINVAL;
    }
    FD_ZERO(&fds);
    FD_SET(0, &fds);
    FD_SET(inst->drm.fd, &fds);

    ret = select(inst->drm.fd +1, &fds, NULL, NULL, &timeout);
    if (ret <= 0) {
        err("Select timed out or error occured\n");
        return -EINVAL;
    } else if (FD_ISSET(0, &fds)) {
        err("Failed on select \n");
        return -EINVAL;
    }
    drmHandleEvent(inst->drm.fd, &event_ctx);
    return 0;
}

int exynos_drm_ipp_dequeue_buffors(struct instance *inst, int index)
{
    dbg("Dequeueing buffer %d\n", index);
    if (exynos_drm_ipp_do_buffer(index, IPP_BUF_DEQUEUE,EXYNOS_DRM_OPS_SRC,inst))
        err("Failed to queue source buffer\n");
    if (exynos_drm_ipp_do_buffer(index, IPP_BUF_DEQUEUE,EXYNOS_DRM_OPS_DST,inst))
        err("Failed to queue destination buffer\n");
    return 0;
}
/*
 *
 */
int exynos_drm_ipp_process_frame(struct instance *inst, int index)
{
    char buffer[1024];
    int len, i;
    struct drm_event *event;
    struct drm_exynos_ipp_event *ipp_event;
    int count = 20;
    int ret;

    dbg("Queueing buffer %d\n", index);
    if (exynos_drm_ipp_do_buffer(index, IPP_BUF_ENQUEUE,EXYNOS_DRM_OPS_SRC,inst))
        err("Failed to queue source buffer\n");
    if (exynos_drm_ipp_do_buffer(index, IPP_BUF_ENQUEUE,EXYNOS_DRM_OPS_DST,inst))
        err("Failed to queue destination buffer\n");

    dbg("IPP process frame\n");
    while (count) {
        struct timeval timeout = { .tv_sec = 3, .tv_usec = 0};
        fd_set fds;

        FD_ZERO(&fds);
        FD_SET(0, &fds);
        FD_SET(inst->drm.fd, &fds);
        ret = select(inst->drm.fd + 1, &fds, NULL, NULL, &timeout);
        if (ret <= 0) {
            err("Failed on select - remainig attempts: %d\n", count);
            --count;
            continue;
        } else if (FD_ISSET(0, &fds))
            break;
        /* This is painfull ... */
        len = read(inst->drm.fd, buffer, sizeof(struct drm_exynos_ipp_event));
        if (len >= sizeof(*event)) {
            int src_id, dest_id;

            i = 0;
            while (i < len) {
                event = (struct drm_event*) &buffer[i];

                switch (event->type) {
                case DRM_EXYNOS_IPP_EVENT:
                    dbg("Received event:\n");
                    ipp_event = (struct drm_exynos_ipp_event*)event;
                    src_id  = ipp_event->buf_id[EXYNOS_DRM_OPS_SRC];
                    dest_id = ipp_event->buf_id[EXYNOS_DRM_OPS_DST];

                    exynos_drm_ipp_display_flip(dest_id, inst);
                    dbg("SRC ID: %d DEST ID: %d INDEX: %d\n", src_id, dest_id, index);
                    break;
                default:
                    break;
                }
                i += event->length;
            }
            /* Done */
            return 0;
        }
        /* Handle event */
        --count;
    }
    // leave:
    return -EINVAL;
}
/*
 * Send command to stop ctrl and then clean up buffors and memory
 */
void exynos_drm_ipp_close(struct instance *inst)
{
    int i;
    struct drm_gem_close gem_close;

    memset(&gem_close, 0, sizeof(gem_close));
    exynos_drm_ipp_cmd_ctrl( IPP_CTRL_STOP, inst);
    for (i = 0; i < inst->mfc.cap_buf_cnt*2; ++i) {
        gem_close.handle = inst->drm.gem_src[i].handle;
        exynos_gem_close(inst->drm.fd, &gem_close);
    }
    for (i = 0; i < MAX_BUFS; ++i) {
        gem_close.handle = inst->drm.gem[i].handle;
        exynos_gem_close(inst->drm.fd, &gem_close);
	drmModeRmFB(inst->drm.fd, inst->drm.fb[i]);
    }
    dbg("Exynos DRM IPP closed\n");

    close(inst->drm.fd);
}
/*
 * Set ipp properties
 */
int exynos_drm_ipp_setup(struct instance *inst)
{
    int ret;
    struct drm_exynos_sz input_size = {inst->mfc.cap_crop_w, inst->mfc.cap_crop_h};

    dbg("Exynos DRM IPP init start\n");

    struct drm_exynos_pos	cpos = {inst->mfc.cap_crop_left, inst->mfc.cap_crop_top, input_size.hsize, input_size.vsize};
    struct drm_exynos_sz output_size = {inst->drm.width, inst->drm.height};

    struct drm_exynos_pos spos = {0, 0, output_size.hsize, output_size.vsize};

    if (inst->mfc.cap_pixfmt == V4L2_PIX_FMT_NV12MT)
        inst->ipp.prop.config[EXYNOS_DRM_OPS_SRC].fmt = DRM_FORMAT_NV12MT;
    else
        inst->ipp.prop.config[EXYNOS_DRM_OPS_SRC].fmt = DRM_FORMAT_NV12;

    inst->ipp.prop.cmd = IPP_CMD_M2M;
    inst->ipp.prop.prop_id = 0;
    /* input */
    inst->ipp.prop.config[EXYNOS_DRM_OPS_SRC].ops_id	= EXYNOS_DRM_OPS_SRC;
    inst->ipp.prop.config[EXYNOS_DRM_OPS_SRC].flip	= EXYNOS_DRM_FLIP_NONE;
    inst->ipp.prop.config[EXYNOS_DRM_OPS_SRC].degree	= EXYNOS_DRM_DEGREE_0;
    //     inst->ipp.prop.config[EXYNOS_DRM_OPS_SRC].fmt	= DRM_FORMAT_NV12MT;
    inst->ipp.prop.config[EXYNOS_DRM_OPS_SRC].pos	= cpos;
    inst->ipp.prop.config[EXYNOS_DRM_OPS_SRC].sz	= input_size;
    /* output */
    inst->ipp.prop.config[EXYNOS_DRM_OPS_DST].ops_id	= EXYNOS_DRM_OPS_DST;
    inst->ipp.prop.config[EXYNOS_DRM_OPS_DST].flip	= EXYNOS_DRM_FLIP_NONE;
    inst->ipp.prop.config[EXYNOS_DRM_OPS_DST].degree	= EXYNOS_DRM_DEGREE_0;
    inst->ipp.prop.config[EXYNOS_DRM_OPS_DST].fmt	= DRM_FORMAT_XRGB8888;
    inst->ipp.prop.config[EXYNOS_DRM_OPS_DST].pos	= spos;
    inst->ipp.prop.config[EXYNOS_DRM_OPS_DST].sz	= output_size;

    ret = ioctl(inst->drm.fd, DRM_IOCTL_EXYNOS_IPP_SET_PROPERTY, &inst->ipp.prop);
    if (ret) {
        err("Failed to set properties [%d]\n", errno);
        close(inst->drm.fd);
        return -EINVAL;
    }

    dbg("Exynos DRM IPP init done\n prop_id %d\n", inst->ipp.prop.prop_id);
    return 0;
}
/*
 * Allocate and setup ipp buffors
 */
int exynos_drm_ipp_allocate_ipp_buffers(struct instance *inst) {
    int ret=0, d=0;

    for(d = 0; d < inst->mfc.cap_buf_cnt; d++) {
        ret = drmPrimeFDToHandle(inst->drm.fd, inst->mfc.dbuf[d][0], &inst->drm.gem_src[d*2].handle);
        ret = drmPrimeFDToHandle(inst->drm.fd, inst->mfc.dbuf[d][1], &inst->drm.gem_src[(d*2)+1].handle);
        close(inst->mfc.dbuf[d][0]);
        close(inst->mfc.dbuf[d][1]);
    }

    for (d=0; d<DRM_IPP_MAX_BUF; d++) {
        inst->ipp.src_buff[d]= (struct drm_exynos_ipp_queue_buf *) calloc (1,sizeof(struct drm_exynos_ipp_queue_buf));
        inst->ipp.src_buff[d]->ops_id = EXYNOS_DRM_OPS_SRC;
        inst->ipp.src_buff[d]->buf_type = IPP_BUF_ENQUEUE;
        inst->ipp.src_buff[d]->user_data = 0;
        inst->ipp.src_buff[d]->prop_id = inst->ipp.prop.prop_id;
        inst->ipp.src_buff[d]->buf_id = d;

        inst->ipp.src_buff[d]->handle[EXYNOS_DRM_PLANAR_Y] = inst->drm.gem_src[d*2].handle;
        inst->ipp.src_buff[d]->handle[EXYNOS_DRM_PLANAR_CB] = inst->drm.gem_src[(d*2)+1].handle;
        inst->ipp.src_buff[d]->handle[EXYNOS_DRM_PLANAR_CR] = 0;
        ret = drmIoctl(inst->drm.fd, DRM_IOCTL_EXYNOS_IPP_QUEUE_BUF, inst->ipp.src_buff[d]);
        if (ret) {
            fprintf(stderr,
                    "failed to SRC DRM_IOCTL_EXYNOS_IPP_QUEUE_BUF[id:%d][buf addr:%p] : %s\n",
                    inst->ipp.src_buff[d]->buf_id, inst->ipp.src_buff[d], strerror(errno));

            return ret;
        }

        inst->ipp.dst_buff[d]= (struct drm_exynos_ipp_queue_buf *) calloc (1,sizeof(struct drm_exynos_ipp_queue_buf));
        inst->ipp.dst_buff[d]->ops_id = EXYNOS_DRM_OPS_DST;
        inst->ipp.dst_buff[d]->buf_type = IPP_BUF_ENQUEUE;
        inst->ipp.dst_buff[d]->user_data = 0;
        inst->ipp.dst_buff[d]->prop_id = inst->ipp.prop.prop_id;
        inst->ipp.dst_buff[d]->buf_id = d;

        inst->ipp.dst_buff[d]->handle[EXYNOS_DRM_PLANAR_Y] = inst->drm.gem[0].handle;
        inst->ipp.dst_buff[d]->handle[EXYNOS_DRM_PLANAR_CB] = 0;
        inst->ipp.dst_buff[d]->handle[EXYNOS_DRM_PLANAR_CR] = 0;
        ret = drmIoctl(inst->drm.fd, DRM_IOCTL_EXYNOS_IPP_QUEUE_BUF, inst->ipp.dst_buff[d]);
        if (ret) {
            fprintf(stderr,
                    "failed to DST DRM_IOCTL_EXYNOS_IPP_QUEUE_BUF[id:%d][buf addr:%p] : %s\n",
                    inst->ipp.src_buff[d]->buf_id, inst->ipp.src_buff[d], strerror(errno));

            return ret;
        }
    }
    dbg("ipp_setup_output_from_mfc\n");
    return 0;
}
