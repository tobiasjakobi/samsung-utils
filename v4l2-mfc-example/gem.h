/*
 * V4L2 Codec decoding example application
 * Kamil Debski <k.debski@samsung.com>
 * Mateusz Krawczuk <m.krawczuk@samsung.com>
 * 
 * GEM operations header file
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
#ifndef INCLUDE_GEM_H
#define INCLUDE_GEM_H

#include "common.h"
#include <drm/exynos_drm.h>

struct exynos_gem_mmap_data {
    uint32_t handle;
    uint64_t size;
    uint64_t offset;
    void *addr;
};

int exynos_gem_create(int fd, struct drm_exynos_gem_create *gem);
int exynos_gem_map_offset(int fd, struct drm_mode_map_dumb *map_off);
int exynos_gem_mmap(int fd, struct exynos_gem_mmap_data *in_mmap);
int exynos_gem_close(int fd, struct drm_gem_close *gem_close);
#endif /* INCLUDE_IPP_H */
