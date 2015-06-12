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
#include "gem.h"

int exynos_gem_create(int fd, struct drm_exynos_gem_create *gem)
{
	int ret;
    if (!gem) {
        fprintf(stderr,"%s: %d - GEM object is null\n",__func__,__LINE__);
        return -EINVAL;
    }

    ret = ioctl(fd, DRM_IOCTL_EXYNOS_GEM_CREATE, gem)  ;
    if(ret){
        fprintf(stderr,"%s: %d - Failed to create GEM buffer\n %d %s",__func__,__LINE__,ret,strerror(errno));
        return -EINVAL;

    }
    return 0;
}

int exynos_gem_map_offset(int fd, struct drm_mode_map_dumb *map_off)
{

    if (ioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, map_off) < 0) {
	 fprintf(stderr,"%s: %d - Failed to get buffer offset\n",__func__,__LINE__);
        return -EINVAL;
    }
    return 0;
}
int exynos_gem_mmap(int fd, struct exynos_gem_mmap_data *in_mmap)
{
    int ret;
    void *map;
    struct drm_mode_map_dumb arg;

    memset(&arg, 0, sizeof(arg));
    arg.handle = in_mmap->handle;

    ret = ioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &arg);
    if (ret) {
        fprintf(stderr, "%s: %d - failed to map dumb buffer: %s\n",
                __func__, __LINE__, strerror(errno));
        return ret;
    }

    in_mmap->offset = arg.offset;

    map = mmap(NULL, (size_t)in_mmap->size, PROT_READ | PROT_WRITE,
               MAP_SHARED, fd, (off_t)arg.offset);
    if (map == MAP_FAILED) {
        fprintf(stderr, "%s: %d - failed to mmap buffer: %s\n",
                __func__, __LINE__, strerror(errno));
        return -EFAULT;
    }

    in_mmap->addr = map;

    return 0;
}

int exynos_gem_close(int fd, struct drm_gem_close *gem_close)
{
    int ret = 0;

    ret = ioctl(fd, DRM_IOCTL_GEM_CLOSE, gem_close);
    if (ret < 0)
        fprintf(stderr, "%s: %d - failed to close: %s\n", __func__, __LINE__,  strerror(-ret));
    return ret;
}