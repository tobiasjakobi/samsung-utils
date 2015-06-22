/*
 * V4L2 Codec decoding example application
 * Kamil Debski <k.debski@samsung.com>
 * Mateusz Krawczuk <m.krawczuk@samsung.com>
 *
 * DRM operations
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

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "common.h"
#include "drm.h"
#include <string.h>
#include <errno.h>
#include <drm/drm_fourcc.h>

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

struct type_name {
	int type;
	char *name;
};

#define type_name_fn(res) \
char * res##_str(int type) {			\
	int i;						\
	for (i = 0; i < ARRAY_SIZE(res##_names); i++) { \
		if (res##_names[i].type == type)	\
			return res##_names[i].name;	\
	}						\
	return "(invalid)";				\
}

#ifdef DRM

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm/exynos_drm.h>
#include "gem.h"
#include "ipp.h"

struct type_name encoder_type_names[] = {
	{ DRM_MODE_ENCODER_NONE, "none" },
	{ DRM_MODE_ENCODER_DAC, "DAC" },
	{ DRM_MODE_ENCODER_TMDS, "TMDS" },
	{ DRM_MODE_ENCODER_LVDS, "LVDS" },
	{ DRM_MODE_ENCODER_TVDAC, "TVDAC" },
};

type_name_fn(encoder_type)

struct type_name connector_status_names[] = {
	{ DRM_MODE_CONNECTED, "connected" },
	{ DRM_MODE_DISCONNECTED, "disconnected" },
	{ DRM_MODE_UNKNOWNCONNECTION, "unknown" },
};

type_name_fn(connector_status)

struct type_name connector_type_names[] = {
	{ DRM_MODE_CONNECTOR_Unknown, "unknown" },
	{ DRM_MODE_CONNECTOR_VGA, "VGA" },
	{ DRM_MODE_CONNECTOR_DVII, "DVI-I" },
	{ DRM_MODE_CONNECTOR_DVID, "DVI-D" },
	{ DRM_MODE_CONNECTOR_DVIA, "DVI-A" },
	{ DRM_MODE_CONNECTOR_Composite, "composite" },
	{ DRM_MODE_CONNECTOR_SVIDEO, "s-video" },
	{ DRM_MODE_CONNECTOR_LVDS, "LVDS" },
	{ DRM_MODE_CONNECTOR_Component, "component" },
	{ DRM_MODE_CONNECTOR_9PinDIN, "9-pin DIN" },
	{ DRM_MODE_CONNECTOR_DisplayPort, "displayport" },
	{ DRM_MODE_CONNECTOR_HDMIA, "HDMI-A" },
	{ DRM_MODE_CONNECTOR_HDMIB, "HDMI-B" },
	{ DRM_MODE_CONNECTOR_TV, "TV" },
	{ DRM_MODE_CONNECTOR_eDP, "embedded displayport" },
};

type_name_fn(connector_type)

int dump_encoders(struct instance *i);
int dump_connectors(struct instance *i);
int dump_crtcs(struct instance *i);
int dump_framebuffers(struct instance *i);


/*
 * drm_open calls functions to detect DRM parameters and setup gem and display.
 */
int drm_open(struct instance *i)
{
	char mode[32];
	char *name = "exynos";
	drmModeCrtc *crtc;
	struct drm_gem_close args;
	drmModeConnector *connector;
	drmModeModeInfo *mode_drm;
	unsigned int fb_id;
	int ret;
	int n;
	void *map = NULL;

/*
 *Open drm file descriptor
 */
	i->drm.fd =drmOpen(name, NULL);
	if (i->drm.fd < 0) {
		err("Failed to open drm module \"%s\"", name);
		return -1;
	}	i->drm.resources = drmModeGetResources(i->drm.fd);
/*
 *Auto detect Crtc and Connectors
 */

	if(i->drm.autodetect) {
		i->drm.crtc_id = dump_crtcs(i);
		i->drm.conn_id = dump_connectors(i);
	}
	printf("Conn_id:%i \n",i->drm.conn_id);
	printf("Crtc_id:%i \n",i->drm.crtc_id);

/*
 *Detect screen width and height
 */

	crtc = drmModeGetCrtc(i->drm.fd, i->drm.crtc_id);
	if (!crtc) {
		err("Failed to get crtc: %d", i->drm.crtc_id);
		return -1;
	}

	i->drm.width = crtc->width;
	i->drm.height = crtc->height;

	printf("DRM crtc (%d) resolution: %dx%d @", i->drm.crtc_id, i->drm.width,
							i->drm.height);
	drmModeFreeCrtc(crtc);

	for (n = 0; n < 3; n++) {
		i->drm.gem[n].size = i->drm.width * i->drm.height * 4;
		ret = ioctl(i->drm.fd, DRM_IOCTL_EXYNOS_GEM_CREATE,
								&i->drm.gem[n]);
		if (ret < 0) {
			   fprintf(stderr,
                    "Failed to Create Gem %s\n", strerror(errno));
			return -1;
		}
		args.handle = i->drm.gem[n].handle;

		 if (i->fimc.dmabuf)
			ret = drmPrimeHandleToFD(i->drm.fd, i->drm.gem[n].handle, DRM_CLOEXEC, &i->fimc.dbuf[n]);
			if (ret < 0) {
				ioctl(i->drm.fd, DRM_IOCTL_GEM_CLOSE, &args);
				err("Failed to expbuf a gem object");
				return -1;
			}
		ret = drmModeAddFB(i->drm.fd, i->drm.width, i->drm.height, 32,
			32, 4 *  i->drm.width, i->drm.gem[n].handle, &fb_id);
		if (ret) {
			munmap((void *)(unsigned long)map,i->drm.gem[n].size);
			ioctl(i->drm.fd, DRM_IOCTL_GEM_CLOSE, &args);
			err("Failed to add fb");
			return -1;
		}
		i->drm.fb[n] = fb_id;
	}

	snprintf(mode, 32, "%dx%d", i->drm.width , i->drm.height);
/*
 * Use detected Connector
 */
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
/*
 * Use detected Crtc
 */
	ret = drmModeSetCrtc(i->drm.fd, i->drm.crtc_id, i->drm.fb[0], 0, 0,
		&i->drm.conn_id, 1, mode_drm);
	if (ret) {
		err("Failed to set crtc");
		drmModeFreeConnector(connector);
		return -1;
	}
/*
 * Setup fimc if DRM IPP is not enabled
 */
	if(i->fimc.enabled) {
	i->fimc.width = i->drm.width;
	i->fimc.height = i->drm.height;
	i->fimc.stride = i->drm.width * 4;
	i->fimc.bpp = 32;
	i->fimc.buffers = (i->fimc.double_buf?2:1);
	i->fimc.size = i->drm.width * i->drm.height * 4;
	}
	i->drm.crtc[0] = i->drm.crtc_id;
	i->drm.crtc[1] = i->drm.crtc_id;

	if(i->ipp.enabled) {
	exynos_drm_ipp_setup(i);

	}

	return 0;
}

/*
 * Closing DRM and IPP
 */
void drm_close(struct instance *i)
{
	exynos_drm_ipp_close(i);
	drmClose(i->drm.fd);
}
/*
 * Requise instance to get drm file descriptor and put data to struct
 */
int dump_encoders(struct instance *i)
{
	drmModeEncoder *encoder;
	int j;

	printf("Encoders:\n");
	printf("id\tcrtc\ttype\tpossible crtcs\tpossible clones\t\n");
	for (j = 0; j < i->drm.resources->count_encoders; j++) {
		encoder = drmModeGetEncoder(i->drm.fd, i->drm.resources->encoders[j]);

		if (!encoder) {
			fprintf(stderr, "could not get encoder %i: %s\n",
				i->drm.resources->encoders[j], strerror(errno));
			continue;
		}
		printf("%d\t%d\t%s\t0x%08x\t0x%08x\n",
		       encoder->encoder_id,
		       encoder->crtc_id,
		       encoder_type_str(encoder->encoder_type),
		       encoder->possible_crtcs,
		       encoder->possible_clones);
		drmModeFreeEncoder(encoder);
	}
	printf("\n");

	return 0;
}

int dump_connectors(struct instance *i)
{
	drmModeConnector *connector;
	int g , j;

	printf("Connectors:\n");
	printf("id\tencoder\tstatus\t\ttype\tsize (mm)\tmodes\tencoders\n");
	for (g = 0; g < i->drm.resources->count_connectors; g++) {
		connector = drmModeGetConnector(i->drm.fd, i->drm.resources->connectors[g]);

		if (!connector) {
			fprintf(stderr, "could not get connector %i: %s\n",
				i->drm.resources->connectors[g], strerror(errno));
			continue;
		}

		printf("%d\t%d\t%s\t%s\t%dx%d\t\t%d\t",
		       connector->connector_id,
		       connector->encoder_id,
		       connector_status_str(connector->connection),
		       connector_type_str(connector->connector_type),
		       connector->mmWidth, connector->mmHeight,
		       connector->count_modes);

		for (j = 0; j < connector->count_encoders; j++)
			printf("%s%d", j > 0 ? ", " : "",
							connector->encoders[j]);
		printf("\n");

		if (!connector->count_modes)
			continue;

		printf("  modes:\n");
		printf("  name refresh (Hz) hdisp hss hse htot vdisp "
		       "vss vse vtot)\n");
		printf("  props:\n");
	return connector->connector_id;

		drmModeFreeConnector(connector);
	}
	printf("\n");

	return 0;
}

int dump_crtcs(struct instance *i)
{
	drmModeCrtc *crtc;
	int j,ret;


	printf("CRTCs:\n");
	printf("id\tfb\tpos\tsize\n");
	for (j = 0; j < i->drm.resources->count_crtcs; j++) {
		crtc = drmModeGetCrtc(i->drm.fd, i->drm.resources->crtcs[j]);

		if (!crtc) {
			fprintf(stderr, "could not get crtc %i: %s\n",
				i->drm.resources->crtcs[j], strerror(errno));
			continue;
		}
		ret = crtc->crtc_id;
		printf("%d\t%d\t(%d,%d)\t(%dx%d)\n",
			crtc->crtc_id,
			crtc->buffer_id,
			crtc->x, crtc->y,
			crtc->width, crtc->height);
		drmModeFreeCrtc(crtc);
	}
	return ret;
}

int dump_framebuffers(struct instance *i)
{
	drmModeFB *fb;
	int j;

	printf("Frame buffers:\n");
	printf("id\tsize\tpitch\n");
	for (j = 0; j < i->drm.resources->count_fbs; j++) {
		fb = drmModeGetFB(i->drm.fd, i->drm.resources->fbs[j]);

		if (!fb) {
			fprintf(stderr, "could not get fb %i: %s\n",
				i->drm.resources->fbs[j], strerror(errno));
			continue;
		}
		printf("%u\t(%ux%u)\t%u\n",
			fb->fb_id,
			fb->width, fb->height,
			fb->pitch);

		drmModeFreeFB(fb);
	}
	printf("\n");

	return 0;
}

#else /*if DRM is Disabled*/

int drm_open(struct instance *i)
{
	return 0;
};

void drm_close(struct instance *i) {
	return;
}

#endif /* DRM */
