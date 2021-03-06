/*
 * V4L2 Codec decoding example application
 * Kamil Debski <k.debski@samsung.com>
 *
 * FIMC operations
 *
 * Copyright 2012 Samsung Electronics Co., Ltd.
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

#include <linux/videodev2.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>

#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>

#include "common.h"
#include "fimc.h"

#include <errno.h>
#include <drm/drm_fourcc.h>


static char *dbg_type[2] = {"OUTPUT", "CAPTURE"};
static char *dbg_status[2] = {"ON", "OFF"};

int fimc_open(struct instance *i)
{
	struct v4l2_capability cap;
	int ret;

	i->fimc.fd = open(i->fimc.name, O_RDWR, 0);
	if (i->fimc.fd < 0) {
		err("Failed to open FIMC: %s", i->fimc.name);
		free(i->fimc.name);
		return -1;
	}

	memzero(cap);
	ret = ioctl(i->fimc.fd, VIDIOC_QUERYCAP, &cap);
	if (ret != 0) {
		err("Failed to verify capabilities");
		return -1;
	}

	dbg("FIMC Info (%s): driver=\"%s\" bus_info=\"%s\" card=\"%s\" fd=0x%x",
			i->fimc.name, cap.driver, cap.bus_info, cap.card, i->fimc.fd);

	if (	!(((cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) &&
		  (cap.capabilities & V4L2_CAP_VIDEO_OUTPUT_MPLANE)) ||
		  (cap.capabilities & V4L2_CAP_VIDEO_M2M_MPLANE)) ||
		!(cap.capabilities & V4L2_CAP_STREAMING)) {
		err("Insufficient capabilities of FIMC device (is %s correct?)",
									i->fimc.name);
		free(i->fimc.name);

		return -1;
	}
	free(i->fimc.name);

        return 0;
}

void fimc_close(struct instance *i)
{
	close(i->fimc.fd);
}

int fimc_sfmt(struct instance *i, int width, int height,
	 enum v4l2_buf_type type, unsigned long pix_fmt, int num_planes,
	 struct v4l2_plane_pix_format planes[])
{
	struct v4l2_format fmt;
	int ret;
	int n;

	memzero(fmt);
	fmt.fmt.pix_mp.pixelformat = pix_fmt;
	fmt.type = type;
	fmt.fmt.pix_mp.width = width;
	fmt.fmt.pix_mp.height = height;
	fmt.fmt.pix_mp.num_planes = num_planes;

	for (n = 0; n < num_planes; n++)
		memcpy(&fmt.fmt.pix_mp.plane_fmt[n], &planes[n],
			sizeof(*planes));

	fmt.fmt.pix_mp.field = V4L2_FIELD_ANY;

	ret = ioctl(i->fimc.fd, VIDIOC_S_FMT, &fmt);

	if (ret != 0) {
		err("Failed to SFMT on %s of FIMC",
			dbg_type[type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE]);
		return -1;
	}

	if (fmt.fmt.pix_mp.width != width ||
		fmt.fmt.pix_mp.height != height ||
		fmt.fmt.pix_mp.num_planes != num_planes ||
		fmt.fmt.pix_mp.pixelformat != pix_fmt) {
		err("Format was changed by FIMC so we abort operations\n"
		    "This may happen around kernel 3.16 as the format supported by FIMC\n"
		    "was changed.\n\n"
		    "To fix this please change function fimc_setup_capture in fimc.c (around line 162)\n"
		    "It should use V4L2_PIX_FMT_BGR32 for 3.16+ instead of V4L2_PIX_FMT_RGB32 used by 3.15-\n\n"
		    "Please read patch: \"s5p-fimc: Changed RGB32 to BGR32\"\n"
		    "\thttp://www.spinics.net/lists/linux-media/msg74953.html\n"
		    "\thttps://patchwork.linuxtv.org/patch/23236/");
		if (i->fimc.ignore_format_change) {
			err("!!! IGNORING FORMAT CHANGE !!!");
		} else {
			return -1;
		}
	}

	dbg("Successful SFMT on %s of FIMC (%dx%d)",
			dbg_type[type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE],
			width, height);

	return 0;
}

int fimc_setup_output_from_mfc(struct instance *i)
{
	struct v4l2_plane_pix_format planes[MFC_CAP_PLANES];
	struct v4l2_requestbuffers reqbuf;
	int ret;
	int n;

	memzero(planes);
	for (n = 0; n < MFC_CAP_PLANES; n++) {
		planes[n].sizeimage = i->mfc.cap_buf_size[n];
		planes[n].bytesperline = i->mfc.cap_w;
	}

	ret = fimc_sfmt(i, i->mfc.cap_w, i->mfc.cap_h,
		V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, i->mfc.cap_pixfmt,
		MFC_CAP_PLANES, planes);

	if (ret)
		return ret;

	memzero(reqbuf);
	reqbuf.count = i->mfc.cap_buf_cnt;
	reqbuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	if (i->fimc.dmabuf) {
		reqbuf.memory = V4L2_MEMORY_DMABUF;
	} else {
		reqbuf.memory = V4L2_MEMORY_USERPTR;
	}

	ret = ioctl(i->fimc.fd, VIDIOC_REQBUFS, &reqbuf);
	if (ret) {
		err("REQBUFS failed on OUTPUT of FIMC");
		return -1;
	}

	dbg("Succesfully setup OUTPUT of FIMC");

	return 0;
}

int fimc_setup_capture(struct instance *i)
{
	struct v4l2_plane_pix_format planes[MFC_OUT_PLANES];
	struct v4l2_requestbuffers reqbuf;
	unsigned long fmt;
	int ret;

	memzero(planes);
	planes[0].sizeimage = i->fimc.stride * i->fimc.height;
	planes[0].bytesperline = i->fimc.stride;

	switch (i->fimc.bpp) {
	case 16:
		fmt = V4L2_PIX_FMT_RGB565;
		break;
	case 32:
		fmt = V4L2_PIX_FMT_BGR32;
		break;
	default:
		err("Framebuffer format in not recognized. Bpp=%d", i->fimc.bpp);
		return -1;
	}

	ret = fimc_sfmt(i, i->fimc.width, i->fimc.height,
		V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, fmt, 1, planes);

	if (ret)
		return -1;

	memzero(reqbuf);
	reqbuf.count = i->fimc.buffers;
	dbg("reqbuf.count = %d", reqbuf.count);
	reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	if (i->fimc.dmabuf && i->drm.enabled)
		reqbuf.memory = V4L2_MEMORY_DMABUF;
	else
		reqbuf.memory = V4L2_MEMORY_USERPTR;

	ret = ioctl(i->fimc.fd, VIDIOC_REQBUFS, &reqbuf);
	if (ret) {
		err("REQBUFS failed on CAPTURE of FIMC");
		return -1;
	}

	dbg("Succesfully setup CAPTURE of FIMC");

	return 0;
}

int fimc_stream(struct instance *i, enum v4l2_buf_type type, int status)
{
	int ret;

	ret = ioctl(i->fimc.fd, status, &type);
	if (ret) {
		err("Failed to change streaming on FIMC (type=%s, status=%s)",
			dbg_type[type==V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE],
			dbg_status[status==VIDIOC_STREAMOFF]);
		return -1;
	}

	dbg("Stream %s on %s queue\n", dbg_status[status==VIDIOC_STREAMOFF],
		dbg_type[type==V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE]);

	return 0;
}

int fimc_dec_queue_buf_out_from_mfc(struct instance *inst, int n)
{
	struct v4l2_buffer buf;
	struct v4l2_plane planes[MFC_CAP_PLANES];
	int ret,p;

	memzero(buf);
	memzero(planes);
	buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	buf.index = n;
	buf.m.planes = planes;
	buf.length = MFC_CAP_PLANES;

	if (inst->fimc.dmabuf) {
		buf.memory = V4L2_MEMORY_DMABUF;
	} else {
		buf.memory = V4L2_MEMORY_USERPTR;
	}

	for(p = 0; p < 2; p++) {
		buf.m.planes[p].bytesused = inst->mfc.cap_buf_size[p];
		buf.m.planes[p].length = inst->mfc.cap_buf_size[p];

		if (inst->fimc.dmabuf) {
			buf.m.planes[p].m.fd = inst->mfc.dbuf[n][p];
		} else {
			buf.m.planes[p].m.userptr = (unsigned long)inst->mfc.cap_buf_addr[n][p];
		}
	}
	ret = ioctl(inst->fimc.fd, VIDIOC_QBUF, &buf);

	if (ret) {
		err("Failed to queue buffer (index=%d) on CAPTURE", n);
		return -1;
	}

	dbg("Queued buffer on CAPTURE queue with index %d", n);

	return 0;
}

int fimc_dec_queue_buf_cap(struct instance *i, int n)
{
	struct v4l2_buffer buf;
	struct v4l2_plane planes[FIMC_CAP_PLANES];
	int ret;

	memzero(buf);
	memzero(planes);
	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	buf.index = n;
	buf.m.planes = planes;
	buf.length = FIMC_CAP_PLANES;

	buf.m.planes[0].bytesused = i->fimc.size;
	buf.m.planes[0].length = i->fimc.size;
	if (i->fimc.dmabuf && i->drm.enabled) {
		buf.memory = V4L2_MEMORY_DMABUF;
		buf.m.planes[0].m.fd = i->fimc.dbuf[n];
	} else {
		buf.memory = V4L2_MEMORY_USERPTR;
		buf.m.planes[0].m.userptr = (unsigned long)i->fimc.p[n];
	}

	ret = ioctl(i->fimc.fd, VIDIOC_QBUF, &buf);

	if (ret) {
		err("Failed to queue buffer (index=%d) on CAPTURE", n);
		return -1;
	}

	dbg("Queued buffer on OUTPUT queue with index %d", n);

	return 0;
}

int fimc_dec_dequeue_buf(struct instance *i, int *n, int nplanes, int type)
{
	struct v4l2_buffer buf;
	struct v4l2_plane planes[MFC_MAX_PLANES];
	int ret;

	memzero(buf);
	memzero(planes);
	buf.type = type;
	buf.memory = V4L2_MEMORY_USERPTR;
	buf.m.planes = planes;
	buf.length = nplanes;

	ret = ioctl(i->fimc.fd, VIDIOC_DQBUF, &buf);

	if (ret) {
		err("Failed to dequeue buffer");
		return -1;
	}

	*n = buf.index;

	dbg("Dequeued buffer with index %d on %s queue", buf.index,
		dbg_type[type==V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE]);

	return 0;
}

int fimc_dec_dequeue_buf_cap(struct instance *i, int *n)
{
	return fimc_dec_dequeue_buf(i, n, 1, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
}

int fimc_dec_dequeue_buf_out(struct instance *i, int *n)
{
	return fimc_dec_dequeue_buf(i, n, 2, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
}

int fimc_set_crop(struct instance *i, int type, int width, int height, int left,
									int top)
{
	struct v4l2_crop crop;

	memzero(crop);
	crop.type = type;
	crop.c.width = width;
	crop.c.height = height;
	crop.c.left = left;
	crop.c.top = top;

	if (ioctl(i->fimc.fd, VIDIOC_S_CROP, &crop)) {
		err("Failed to set CROP on %s",
			dbg_type[type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE]);
		return -1;
	}

	return 0;
}

