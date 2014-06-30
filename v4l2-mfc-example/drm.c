/*
 * V4L2 Codec decoding example application
 * Kamil Debski <k.debski@samsung.com>
 *
 * DRM operations
 *
 * Copyright 2014 Samsung Electronics Co., Ltd.
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

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "common.h"
#include "drm.h"
#include <string.h>

#ifdef DRM

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <exynos_drm.h>

int drm_open(struct instance *i, char *name)
{
	char mode[32];
	drmModeCrtc *crtc;
	struct drm_gem_close args;
	drmModeConnector *connector;
	drmModeModeInfo *mode_drm;
	unsigned int fb_id;
	int ret;
	int n;

	i->drm.fd =drmOpen(name, NULL);
	if (i->drm.fd < 0) {
		err("Failed to open drm module \"%s\"", name);
		return -1;
	}

	i->drm.resources = drmModeGetResources(i->drm.fd);

	crtc = drmModeGetCrtc(i->drm.fd, i->drm.crtc_id);
	if (!crtc) {
		err("Failed to get crtc: %d", i->drm.crtc_id);
		return -1;
	}

	i->drm.width = crtc->width;
	i->drm.height = crtc->height;

	dbg("DRM crtc (%d) resolution: %dx%d @", i->drm.crtc_id, i->drm.width,
							i->drm.height);
	drmModeFreeCrtc(crtc);

	for (n = 0; n < (i->fimc.double_buf?2:1); n++) {
		i->drm.gem[n].size = i->drm.width * i->drm.height * 4;
		ret = ioctl(i->drm.fd, DRM_IOCTL_EXYNOS_GEM_CREATE,
								&i->drm.gem[n]);
		if (ret < 0) {
			err("Failed to create gem");
			return -1;
		}
		args.handle = i->drm.gem[n].handle;
		i->drm.mmap[n].handle = i->drm.gem[n].handle;
		i->drm.mmap[n].size = i->drm.gem[n].size;
		ret = ioctl(i->drm.fd, DRM_IOCTL_EXYNOS_GEM_MMAP, &i->drm.mmap[n]);
		if (ret < 0) {
			ioctl(i->drm.fd, DRM_IOCTL_GEM_CLOSE, &args);
			err("Failed to mmap gem");
			return -1;
		}
		ret = drmModeAddFB(i->drm.fd, i->drm.width, i->drm.height, 32,
			32, 4 *  i->drm.width, i->drm.gem[n].handle, &fb_id);
		if (ret) {
			munmap((void *)(unsigned long)i->drm.mmap[n].mapped,
							i->drm.gem[n].size);
			ioctl(i->drm.fd, DRM_IOCTL_GEM_CLOSE, &args);
			err("Failed to add fb");
			return -1;
		}
		i->drm.fb[n] = fb_id;
		i->drm.p[n] = (char *)(unsigned int)i->drm.mmap[n].mapped;
		i->fimc.p[n] = (char *)(unsigned int)i->drm.mmap[n].mapped;
	}

	snprintf(mode, 32, "%dx%d", crtc->width, crtc->height);

	connector = drmModeGetConnector(i->drm.fd, i->drm.conn_id);
	if (!connector || connector->connector_id != i->drm.conn_id) {
		err("Couldn't find the connector");
		drmModeFreeConnector(connector);
		return -1;
	}

	mode_drm = 0;

	for (n = 0; n < connector->count_modes; n++) {
		if (!strncmp(connector->modes[n].name, mode, 32)) {
			mode_drm = &connector->modes[n];
			break;
		}
	}

	if (!mode_drm) {
		err("Failed to find mode\n");
		drmModeFreeConnector(connector);
		return -1;
	}

	ret = drmModeSetCrtc(i->drm.fd, i->drm.crtc_id, i->drm.fb[0], 0, 0,
		&i->drm.conn_id, 1, mode_drm);
	if (ret) {
		err("Failed to set crtc");
		drmModeFreeConnector(connector);
		return -1;
	}

	i->fimc.width = i->drm.width;
	i->fimc.height = i->drm.height;
	i->fimc.stride = i->drm.width * 4;
	i->fimc.bpp = 32;
	i->fimc.buffers = (i->fimc.double_buf?2:1);
	i->fimc.size = i->drm.width * i->drm.height * 4;

	i->drm.crtc[0] = i->drm.crtc_id;
	i->drm.crtc[1] = i->drm.crtc_id;

	return 0;
}

void drm_close(struct instance *i)
{
	drmClose(i->drm.fd);
}

#else /* DRM */

int drm_open(struct instance *i, char *name)
{
	return 0;
};

void drm_close(struct instance *i) {
	return;
}

#endif /* DRM */
