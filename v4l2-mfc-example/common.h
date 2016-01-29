/*
 * V4L2 Codec decoding example application
 * Kamil Debski <k.debski@samsung.com>
 *
 * Common stuff header file
 *
 * Copyright 2012 - 2015 Samsung Electronics Co., Ltd.
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

#ifndef INCLUDE_COMMON_H
#define INCLUDE_COMMON_H

#include <stdio.h>
#include <semaphore.h>

#include "parser.h"
#include "queue.h"

/* When ADD_DETAILS is defined every debug and error message contains
 * information about the file, function and line of code where it has
 * been called */
#define ADD_DETAILS
/* When DEBUG is defined debug messages are printed on the screen.
 * Otherwise only error messages are displayed. */
#define DEBUG
/* Remove #define DRM will disable DRM support */
#define DRM

#ifdef DRM
#include <libdrm/drm.h>
#include <libdrm/drm_mode.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <exynos/exynos_drm.h>
#include <libdrm/drm_fourcc.h>
#include <linux/videodev2.h>
#endif

#ifdef ADD_DETAILS
#define err(msg, ...) \
	fprintf(stderr, "Error (%s:%s:%d): " msg "\n", __FILE__, \
		__func__, __LINE__, ##__VA_ARGS__)
#else
#define err(msg, ...) \
	fprintf(stderr, "Error: " msg "\n", __FILE__, ##__VA_ARGS__)
#endif /* ADD_DETAILS */

#ifdef DEBUG
#ifdef ADD_DETAILS
#define dbg(msg, ...) \
	fprintf(stdout, "(%s:%s:%d): " msg "\n", __FILE__, \
		__func__, __LINE__, ##__VA_ARGS__)
#else
#define dbg(msg, ...) \
	fprintf(stdout, msg "\n", ##__VA_ARGS__)
#endif /* ADD_DETAILS */
#else /* DEBUG */
#define dbg(...) {}
#endif /* DEBUG */

#define memzero(x)\
        memset(&(x), 0, sizeof (x));

/* Maximum number of output buffers */
#define MFC_MAX_OUT_BUF 16
/* Maximum number of capture buffers (32 is the limit imposed by MFC */
#define MFC_MAX_CAP_BUF 32
/* Number of output planes */
#define MFC_OUT_PLANES 1
/* Number of capture planes */
#define MFC_CAP_PLANES 2
/* Maximum number of planes used in the application */
#define MFC_MAX_PLANES MFC_CAP_PLANES
/* Number of FIMC capture planes = number of frame buffer planes */
#define FIMC_CAP_PLANES 1
/* Maximum number of frame buffers - used for double buffering and
 * vsyns synchronisation */
#define MAX_BUFS 2

/* The buffer is free to use by MFC */
#define BUF_FREE 0
/* The buffer is currently queued in MFC */
#define BUF_MFC 1
/* The buffer has been processed by MFC and is now queued
 * to be processed by FIMC. */
#define BUF_FIMC 2

#define INVALID_GEM_HANDLE  ~0U

#define DRM_IPP_MAX_BUF		3
#define DRM_IPP_MAX_PLANES	3

struct drm_buff {
	unsigned int 	index;
	unsigned int 	handle[DRM_IPP_MAX_PLANES];
	int		fd[DRM_IPP_MAX_PLANES];
	unsigned int	fb_id;
};

struct shared_buffer {
	unsigned int		index;
	unsigned int		width;
	unsigned int		height;
	unsigned int		num_planes;
	struct v4l2_plane	planes[DRM_IPP_MAX_PLANES];
	unsigned int		handle[DRM_IPP_MAX_PLANES];
};

struct instance {
	/* Input file related parameters */
	struct {
		char *name;
		int fd;
		char *p;
		int size;
		int offs;
	} in;

	/* DRM related parameters */
	struct {
		char *name;
		int fd;

		int fb[MAX_BUFS];
		int crtc[MAX_BUFS];

		int width;
		int height;

		char *p[MAX_BUFS];

		unsigned int autodetect;
		unsigned int conn_id;
		unsigned int crtc_id;
#ifdef DRM
		struct drm_exynos_gem_create gem[MAX_BUFS];
		struct drm_exynos_gem_create gem_src[MFC_MAX_CAP_BUF];
		struct drm_mode_map_dumb mmap[MAX_BUFS];
		struct drm_prime_handle prime[MAX_BUFS];

		drmModeRes *resources;
#endif
		char enabled;

	} drm;

	/* Frame buffer related parameters */
	struct {
		char *name;
		int fd;
		char *p[MAX_BUFS];
		int buffers;
		int width;
		int height;
		int virt_width;
		int virt_height;
		int bpp;
		int stride;
		int size;
		int full_size;
		char enabled;
	} fb;

	/* FIMC related parameter */
	struct {
		char *name;
		int fd;
		struct queue queue;
		sem_t todo;
		/* Semaphores are used to synchronise FIMC thread with
		 * the MFC thread */
		sem_t done;
		char enabled;

		int cur_buf;
		int double_buf;
		int size;
		int buffers;
		int bpp;
		int width;
		int stride;
		int height;
		char *p[MAX_BUFS];
		int dbuf[MAX_BUFS];
		int dmabuf;
		char ignore_format_change;
	} fimc;
	struct drm_ipp {
		int enabled;
		struct drm_exynos_ipp_queue_buf *queue_buf;
		struct drm_exynos_ipp_property prop;
		struct queue queue;
		sem_t todo;
		sem_t done;
		int cur_buf;
		struct drm_exynos_ipp_queue_buf *src_buff[DRM_IPP_MAX_BUF];
		struct drm_exynos_ipp_queue_buf *dst_buff[DRM_IPP_MAX_BUF];
	} ipp;
	/* MFC related parameters */
	struct {
		char *name;
		int fd;

		/* Output queue related */
		int out_buf_cnt;
		int out_buf_size;
		int out_buf_off[MFC_MAX_OUT_BUF];
		char *out_buf_addr[MFC_MAX_OUT_BUF];
		int out_buf_flag[MFC_MAX_OUT_BUF];
		/* Capture queue related */
		int cap_w;
		int cap_h;
		int cap_crop_w;
		int cap_crop_h;
		int cap_crop_left;
		int cap_crop_top;
		int cap_buf_cnt;
		int cap_buf_cnt_min;
		int cap_buf_size[MFC_CAP_PLANES];
		int cap_buf_off[MFC_MAX_CAP_BUF][MFC_CAP_PLANES];
		char *cap_buf_addr[MFC_MAX_CAP_BUF][MFC_CAP_PLANES];
		int cap_buf_flag[MFC_MAX_CAP_BUF];
		int cap_buf_queued;
		int dbuf[MFC_MAX_CAP_BUF][MFC_CAP_PLANES];
		unsigned int cap_pixfmt;
	} mfc;

	/* Parser related parameters */
	struct {
		struct mfc_parser_context ctx;
		unsigned long codec;
		/* Callback function to the real parsing function.
		 * Dependent on the codec used. */
		int (*func)( struct mfc_parser_context *ctx,
		        char* in, int in_size, char* out, int out_size,
		        int *consumed, int *frame_size, char get_head);
		/* Set when the parser has finished and end of file has
		 * been reached */
		int finished;
	} parser;


	/* Control */
	int error; /* The error flag */
	int finish;  /* Flag set when decoding has been completed and all
			threads finish */
};

#endif /* INCLUDE_COMMON_H */

