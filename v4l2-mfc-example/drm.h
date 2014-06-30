/*
 * V4L2 Codec decoding example application
 * Kamil Debski <k.debski@samsung.com>
 *
 * DRM operations header file
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

#ifndef INCLUDE_DRM_H
#define INCLUDE_DRM_H

#ifdef DRM

/* Open and mmap DRM buffer. Also read its properties */
int	drm_open(struct instance *i, char *name);
/* Unmap and close the buffer */
void	drm_close(struct instance *i);

#endif /* DRM */

#endif /* INCLUDE_DRM_H */

