/*
 * V4L2 Codec decoding example application
 * Kamil Debski <k.debski@samsung.com>
 *
 * DRM operations header file
 *
 * Copyright 2014 - 2015 Samsung Electronics Co., Ltd.
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

#ifndef INCLUDE_DRM_H
#define INCLUDE_DRM_H

#include <linux/videodev2.h>

#include <libdrm/drm.h>
#include <libdrm/drm_mode.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#ifdef DRM

/* Open and mmap DRM buffer. Also read its properties */
int	drm_open(struct instance *i);
/* Unmap and close the buffer */
void	drm_close(struct instance *i);
extern int exynos_drm_ipp_init(struct instance *i);
void exynos_drm_ipp_close(struct instance *inst);

struct connector {
	uint32_t id;
	char mode_str[64];
	drmModeModeInfo *mode;
	drmModeEncoder *encoder;
	int crtc;
	unsigned int fb_id[2], current_fb_id;
	struct timeval start;

	int swap_count;
};



#endif /* DRM */

#endif /* INCLUDE_DRM_H */

