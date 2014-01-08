 /*
  * test-jpeg.c
  *
  * Copyright 2011 - 2013 Samsung Electronics Co., Ltd.
  *
  * Licensed under the Apache License, Version 2.0 (the "License");
  * you may not use this file except in compliance with the License.
  * You may obtain a copy of the License atoi *
  * http://www.apache.org/licenses/LICENSE-2.0 *
  * Unless required by applicable law or agreed to in writing, Software
  * distributed under the License is distributed on an "AS IS" BASIS,
  * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  * See the License for the specific language governing permissions and
  * limitations under the License.
  *
  */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <errno.h>
#include <ctype.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>

#include <linux/fb.h>
#include <linux/videodev2.h>

#include <sys/mman.h>

#define VIDEO_DEV_NAME	"/dev/video"

#define perror_exit(cond, func)\
	if (cond) {\
		fprintf(stderr, "%s:%d: ", __func__, __LINE__);\
		perror(func);\
		exit(EXIT_FAILURE);\
	}

#define perror_ret(cond, func)\
	if (cond) {\
		fprintf(stderr, "%s:%d: ", __func__, __LINE__);\
		perror(func);\
		return ret;\
	}

#define memzero(x)\
	memset(&(x), 0, sizeof (x));


//#define PROCESS_DEBUG
#ifdef PROCESS_DEBUG
#define debug(msg, ...)\
	fprintf(stderr, "%s: \n" msg, __func__, ##__VA_ARGS__);
#else
#define debug(msg, ...)
#endif

#define ENCODE 0
#define DECODE 1

enum pix_format {
	FMT_JPEG,
        FMT_RGB565,
        FMT_RGB32,
        FMT_YUYV,
        FMT_YVYU,
        FMT_NV24,
        FMT_NV42,
        FMT_NV16,
        FMT_NV61,
        FMT_NV12,
        FMT_NV21,
        FMT_YUV420,
        FMT_GREY,
};

static int vid_fd;
static char *p_src_buf, *p_dst_buf;
static char *p_input_file;
static size_t src_buf_size, dst_buf_size;
static int input_file_sz, capture_buffer_sz = 0, compr_quality = 0, subsampling,
	   num_src_bufs, num_dst_bufs;

/* Command-line params */
int mode = ENCODE;
const char *input_filename;
const char *output_filename;
int video_node = 5;
int width = 0, height = 0;
int fourcc;

static __u32 get_px_format_by_id(enum pix_format px_fmt)
{
        switch (px_fmt) {
	case FMT_JPEG:
		return V4L2_PIX_FMT_JPEG;
	case FMT_RGB565:
		return V4L2_PIX_FMT_RGB565;
	case FMT_RGB32:
		return V4L2_PIX_FMT_RGB32;
	case FMT_YUYV:
		return V4L2_PIX_FMT_YUYV;
	case FMT_YVYU:
		return V4L2_PIX_FMT_YVYU;
	case FMT_NV24:
		return V4L2_PIX_FMT_NV24;
	case FMT_NV42:
		return V4L2_PIX_FMT_NV42;
	case FMT_NV16:
		return V4L2_PIX_FMT_NV16;
	case FMT_NV61:
		return V4L2_PIX_FMT_NV61;
	case FMT_NV12:
		return V4L2_PIX_FMT_NV12;
	case FMT_NV21:
		return V4L2_PIX_FMT_NV21;
	case FMT_YUV420:
		return V4L2_PIX_FMT_YUV420;
	case FMT_GREY:
		return V4L2_PIX_FMT_GREY;
        }

        return -EINVAL;
}

static void get_format_name_by_fourcc(unsigned int fourcc, char *fmt_name)
{
        switch (fourcc) {
	case V4L2_PIX_FMT_JPEG:
		strcpy(fmt_name, "V4L2_PIX_FMT_JPEG");
		break;
	case V4L2_PIX_FMT_RGB565:
		strcpy(fmt_name, "V4L2_PIX_FMT_RGB565");
		break;
	case V4L2_PIX_FMT_RGB32:
		strcpy(fmt_name, "V4L2_PIX_FMT_RGB32");
		break;
	case V4L2_PIX_FMT_YUYV:
		strcpy(fmt_name, "V4L2_PIX_FMT_YUYV");
		break;
	case V4L2_PIX_FMT_YVYU:
		strcpy(fmt_name, "V4L2_PIX_FMT_YVYU");
		break;
	case V4L2_PIX_FMT_NV24:
		strcpy(fmt_name, "V4L2_PIX_FMT_NV24");
		break;
	case V4L2_PIX_FMT_NV42:
		strcpy(fmt_name, "V4L2_PIX_FMT_NV42");
		break;
	case V4L2_PIX_FMT_NV16:
		strcpy(fmt_name, "V4L2_PIX_FMT_NV16");
		break;
	case V4L2_PIX_FMT_NV61:
		strcpy(fmt_name, "V4L2_PIX_FMT_NV61");
		break;
	case V4L2_PIX_FMT_NV12:
		strcpy(fmt_name, "V4L2_PIX_FMT_NV12");
		break;
	case V4L2_PIX_FMT_NV21:
		strcpy(fmt_name, "V4L2_PIX_FMT_NV21");
		break;
	case V4L2_PIX_FMT_YUV420:
		strcpy(fmt_name, "V4L2_PIX_FMT_YUV420");
		break;
	case V4L2_PIX_FMT_GREY:
		strcpy(fmt_name, "V4L2_PIX_FMT_GREY");
		break;
	default:
		strcpy(fmt_name, "UNKNOWN");
		break;
        }
}

static void get_subsampling_by_id(int subs, char *subs_name)
{
	switch (subs) {
	case V4L2_JPEG_CHROMA_SUBSAMPLING_444:
		strcpy(subs_name, "JPEG 4:4:4");
		break;
	case V4L2_JPEG_CHROMA_SUBSAMPLING_422:
		strcpy(subs_name, "JPEG 4:2:2");
		break;
	case V4L2_JPEG_CHROMA_SUBSAMPLING_420:
		strcpy(subs_name, "JPEG 4:2:0");
		break;
	case V4L2_JPEG_CHROMA_SUBSAMPLING_GRAY:
		strcpy(subs_name, "GRAY SCALE JPEG");
		break;
	default:
		sprintf(subs_name, "Unknown JPEG subsampling: %d", subs);
		break;
	}
}

static int dq_frame(void)
{
	struct v4l2_buffer buf;
	int ret;

	memzero(buf);

	buf.type	= V4L2_BUF_TYPE_VIDEO_OUTPUT;
	buf.memory	= V4L2_MEMORY_MMAP;
	ret = ioctl(vid_fd, VIDIOC_DQBUF, &buf);
	debug("Dequeued source buffer, index: %d\n", buf.index);
	if (ret) {
		switch (errno) {
		case EAGAIN:
			debug("Got EAGAIN\n");
			return 0;

		case EIO:
			debug("Got EIO\n");
			return 0;

		default:
			perror("ioctl");
			return 0;
		}
	}

	/* Verify we've got a correct buffer */
	assert(buf.index < num_src_bufs);

	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	ret = ioctl(vid_fd, VIDIOC_DQBUF, &buf);
	debug("Dequeued dst buffer, index: %d\n", buf.index);
	if (ret) {
		switch (errno) {
		case EAGAIN:
			debug("Got EAGAIN\n");
			return 0;

		case EIO:
			debug("Got EIO\n");
			return 0;

		default:
			perror("ioctl");
			return 1;
		}
	}

	/* Verify we've got a correct buffer */
	assert(buf.index < num_dst_bufs);

	capture_buffer_sz = buf.bytesused;
	return 0;
}

int init_input_file(void)
{
	struct stat stat;
	int fd;

	fd = open(input_filename, O_RDONLY);
	fstat(fd, &stat);
	input_file_sz = stat.st_size;
	printf("input_file size: %d\n", input_file_sz);

	p_input_file = mmap(NULL, input_file_sz, PROT_READ , MAP_SHARED, fd, 0);

	return fd;
}


void print_usage (void)
{
	fprintf (stderr, "Usage:\n"
		 "-m[MODE: 0 - ENCODE, 1 - DECODE]\n"
		 "-f[INPUT FILE NAME]\n"
		 "-o[OUTPUT FILE NAME]\n"
		 "-v[VIDEO NODE NUMBER]\n"
		 "-w[INPUT IMAGE WIDTH]\n"
		 "-h[INPUT IMAGE HEIGHT]\n"
		 "-r[COLOUR FORMAT: 1..12]\n"
		 "-c[COMPRESSION LEVEL: 0..3]\n"
		 "-p[CHROMA SUBSAMPLING: 0..3]\n");
}

static int parse_args(int argc, char *argv[])
{
	int c;

	opterr = 0;
	while ((c = getopt (argc, argv, "m:f:o:v:w:h:r:c:p:")) != -1) {
		debug("c: %c, optarg: %s\n", c, optarg);
		switch (c) {
		case 'm':
			mode = atoi(optarg);
			break;
		case 'f':
			input_filename = optarg;
			break;
		case 'o':
			output_filename = optarg;
			break;
		case 'v':
			video_node = atoi(optarg);
			break;
		case 'w':
			width = atoi(optarg);
			break;
		case 'h':
			height = atoi(optarg);
			break;
		case 'r':
			fourcc = atoi(optarg);
			break;
		case 'c':
			compr_quality = atoi(optarg);
			break;
		case 'p':
			subsampling = atoi(optarg);
			break;
		case '?':
			print_usage ();
			return -1;
		default:
			fprintf (stderr,
				 "Unknown option character `\\x%x'.\n",
				 optopt);
			return -1;
		}
	}

//	printf("mode: %d, input_filename: %s, video_node: %s%d, width: %d, height: %d, fourcc: %d, compr_quality: %d, subsampling: %d\n",
//		mode, input_filename, VIDEO_DEV_NAME, video_node, width, height, fourcc, compr_quality, subsampling);
	return 0;
}

int main(int argc, char *argv[])
{
	int i, r, input_fd;
	int ret = 0;
	struct v4l2_buffer buf;
	struct v4l2_requestbuffers reqbuf;
	enum v4l2_buf_type type;
	fd_set read_fds;
	char *def_outfile = "out.file";

	struct v4l2_capability cap;
	struct v4l2_format fmt;
	char video_node_name[20];

	ret = parse_args(argc, argv);
	if (ret < 0)
		return 0;

	input_fd = init_input_file();
	memzero(reqbuf);

	strcpy(video_node_name, VIDEO_DEV_NAME);
	sprintf(video_node_name + strlen(video_node_name), "%d", video_node);
	vid_fd = open(video_node_name, O_RDWR | O_NONBLOCK, 0);
	perror_exit(vid_fd < 0, "open");

	ret = ioctl(vid_fd, VIDIOC_QUERYCAP, &cap);
	perror_exit(ret != 0, "ioctl");

	if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
		fprintf(stderr, "Device does not support capture\n");
		exit(EXIT_FAILURE);
	}
	if (!(cap.capabilities & V4L2_CAP_VIDEO_OUTPUT)) {
		fprintf(stderr, "Device does not support output\n");
		exit(EXIT_FAILURE);
	}

	/* set input format */
	fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	fmt.fmt.pix.width	= width;
	fmt.fmt.pix.height	= height;
	fmt.fmt.pix.sizeimage	= input_file_sz;
	if (mode == ENCODE)
		fmt.fmt.pix.pixelformat = get_px_format_by_id(fourcc);
	else
		fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_JPEG;
	fmt.fmt.pix.field	= V4L2_FIELD_ANY;
	fmt.fmt.pix.bytesperline = 0;

	ret = ioctl(vid_fd, VIDIOC_S_FMT, &fmt);
	perror_exit(ret != 0, "ioctl");

	/* request input buffer */
	reqbuf.count	= 1;
	reqbuf.type	= V4L2_BUF_TYPE_VIDEO_OUTPUT;
	reqbuf.memory	= V4L2_MEMORY_MMAP;

	ret = ioctl(vid_fd, VIDIOC_REQBUFS, &reqbuf);
	perror_exit(ret != 0, "ioctl");
	num_src_bufs = reqbuf.count;

	/* query buffer parameters */
	buf.type	= V4L2_BUF_TYPE_VIDEO_OUTPUT;
	buf.memory	= V4L2_MEMORY_MMAP;
	buf.index	= 0;

	ret = ioctl(vid_fd, VIDIOC_QUERYBUF, &buf);
	perror_exit(ret != 0, "ioctl");

	src_buf_size = buf.length;

	/* mmap buffer */
	p_src_buf = mmap(NULL, buf.length,
			    PROT_READ | PROT_WRITE, MAP_SHARED,
			    vid_fd, buf.m.offset);
	perror_exit(MAP_FAILED == p_src_buf, "mmap");

	/* copy input file data to the buffer */
	memcpy(p_src_buf, p_input_file, input_file_sz);

	/* queue input buffer */
	memzero(buf);
	buf.type	= V4L2_BUF_TYPE_VIDEO_OUTPUT;
	buf.memory	= V4L2_MEMORY_MMAP;
	buf.index	= 0;

	ret = ioctl(vid_fd, VIDIOC_QBUF, &buf);
	perror_exit(ret != 0, "ioctl");

	type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	ret = ioctl(vid_fd, VIDIOC_STREAMON, &type);
	perror_exit(ret != 0, "ioctl");

	if (mode == DECODE) {
		/* get input JPEG dimensions */
		fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		ret = ioctl(vid_fd, VIDIOC_G_FMT, &fmt);
		perror_exit(ret != 0, "ioctl");
		width = fmt.fmt.pix.width;
		height = fmt.fmt.pix.height;
		printf("input JPEG dimensions: %dx%d\n", width, height);
	}

	/* set output format */
	fmt.type		= V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width	= width;
	fmt.fmt.pix.height	= height;
	fmt.fmt.pix.sizeimage	= width * height * 4;
	if (mode == DECODE)
		fmt.fmt.pix.pixelformat = get_px_format_by_id(fourcc);
	else
		fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_JPEG;
	fmt.fmt.pix.field	= V4L2_FIELD_ANY;

	char fmt_name[30];
	get_format_name_by_fourcc(fmt.fmt.pix.pixelformat, fmt_name);
	printf("setting output format: %s\n", fmt_name);

	ret = ioctl(vid_fd, VIDIOC_S_FMT, &fmt);
	perror_exit(ret != 0, "ioctl");

	get_format_name_by_fourcc(fmt.fmt.pix.pixelformat, fmt_name);
	printf("output format set: %s\n", fmt_name);
	printf("output image dimensions: %dx%d\n", fmt.fmt.pix.width, fmt.fmt.pix.height);

	if (mode == ENCODE) {
		struct v4l2_ext_controls ctrls;
		struct v4l2_ext_control ctrl[2];
		int ret;

		/* set compression quality */
		memzero(ctrl[0]);
		memzero(ctrl[1]);
		memzero(ctrls);

		ctrl[0].id = V4L2_CID_JPEG_COMPRESSION_QUALITY;
		ctrl[0].value = compr_quality;

		ctrl[1].id = V4L2_CID_JPEG_CHROMA_SUBSAMPLING;
		ctrl[1].value = subsampling;

		ctrls.ctrl_class = V4L2_CTRL_CLASS_JPEG;
		ctrls.count = 2;
		ctrls.controls = ctrl;

		ret = ioctl (vid_fd, VIDIOC_S_EXT_CTRLS, &ctrls);
		perror_exit (ret != 0, "VIDIOC_S_CTRL v4l2_ioctl");
	}

	/* request output buffer */
	reqbuf.count = 1;
	reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	ret = ioctl(vid_fd, VIDIOC_REQBUFS, &reqbuf);
	perror_exit(ret != 0, "ioctl");
	num_dst_bufs = reqbuf.count;

	/* query buffer parameters */
	buf.type	= V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory	= V4L2_MEMORY_MMAP;
	buf.index	= 0;

	ret = ioctl(vid_fd, VIDIOC_QUERYBUF, &buf);
	perror_exit(ret != 0, "ioctl");

	/* mmap buffer */
	p_dst_buf = mmap(NULL, buf.length,
			    PROT_READ | PROT_WRITE, MAP_SHARED,
			    vid_fd, buf.m.offset);
	perror_exit(MAP_FAILED == p_dst_buf, "mmap");

	memzero(buf);
	buf.type	= V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory	= V4L2_MEMORY_MMAP;
	buf.index	= 0;

	ret = ioctl(vid_fd, VIDIOC_QBUF, &buf);
	perror_exit(ret != 0, "ioctl");

	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	ret = ioctl(vid_fd, VIDIOC_STREAMON, &type);
	perror_exit(ret != 0, "ioctl");

	/* dequeue buffer */
	FD_ZERO(&read_fds);
	FD_SET(vid_fd, &read_fds);

	r = select(vid_fd + 1, &read_fds, NULL, NULL, 0);
	perror_exit(r < 0, "select");

	if (dq_frame()) {
		fprintf(stderr, "dequeue frame failed\n");
		goto done;
	}

	if (mode == DECODE) {
		struct v4l2_control ctrl;
		char subs_name[50];

		/* get input JPEG subsampling info */
		memzero(ctrl);
		ctrl.id = V4L2_CID_JPEG_CHROMA_SUBSAMPLING;

		ret = ioctl (vid_fd, VIDIOC_G_CTRL, &ctrl);
		perror_exit (ret != 0, "VIDIOC_G_CTRL v4l2_ioctl");

		get_subsampling_by_id(ctrl.value, subs_name);
		printf("decoded JPEG subsampling: %s\n", subs_name);
	}

	/* generate output file */
	if (output_filename == NULL)
		output_filename = def_outfile;

	int out_fd = open(output_filename, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);

	printf("Generating output file...\n");
	char x;
	for (i = 0; i < capture_buffer_sz; ++i) {
		x = p_dst_buf[i];
		write(out_fd, (const void * )&x, 1);
	}
	close(out_fd);

	printf("Output file: %s, size: %d\n", output_filename, capture_buffer_sz);

done:
	close(vid_fd);
	close(input_fd);
	munmap(p_src_buf, src_buf_size);
	munmap(p_dst_buf, dst_buf_size);
	munmap(p_input_file, input_file_sz);

	return ret;
}

