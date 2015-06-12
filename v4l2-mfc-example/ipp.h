/*
 * V4L2 Codec decoding example application
 * Kamil Debski <k.debski@samsung.com>
 * Mateusz Krawczuk <m.krawczuk@samsung.com>
 * 
 * DRM IPP operations header file
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
#ifndef INCLUDE_IPP_H
#define INCLUDE_IPP_H

#include "common.h"
/* Set ipp properties */
int exynos_drm_ipp_setup(struct instance *i);
/**/
int exynos_drm_ipp_allocate_ipp_buffers(struct instance *i);
/* exynos_drm_ipp_do_buffer is for enqueue or dequeue buffor of both types */
int exynos_drm_ipp_do_buffer(int index,int buf_type, int type,struct instance *inst);
int exynos_drm_ipp_process_frame(struct instance *inst, int index);
/* Command for controling ipp */
int exynos_drm_ipp_cmd_ctrl(enum drm_exynos_ipp_ctrl ctrl, struct instance *inst);
int exynos_drm_ipp_dequeue_buffors(struct instance *inst, int index);
/* Setup OUTPUT queue of IPP basing on the configuration of MFC */
int exynos_drm_ipp_setup_output_from_mfc(struct instance *i);
/* Send command to stop ctrl and then clean up buffors and memory */
int exynos_drm_ipp_dec_queue_buf_out_from_mfc(struct instance *inst, int n);
/* Send command to stop ctrl and then clean up buffors and memory */
void exynos_drm_ipp_close(struct instance *inst);
#endif /* INCLUDE_IPP_H */

