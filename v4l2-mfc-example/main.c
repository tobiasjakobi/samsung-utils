/*
 * V4L2 Codec decoding example application
 * Kamil Debski <k.debski@samsung.com>
 *
 * Main file of the application
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

#include <stdio.h>
#include <string.h>
#include <linux/videodev2.h>
#include <pthread.h>
#include <semaphore.h>

#include "args.h"
#include "common.h"
#include "fb.h"
#include "fimc.h"
#include "fileops.h"
#include "mfc.h"
#include "parser.h"
#include "drm.h"
#include "ipp.h"

/* This is the size of the buffer for the compressed stream.
 * It limits the maximum compressed frame size. */
#define STREAM_BUUFER_SIZE	(1024 * 1024)
/* The number of compress4ed stream buffers */
#define STREAM_BUFFER_CNT	2

/* The number of extra buffers for the decoded output.
 * This is the number of buffers that the application can keep
 * used and still enable MFC to decode with the hardware. */
#define RESULT_EXTRA_BUFFER_CNT 2

void cleanup(struct instance *i)
{
	if (i->mfc.fd)
		mfc_close(i);
	if (i->fimc.fd)
		fimc_close(i);
	if (i->fb.fd)
		fb_close(i);
	if (i->in.fd)
		input_close(i);
	if (i->drm.fd)
		drm_close(i);
	if (i->fimc.enabled)
		queue_free(&i->fimc.queue);
}

int extract_and_process_header(struct instance *i)
{
	int used, fs;
	int ret;

	ret = i->parser.func(&i->parser.ctx, i->in.p + i->in.offs,
		i->in.size - i->in.offs, i->mfc.out_buf_addr[0],
		i->mfc.out_buf_size, &used, &fs, 1);

	if (ret == 0) {
		err("Failed to extract header from stream");
		return -1;
	}

	/* For H263 the header is passed with the first frame, so we should
	 * pass it again */
	if (i->parser.codec != V4L2_PIX_FMT_H263)
		i->in.offs += used;
	else
	/* To do this we shall reset the stream parser to the initial
	 * configuration */
		parse_stream_init(&i->parser.ctx);

	dbg("Extracted header of size %d", fs);

	ret = mfc_dec_queue_buf_out(i, 0, fs);

	if (ret)
		return -1;

	ret = mfc_stream(i, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, VIDIOC_STREAMON);

	if (ret)
		return -1;

	return 0;
}

int dequeue_output(struct instance *i, int *n)
{
	struct v4l2_buffer qbuf;
	struct v4l2_plane planes[MFC_OUT_PLANES];

	memzero(qbuf);
	memzero(planes[0]);
	qbuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	qbuf.memory = V4L2_MEMORY_MMAP;
	qbuf.m.planes = planes;
	qbuf.length = 1;

	if (mfc_dec_dequeue_buf(i, &qbuf))
		return -1;

	*n = qbuf.index;

	return 0;
}

int dequeue_capture(struct instance *i, int *n, int *finished)
{
	struct v4l2_buffer qbuf;
	struct v4l2_plane planes[MFC_CAP_PLANES];

	memzero(qbuf);
	memzero(planes[0]);
	memzero(planes[1]);

	qbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	qbuf.memory = V4L2_MEMORY_MMAP;
	qbuf.m.planes = planes;
	qbuf.length = 2;

	if (mfc_dec_dequeue_buf(i, &qbuf))
		return -1;

	dbg("Dequeued MFC sequence: %d\n", qbuf.sequence);

	*finished = qbuf.m.planes[0].bytesused == 0;

	*n = qbuf.index;

	return 0;
}

/* This threads is responsible for parsing the stream and
 * feeding MFC with consecutive frames to decode */
void *parser_thread_func(void *args)
{
	struct instance *i = (struct instance *)args;
	int ret;
	int used, fs, n;

	i->mfc.out_buf_flag[0] = 1;
	while (!i->error && !i->finish && !i->parser.finished) {
		n = 0;
		while (n < i->mfc.out_buf_cnt && i->mfc.out_buf_flag[n])
			n++;

		if (n < i->mfc.out_buf_cnt && !i->parser.finished) {
			dbg("parser.func = %p", i->parser.func);
			ret = i->parser.func(&i->parser.ctx,
				i->in.p + i->in.offs, i->in.size - i->in.offs,
				i->mfc.out_buf_addr[n], i->mfc.out_buf_size,
				&used, &fs, 0);


			if (ret == 0 && i->in.offs == i->in.size) {
				dbg("Parser has extracted all frames");
				i->parser.finished = 1;
				fs = 0;
				mfc_dec_stop(i);
				return 0;
			}

			dbg("Extracted frame of size %d", fs);

			dbg("Before OUTPUT queue");
			ret = mfc_dec_queue_buf_out(i, n, fs);
			dbg("After OUTPUT queue");

			i->mfc.out_buf_flag[n] = 1;

			i->in.offs += used;


		} else {
			dbg("Before OUTPUT dequeue");
			ret = dequeue_output(i, &n);
			dbg("After OUTPUT dequeue");
			i->mfc.out_buf_flag[n] = 0;
			if (ret && !i->parser.finished) {
				err("Failed to dequeue a buffer in parser_thread");
				i->error = 1;
			}
		}
	}
	dbg("Parser thread finished");
	return 0;
}

/* This thread handles the CAPTURE side of MFC. it receives
 * decoded frames and queues empty buffers back to MFC.
 * Also it passes the decoded frames to FIMC, so they
 * can be processed and displayed. */
void *mfc_thread_func(void *args)
{
	struct instance *i = (struct instance *)args;
	int finished;
	int n;

	while (!i->error && !i->finish) {
		if (i->mfc.cap_buf_queued < i->mfc.cap_buf_cnt_min) {
			/* sem_wait - wait until there is a buffer returned from
			 * fimc */
			if (i->ipp.enabled) {
				dbg("Before ipp.done");
				sem_wait(&i->ipp.done);
				dbg("After ipp.done");
			}
			else if (i->fimc.enabled) {
				dbg("Before fimc.done");
				sem_wait(&i->fimc.done);
				dbg("After fimc.done");
			}
			n = 0;
			while (n < i->mfc.cap_buf_cnt &&
				i->mfc.cap_buf_flag[n] != BUF_FREE)
				n++;

			if (n < i->mfc.cap_buf_cnt) {
				/* Can queue a buffer */
				mfc_dec_queue_buf_cap(i, n);
				i->mfc.cap_buf_flag[n] = 1;
				i->mfc.cap_buf_queued++;
			} else {
				err("Something went seriously wrong. There should be a buffer");
				i->error = 1;
				continue;
			}

			continue;
		}

		if (i->mfc.cap_buf_queued < i->mfc.cap_buf_cnt) {
			n = 0;
			while (n < i->mfc.cap_buf_cnt &&
				i->mfc.cap_buf_flag[n] != BUF_FREE)
				n++;

			if (n < i->mfc.cap_buf_cnt) {
				/* sem_wait - we already found a buffer to queue
				 * so no waiting */
				if (i->ipp.enabled) {
					dbg("Before ipp.done");
					sem_wait(&i->ipp.done);
					dbg("After ipp.done");
				}
				else if (i->fimc.enabled) {
					dbg("Before fimc.done");
					sem_wait(&i->fimc.done);
					dbg("After fimc.done");
				}
				/* Can queue a buffer */
				mfc_dec_queue_buf_cap(i, n);
				i->mfc.cap_buf_flag[n] = BUF_MFC;
				i->mfc.cap_buf_queued++;
				continue;
			}
		}

		if (i->mfc.cap_buf_queued >= i->mfc.cap_buf_cnt_min) {
			/* Can dequeue a processed buffer */
			if (dequeue_capture(i, &n, &finished)) {
				err("Error when dequeueing CAPTURE buffer");
				i->error = 1;
				break;
			}

			if (finished) {
				dbg("Finished extracting last frames");
				i->finish = 1;
				break;
			}
			if (i->ipp.enabled) {
				i->mfc.cap_buf_flag[n] = BUF_FIMC;
				i->mfc.cap_buf_queued--;
				queue_add(&i->ipp.queue, n);
				sem_post(&i->ipp.todo);
				dbg("Add IPP queue");

				continue;
			}

			else if (i->fimc.enabled) {
				/* Pass to the FIMC */
				i->mfc.cap_buf_flag[n] = BUF_FIMC;
				i->mfc.cap_buf_queued--;

				queue_add(&i->fimc.queue, n);
				sem_post(&i->fimc.todo);
			} else {
				i->mfc.cap_buf_flag[n] = BUF_FREE;
				i->mfc.cap_buf_queued--;
			}
			continue;
		}
	}

	dbg("MFC thread finished");
	return 0;
}

void
page_flip_handler(int fd, unsigned int frame,
                  unsigned int sec, unsigned int usec, void *data)
{
	printf("Handled %d %d %d\n", frame, sec, usec);	

}

/* This thread handles FIMC processing and optionally frame buffer
 * switching and synchronisation to the vsync of frame buffer. */
void *fimc_thread_func(void *args)
{
	static int frames = 0;
	static int first_run = 1;
	struct instance *i = (struct instance *)args;
	int n, tmp;

	i->fimc.cur_buf = 0;

	while (!i->error && (!i->finish ||
		(i->finish && !queue_empty(&i->fimc.queue))
		)) {
		dbg("Before fimc.todo");
		sem_wait(&i->fimc.todo);
		dbg("After fimc.todo");

		dbg("Processing by FIMC");

		n = queue_remove(&i->fimc.queue);

		if (i->mfc.cap_buf_flag[n] != BUF_FIMC) {
			err("Buffer chosen to be processed by FIMC in wrong");
			i->error = 1;
			break;
		}

		if (n >= i->mfc.cap_buf_cnt) {
			err("Strange. Could not find the buffer to process.");
			i->error = 1;
			break;
		}

		if (fimc_dec_queue_buf_out_from_mfc(i, n)) {
			i->error = 1;
			break;
		}

		if (fimc_dec_queue_buf_cap(i, i->fimc.cur_buf)) {
			i->error = 1;
			break;
		}

		if (first_run) {
			/* Since our fabulous V4L2 framework enforces that at
			 * least one buffer is queued before switching streaming
			 * on then we need to add the following code. Otherwise
			 * it could be ommited and it all would be handled by
			 * the setup sequence in main.*/
			first_run = 0;

			if (fimc_stream(i, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
							VIDIOC_STREAMON)) {
				i->error = 1;
				break;
			}
			if (fimc_stream(i, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
							VIDIOC_STREAMON)) {
				i->error = 1;
				break;
			}
		}

		frames++;

		dbg("Processed frame number: %d", frames);

		if (fimc_dec_dequeue_buf_cap(i, &tmp)) {
			i->error = 1;
			break;
		}
		if (fimc_dec_dequeue_buf_out(i, &tmp)) {
			i->error = 1;
			break;
		}

		if (i->fimc.double_buf && i->fb.enabled) {
			fb_set_virt_y_offset(i, i->fimc.cur_buf * i->fb.height);
			fb_wait_for_vsync(i);
		}

		if (i->fimc.double_buf && i->drm.enabled) {
			drmModePageFlip(i->drm.fd, i->drm.crtc[i->fimc.cur_buf],
				i->drm.fb[i->fimc.cur_buf],
				DRM_MODE_PAGE_FLIP_EVENT, 0);

			while (1) {
				struct timeval timeout = { .tv_sec = 3, .tv_usec = 0 };
				fd_set fds;
				int ret;
				drmEventContext evctx;

				memset(&evctx, 0, sizeof evctx);
				evctx.version = DRM_EVENT_CONTEXT_VERSION;
				evctx.vblank_handler = NULL;
				evctx.page_flip_handler = page_flip_handler;

				FD_ZERO(&fds);
				FD_SET(0, &fds);
				FD_SET(i->drm.fd, &fds);
				ret = select(i->drm.fd + 1, &fds, NULL, NULL, &timeout);

				if (ret <= 0)
					continue;
				else if (FD_ISSET(0, &fds))
					break;

				drmHandleEvent(i->drm.fd, &evctx);
				break;

			}

		}

		if (i->fimc.double_buf) {
			i->fimc.cur_buf++;
			i->fimc.cur_buf %= i->fimc.buffers;
		}

		i->mfc.cap_buf_flag[n] = BUF_FREE;

		sem_post(&i->fimc.done);
	}

	dbg("FIMC thread finished");
	return 0;
}

/* This thread handles FIMC processing and optionally frame buffer
 * switching and synchronisation to the vsync of frame buffer. */
void *ipp_thread_func(void *args)
{
	static int frames = 0;
	static int first_run = 1;
	struct instance *i = (struct instance *)args;
	int n;
	fprintf(stderr,"%s:%d Enter",__func__,__LINE__);

	i->ipp.cur_buf = 0;

	while (!i->error && (!i->finish ||
		(i->finish && !queue_empty(&i->ipp.queue))
		)) {
		dbg("Before ipp.todo");
		sem_wait(&i->ipp.todo);
		dbg("After ipp.todo");

		dbg("Processing by IPP");

		n = queue_remove(&i->ipp.queue);

		if (i->mfc.cap_buf_flag[n] != BUF_FIMC) {
			err("Buffer chosen to be processed by FIMC in wrong");
			i->error = 1;
			break;
		}

		if (n >= i->mfc.cap_buf_cnt) {
			err("Strange. Could not find the buffer to process.");
			i->error = 1;
			break;
		}
		printf("%s: %d n:%d i->mfc.cap_buf_cnt:%d \n", __func__, __LINE__, n, i->mfc.cap_buf_cnt);

		if (first_run) {
			first_run = 0;

			if (exynos_drm_ipp_cmd_ctrl(IPP_CTRL_PLAY, i)) {
				i->error = 1;
				break;
			}
		}
		if (exynos_drm_ipp_process_frame(i, n)) {
			i->error = 1;
			break;
		}
 		frames++;


		dbg("Processed frame number: %d", frames);

		if (exynos_drm_ipp_dequeue_buffors(i, i->ipp.cur_buf)) {
			i->error = 1;
			break;
		}

		i->mfc.cap_buf_flag[n] = BUF_FREE;
 
		sem_post(&i->ipp.done);
	}

	dbg("IPP thread finished");
	return 0;
}

int main(int argc, char **argv)
{
	struct instance inst;

	pthread_t fimc_thread;
	pthread_t ipp_thread;
	pthread_t mfc_thread;
	pthread_t parser_thread;
	int n,ret=0;

	printf("V4L2 Codec decoding example application\n");
	printf("Kamil Debski <k.debski@samsung.com>\n");
	printf("Copyright 2012-2015 Samsung Electronics Co., Ltd.\n\n");

	memzero(inst);
	if (parse_args(&inst, argc, argv)) {
		print_usage(argv[0]);
		return 1;
	}

	if (queue_init(&inst.fimc.queue, MFC_MAX_CAP_BUF))
		return 1;

	if (queue_init(&inst.ipp.queue, MFC_MAX_CAP_BUF))
		return 1;

	if (input_open(&inst, inst.in.name)) {
		cleanup(&inst);
		return 1;
	}

	if (inst.fb.enabled && fb_open(&inst, inst.fb.name)) {
		cleanup(&inst);
		return 1;
	}

	if (inst.fimc.enabled && fimc_open(&inst)) {
		cleanup(&inst);
		return 1;
	}

	if (mfc_open(&inst)) {
		cleanup(&inst);
		return 1;
	}

	dbg("Successfully opened all necessary files and devices");

	if (mfc_dec_setup_output(&inst, inst.parser.codec,
		STREAM_BUUFER_SIZE, STREAM_BUFFER_CNT)) {
		cleanup(&inst);
		return 1;
	}

	parse_stream_init(&inst.parser.ctx);

	if (extract_and_process_header(&inst)) {
		cleanup(&inst);
		return 1;
	}

	if (mfc_dec_setup_capture(&inst, RESULT_EXTRA_BUFFER_CNT)) {
		cleanup(&inst);
		return 1;
	}

	if (inst.drm.enabled && drm_open(&inst)) {
		cleanup(&inst);
		return 1;
	}

	if (inst.fimc.enabled && fimc_setup_output_from_mfc(&inst)) {
		cleanup(&inst);
		return 1;
	}
	if (inst.fimc.enabled && fimc_setup_capture(&inst)) {
		cleanup(&inst);
		return 1;
	}

	if (inst.fimc.enabled && fimc_set_crop(&inst, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
		inst.mfc.cap_crop_w, inst.mfc.cap_crop_h,
		inst.mfc.cap_crop_left, inst.mfc.cap_crop_top)) {
		cleanup(&inst);
		return 1;
	}

	dbg("I for one welcome our succesfully setup environment.");

	/* Since our fabulous V4L2 framework enforces that at least one buffer
	 * is queued before switching streaming on then we need to add the
	 * following code. Otherwise it could be ommited and it all would be
	 * handled by the mfc_thread.*/
	for (n = 0 ; n < inst.mfc.cap_buf_cnt; n++) {

		if (mfc_dec_queue_buf_cap(&inst, n)) {
			cleanup(&inst);
			return 1;
		}

		inst.mfc.cap_buf_flag[n] = BUF_MFC;
		inst.mfc.cap_buf_queued++;
	}
	if (inst.ipp.enabled && exynos_drm_ipp_allocate_ipp_buffers(&inst)) {
		cleanup(&inst);
		return 1;
	}

	if(ret)
		fprintf(stderr,"%s: %d",__func__,__LINE__);

	if (mfc_stream(&inst, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
							VIDIOC_STREAMON)) {
		cleanup(&inst);
		return 1;
	}
	sem_init(&inst.fimc.todo, 0, 0);
	sem_init(&inst.fimc.done, 0, 0);
	sem_init(&inst.ipp.todo, 0, 0);
	sem_init(&inst.ipp.done, 0, 0);
	/* Now we're safe to run the threads */
	dbg("Launching threads");

	if (pthread_create(&parser_thread, NULL, parser_thread_func, &inst)) {
		cleanup(&inst);
		return 1;
	}

	if (pthread_create(&mfc_thread, NULL, mfc_thread_func, &inst)) {
		cleanup(&inst);
		return 1;
	}

	if (inst.ipp.enabled &&  pthread_create(&ipp_thread, NULL, ipp_thread_func, &inst)) {
		cleanup(&inst);
		return 1;
	}
	else if (inst.fimc.enabled &&  pthread_create(&fimc_thread, NULL, fimc_thread_func, &inst)) {
		cleanup(&inst);
		return 1;
	}


	pthread_join(parser_thread, 0);
	pthread_join(mfc_thread, 0);
	if (inst.ipp.enabled)
		pthread_join(ipp_thread, 0);
	else if (inst.fimc.enabled)
		pthread_join(fimc_thread, 0);

	dbg("Threads have finished");

	cleanup(&inst);
	return 0;
}

