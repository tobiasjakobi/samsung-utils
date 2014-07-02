/*
 * V4L2 Codec decoding example application
 * Kamil Debski <k.debski@samsung.com>
 *
 * Argument parser
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <linux/videodev2.h>

#include "common.h"
#include "parser.h"


void print_usage(char *name)
{
	// "d:f:i:m:c:V"
	printf("Usage:\n");
	printf("\t%s\n", name);
	printf("\t-c <codec> - The codec of the encoded stream\n");
	printf("\t\t     Available codecs: mpeg4, h264\n");
	printf("\t-d <device>  - Frame buffer device (e.g. /dev/fb0)\n");
	printf("\t-f <device> - FIMC device (e.g. /dev/video4)\n");
	printf("\t-i <file> - Input file name\n");
	printf("\t-m <device> - MFC device (e.g. /dev/video8)\n");
	printf("\t-D <module>:<crtc>:<conn> - DRM module (e.g. exynos:4:17)\n");
	printf("\t-B - use DMABUF instead of userptr for sharing buffers\n");
	printf("\t-V - synchronise to vsync\n");
	printf("\t\n");
	printf("\tIf DRM or Frame buffer is used then FIMC should be suppplied.\n");
	printf("\tOnly one of the following Frame Buffer, DRM can be used at a time.\n");
	printf("\n");
}

void init_to_defaults(struct instance *i)
{
	memset(i, 0, sizeof(*i));
}

int get_codec(char *str)
{
	if (strncasecmp("mpeg4", str, 5) == 0) {
		return V4L2_PIX_FMT_MPEG4;
	} else if (strncasecmp("h264", str, 5) == 0) {
		return V4L2_PIX_FMT_H264;
	} else if (strncasecmp("h263", str, 5) == 0) {
		return V4L2_PIX_FMT_H263;
	} else if (strncasecmp("xvid", str, 5) == 0) {
		return V4L2_PIX_FMT_XVID;
	} else if (strncasecmp("mpeg2", str, 5) == 0) {
		return V4L2_PIX_FMT_MPEG2;
	} else if (strncasecmp("mpeg1", str, 5) == 0) {
		return V4L2_PIX_FMT_MPEG1;
	}
	return 0;
}

int parse_args(struct instance *i, int argc, char **argv)
{
	char *tmp;
	int c;

	init_to_defaults(i);

	while ((c = getopt(argc, argv, "c:d:f:i:m:VD:B")) != -1) {
		switch (c) {
		case 'c':
			i->parser.codec = get_codec(optarg);
			break;
		case 'd':
			i->fb.name = optarg;
			i->fb.enabled = 1;
			break;
		case 'f':
			i->fimc.name = optarg;
			i->fimc.enabled = 1;
			break;
		case 'i':
			i->in.name = optarg;
			break;
		case 'm':
			i->mfc.name = optarg;
			break;
		case 'V':
			i->fimc.double_buf = 1;
			break;
		case 'B':
			i->fimc.dmabuf = 1;
			break;
		case 'D':
			/* Name */
			tmp = optarg;
			while (*tmp && *tmp != ':')
				tmp++;
			if (*tmp) {
				i->drm.name = optarg;
				*tmp = 0;
				dbg("DRM module name: %s", optarg);
				optarg = ++tmp;
			} else {
				break;
			}
			/* Crtc */
			while (*tmp && *tmp != ':')
				tmp++;
			if (*tmp) {
				*tmp = 0;
				i->drm.crtc_id = atoi(optarg);
				dbg("crtc: %d (%s)", i->drm.crtc_id, optarg);
				optarg = ++tmp;
			} else {
				break;
			}
			/* Crtc */
			while (*tmp && *tmp != ':')
				tmp++;
			i->drm.conn_id = atoi(optarg);
			dbg("conn: %d (%s)", i->drm.conn_id, optarg);
			i->drm.enabled = 1;
			break;
		default:
			err("Bad argument");
			return -1;
		}
	}

	if (!i->parser.codec) {
		err("Unknown or not set codec (-c)");
		return -1;
	}
	if (!i->in.name || !i->mfc.name) {
		err("The following arguments are required: -i -m -c");
		return -1;
	}
	if (i->fimc.enabled) {
		if (i->drm.enabled) {
			dbg("Using DRM for display");
		} else if (i->fb.enabled) {
			dbg("Using FrameBuffer for display\n");
		} else {
			err("When using FIMC a framewbuffer or DRM has to be used\n");
			return -1;
		}
	}

	switch (i->parser.codec) {
	case V4L2_PIX_FMT_XVID:
	case V4L2_PIX_FMT_H263:
	case V4L2_PIX_FMT_MPEG4:
		i->parser.func = parse_mpeg4_stream;
		break;
	case V4L2_PIX_FMT_H264:
		i->parser.func = parse_h264_stream;
		break;
	case V4L2_PIX_FMT_MPEG1:
	case V4L2_PIX_FMT_MPEG2:
		i->parser.func = parse_mpeg2_stream;
		break;
	}

	return 0;
}

