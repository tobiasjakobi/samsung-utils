/*
 * V4L2 Codec decoding example application
 * Kamil Debski <k.debski@samsung.com>
 *
 * MFC operations header file
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

#ifndef INCLUDE_MFC_H
#define INCLUDE_MFC_H

#include "common.h"

/* Open the MFC device */
int	mfc_open(struct instance *i);
/* Close the MFC devices */
void	mfc_close(struct instance *i);
/* Setup the OUTPUT queue. The size determines the size for the stream
 * buffer. This is the maximum size a single compressed frame can have.
 * The count is the number of the stream buffers to allocate. */
int	mfc_dec_setup_output(struct instance *i, unsigned long codec,
					unsigned int size, int count);
/* Queue OUTPUT buffer */
int	mfc_dec_queue_buf_out(struct instance *i, int n, int length);
/* Queue CAPTURE buffer */
int	mfc_dec_queue_buf_cap(struct instance *i, int n);
/* Control MFC streaming */
int	mfc_stream(struct instance *i, enum v4l2_buf_type type, int status);
/* Setup CAPTURE queue. The argument extra_buf means the number of extra
 * buffers that should added to the minimum number of buffers required
 * by MFC. The final number of buffers allocated is stored in the instance
 * structure. */
int	mfc_dec_setup_capture(struct instance *i, int extra_buf);
/* Dequeue a buffer, the structure *buf is used to return the parameters of the
 * dequeued buffer. */
int	mfc_dec_dequeue_buf(struct instance *i, struct v4l2_buffer *buf);
/* This function is for EOS*/
int mfc_dec_stop(struct instance *i);
#endif /* INCLUDE_MFC_H */

