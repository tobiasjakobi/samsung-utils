/*
 * V4L2 Codec decoding example application
 * Kamil Debski <k.debski@samsung.com>
 *
 * Really simple stream parser file
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

#include "common.h"
#include "parser.h"
#include <string.h>

int parse_stream_init(struct mfc_parser_context *ctx)
{
	if (!ctx) {
		err("ctx is NULL");
		return -1;
	}
	memzero(*ctx);
	return 0;
}

int parse_mpeg4_stream(
	struct mfc_parser_context *ctx,
	char* in, int in_size, char* out, int out_size,
	int *consumed, int *frame_size, char get_head)
{
	char *in_orig;
	char tmp;
	char frame_finished;
	int frame_length;

	in_orig = in;

	*consumed = 0;

	frame_finished = 0;

	while (in_size-- > 0) {
		switch (ctx->state) {
		case MPEG4_PARSER_NO_CODE:
			if (*in == 0x0) {
				ctx->state = MPEG4_PARSER_CODE_0x1;
				ctx->tmp_code_start = *consumed;
			}
			break;
		case MPEG4_PARSER_CODE_0x1:
			if (*in == 0x0)
				ctx->state = MPEG4_PARSER_CODE_0x2;
			else
				ctx->state = MPEG4_PARSER_NO_CODE;
			break;
		case MPEG4_PARSER_CODE_0x2:
			if (*in == 0x1) {
				ctx->state = MPEG4_PARSER_CODE_1x1;
			} else if ((*in & 0xFC) == 0x80) {
				/* Short header */
				ctx->state = MPEG4_PARSER_NO_CODE;
				/* Ignore the short header if the current hasn't
				 * been started with a short header. */

				if (get_head && !ctx->short_header) {
					ctx->last_tag = MPEG4_TAG_HEAD;
					ctx->headers_count++;
					ctx->short_header = 1;
				} else if (!ctx->seek_end ||
					(ctx->seek_end && ctx->short_header)) {
					ctx->last_tag = MPEG4_TAG_VOP;
					ctx->main_count++;
					ctx->short_header = 1;
				}
			} else if (*in == 0x0) {
				ctx->tmp_code_start++;
			} else {
				ctx->state = MPEG4_PARSER_NO_CODE;
			}
			break;
		case MPEG4_PARSER_CODE_1x1:
			tmp = *in & 0xF0;
			if (tmp == 0x00 || tmp == 0x01 || tmp == 0x20 ||
				*in == 0xb0 || *in == 0xb2 || *in == 0xb3 ||
				*in == 0xb5) {
				ctx->state = MPEG4_PARSER_NO_CODE;
				ctx->last_tag = MPEG4_TAG_HEAD;
				ctx->headers_count++;
			} else if (*in == 0xb6) {
				ctx->state = MPEG4_PARSER_NO_CODE;
				ctx->last_tag = MPEG4_TAG_VOP;
				ctx->main_count++;
			} else
				ctx->state = MPEG4_PARSER_NO_CODE;
			break;
		}

		if (get_head == 1 && ctx->headers_count >= 1 && ctx->main_count == 1) {
			ctx->code_end = ctx->tmp_code_start;
			ctx->got_end = 1;
			break;
		}

		if (ctx->got_start == 0 && ctx->headers_count == 1 && ctx->main_count == 0) {
			ctx->code_start = ctx->tmp_code_start;
			ctx->got_start = 1;
		}

		if (ctx->got_start == 0 && ctx->headers_count == 0 && ctx->main_count == 1) {
			ctx->code_start = ctx->tmp_code_start;
			ctx->got_start = 1;
			ctx->seek_end = 1;
			ctx->headers_count = 0;
			ctx->main_count = 0;
		}

		if (ctx->seek_end == 0 && ctx->headers_count > 0 && ctx->main_count == 1) {
			ctx->seek_end = 1;
			ctx->headers_count = 0;
			ctx->main_count = 0;
		}

		if (ctx->seek_end == 1 && (ctx->headers_count > 0 || ctx->main_count > 0)) {
			ctx->code_end = ctx->tmp_code_start;
			ctx->got_end = 1;
			if (ctx->headers_count == 0)
				ctx->seek_end = 1;
			else
				ctx->seek_end = 0;
			break;
		}

		in++;
		(*consumed)++;
	}


	*frame_size = 0;

	if (ctx->got_end == 1) {
		frame_length = ctx->code_end;
	} else
		frame_length = *consumed;


	if (ctx->code_start >= 0) {
		frame_length -= ctx->code_start;
		in = in_orig + ctx->code_start;
	} else {
		memcpy(out, ctx->bytes, -ctx->code_start);
		*frame_size += -ctx->code_start;
		out += -ctx->code_start;
		in_size -= -ctx->code_start;
		in = in_orig;
	}

	if (ctx->got_start) {
		if (out_size < frame_length) {
			err("Output buffer too small for current frame");
			return 0;
		}

		memcpy(out, in, frame_length);
		*frame_size += frame_length;

		if (ctx->got_end) {
			ctx->code_start = ctx->code_end - *consumed;
			ctx->got_start = 1;
			ctx->got_end = 0;
			frame_finished = 1;
			if (ctx->last_tag == MPEG4_TAG_VOP) {
				ctx->seek_end = 1;
				ctx->main_count = 0;
				ctx->headers_count = 0;
			} else {
				ctx->seek_end = 0;
				ctx->main_count = 0;
				ctx->headers_count = 1;
				ctx->short_header = 0;
				/* If the last frame used the short then
				 * we shall save this information, otherwise
				 * it is necessary to clear it */
			}
			memcpy(ctx->bytes, in_orig + ctx->code_end, *consumed - ctx->code_end);
		} else {
			ctx->code_start = 0;
			frame_finished = 0;
		}
	}

	ctx->tmp_code_start -= *consumed;

	return frame_finished;
}

int parse_h264_stream(
	struct mfc_parser_context *ctx,
	char* in, int in_size, char* out, int out_size,
	int *consumed, int *frame_size, char get_head)
{
	char *in_orig;
	char tmp;
	char frame_finished;
	int frame_length;

	in_orig = in;

	*consumed = 0;

	frame_finished = 0;

	while (in_size-- > 0) {
		switch (ctx->state) {
		case H264_PARSER_NO_CODE:
			if (*in == 0x0) {
				ctx->state = H264_PARSER_CODE_0x1;
				ctx->tmp_code_start = *consumed;
			}
			break;
		case H264_PARSER_CODE_0x1:
			if (*in == 0x0)
				ctx->state = H264_PARSER_CODE_0x2;
			else
				ctx->state = H264_PARSER_NO_CODE;
			break;
		case H264_PARSER_CODE_0x2:
			if (*in == 0x1) {
				ctx->state = H264_PARSER_CODE_1x1;
			} else if (*in == 0x0) {
				ctx->state = H264_PARSER_CODE_0x3;
			} else {
				ctx->state = H264_PARSER_NO_CODE;
			}
			break;
		case H264_PARSER_CODE_0x3:
			if (*in == 0x1)
				ctx->state = H264_PARSER_CODE_1x1;
			else if (*in == 0x0)
				ctx->tmp_code_start++;
			else
				ctx->state = H264_PARSER_NO_CODE;
			break;
		case H264_PARSER_CODE_1x1:
			tmp = *in & 0x1F;

			if (tmp == 1 || tmp == 5) {
				ctx->state = H264_PARSER_CODE_SLICE;
			} else if (tmp == 6 || tmp == 7 || tmp == 8) {
				ctx->state = H264_PARSER_NO_CODE;
				ctx->last_tag = H264_TAG_HEAD;
				ctx->headers_count++;
			}
			else
				ctx->state = H264_PARSER_NO_CODE;
			break;
		case H264_PARSER_CODE_SLICE:
			if ((*in & 0x80) == 0x80) {
				ctx->main_count++;
				ctx->last_tag = H264_TAG_SLICE;
			}
			ctx->state = H264_PARSER_NO_CODE;
			break;
		}

		if (get_head == 1 && ctx->headers_count >= 1 && ctx->main_count == 1) {
			ctx->code_end = ctx->tmp_code_start;
			ctx->got_end = 1;
			break;
		}

		if (ctx->got_start == 0 && ctx->headers_count == 1 && ctx->main_count == 0) {
			ctx->code_start = ctx->tmp_code_start;
			ctx->got_start = 1;
		}

		if (ctx->got_start == 0 && ctx->headers_count == 0 && ctx->main_count == 1) {
			ctx->code_start = ctx->tmp_code_start;
			ctx->got_start = 1;
			ctx->seek_end = 1;
			ctx->headers_count = 0;
			ctx->main_count = 0;
		}

		if (ctx->seek_end == 0 && ctx->headers_count > 0 && ctx->main_count == 1) {
			ctx->seek_end = 1;
			ctx->headers_count = 0;
			ctx->main_count = 0;
		}

		if (ctx->seek_end == 1 && (ctx->headers_count > 0 || ctx->main_count > 0)) {
			ctx->code_end = ctx->tmp_code_start;
			ctx->got_end = 1;
			if (ctx->headers_count == 0)
				ctx->seek_end = 1;
			else
				ctx->seek_end = 0;
			break;
		}

		in++;
		(*consumed)++;
	}


	*frame_size = 0;

	if (ctx->got_end == 1) {
		frame_length = ctx->code_end;
	} else
		frame_length = *consumed;


	if (ctx->code_start >= 0) {
		frame_length -= ctx->code_start;
		in = in_orig + ctx->code_start;
	} else {
		memcpy(out, ctx->bytes, -ctx->code_start);
		*frame_size += -ctx->code_start;
		out += -ctx->code_start;
		in_size -= -ctx->code_start;
		in = in_orig;
	}

	if (ctx->got_start) {
		if (out_size < frame_length) {
			printf("out_size: %i frame_length: %i\n", out_size, frame_length);
			err("Output buffer too small for current frame");
			return 0;
		}
		memcpy(out, in, frame_length);
		*frame_size += frame_length;

		if (ctx->got_end) {
			ctx->code_start = ctx->code_end - *consumed;
			ctx->got_start = 1;
			ctx->got_end = 0;
			frame_finished = 1;
			if (ctx->last_tag == H264_TAG_SLICE) {
				ctx->seek_end = 1;
				ctx->main_count = 0;
				ctx->headers_count = 0;
			} else {
				ctx->seek_end = 0;
				ctx->main_count = 0;
				ctx->headers_count = 1;
			}
			memcpy(ctx->bytes, in_orig + ctx->code_end, *consumed - ctx->code_end);
		} else {
			ctx->code_start = 0;
			frame_finished = 0;
		}
	}

	ctx->tmp_code_start -= *consumed;

	return frame_finished;
}

int parse_mpeg2_stream(
	struct mfc_parser_context *ctx,
	char* in, int in_size, char* out, int out_size,
	int *consumed, int *frame_size, char get_head)
{
	char *in_orig;
	char frame_finished;
	int frame_length;

	in_orig = in;

	*consumed = 0;

	frame_finished = 0;

	while (in_size-- > 0) {
		switch (ctx->state) {
		case MPEG4_PARSER_NO_CODE:
			if (*in == 0x0) {
				ctx->state = MPEG4_PARSER_CODE_0x1;
				ctx->tmp_code_start = *consumed;
			}
			break;
		case MPEG4_PARSER_CODE_0x1:
			if (*in == 0x0)
				ctx->state = MPEG4_PARSER_CODE_0x2;
			else
				ctx->state = MPEG4_PARSER_NO_CODE;
			break;
		case MPEG4_PARSER_CODE_0x2:
			if (*in == 0x1) {
				ctx->state = MPEG4_PARSER_CODE_1x1;
			} else if (*in == 0x0) {
				/* We still have two zeroes */
				ctx->tmp_code_start++;
				// TODO XXX check in h264 and mpeg4
			} else {
				ctx->state = MPEG4_PARSER_NO_CODE;
			}
			break;
		case MPEG4_PARSER_CODE_1x1:
			if (*in == 0xb3 || *in == 0xb8) {
				ctx->state = MPEG4_PARSER_NO_CODE;
				ctx->last_tag = MPEG4_TAG_HEAD;
				ctx->headers_count++;
				dbg("Found header at %d (%x)", *consumed, *consumed);
			} else if (*in == 0x00) {
				ctx->state = MPEG4_PARSER_NO_CODE;
				ctx->last_tag = MPEG4_TAG_VOP;
				ctx->main_count++;
				dbg("Found picture at %d (%x)", *consumed, *consumed);
			} else
				ctx->state = MPEG4_PARSER_NO_CODE;
			break;
		}

		if (get_head == 1 && ctx->headers_count >= 1 && ctx->main_count == 1) {
			ctx->code_end = ctx->tmp_code_start;
			ctx->got_end = 1;
			break;
		}

		if (ctx->got_start == 0 && ctx->headers_count == 1 && ctx->main_count == 0) {
			ctx->code_start = ctx->tmp_code_start;
			ctx->got_start = 1;
		}

		if (ctx->got_start == 0 && ctx->headers_count == 0 && ctx->main_count == 1) {
			ctx->code_start = ctx->tmp_code_start;
			ctx->got_start = 1;
			ctx->seek_end = 1;
			ctx->headers_count = 0;
			ctx->main_count = 0;
		}

		if (ctx->seek_end == 0 && ctx->headers_count > 0 && ctx->main_count == 1) {
			ctx->seek_end = 1;
			ctx->headers_count = 0;
			ctx->main_count = 0;
		}

		if (ctx->seek_end == 1 && (ctx->headers_count > 0 || ctx->main_count > 0)) {
			ctx->code_end = ctx->tmp_code_start;
			ctx->got_end = 1;
			if (ctx->headers_count == 0)
				ctx->seek_end = 1;
			else
				ctx->seek_end = 0;
			break;
		}

		in++;
		(*consumed)++;
	}

	*frame_size = 0;

	if (ctx->got_end == 1) {
		frame_length = ctx->code_end;
	} else
		frame_length = *consumed;


	if (ctx->code_start >= 0) {
		frame_length -= ctx->code_start;
		in = in_orig + ctx->code_start;
	} else {
		memcpy(out, ctx->bytes, -ctx->code_start);
		*frame_size += -ctx->code_start;
		out += -ctx->code_start;
		in_size -= -ctx->code_start;
		in = in_orig;
	}

	if (ctx->got_start) {
		if (out_size < frame_length) {
			err("Output buffer too small for current frame");
			return 0;
		}

		memcpy(out, in, frame_length);
		*frame_size += frame_length;

		if (ctx->got_end) {
			ctx->code_start = ctx->code_end - *consumed;
			ctx->got_start = 1;
			ctx->got_end = 0;
			frame_finished = 1;
			if (ctx->last_tag == MPEG4_TAG_VOP) {
				ctx->seek_end = 1;
				ctx->main_count = 0;
				ctx->headers_count = 0;
			} else {
				ctx->seek_end = 0;
				ctx->main_count = 0;
				ctx->headers_count = 1;
			}
			memcpy(ctx->bytes, in_orig + ctx->code_end, *consumed - ctx->code_end);
		} else {
			ctx->code_start = 0;
			frame_finished = 0;
		}
	}

	ctx->tmp_code_start -= *consumed;

	return frame_finished;
}

// IVF is a simple file format that transports raw VP8 data.
int parse_vp8_stream (
	struct mfc_parser_context *ctx,
	char* in, int in_size, char* out, int out_size,
	int *consumed, int *frame_size, char get_head)
{
    unsigned int index = 0;
    unsigned int framesize;

    if ((in[index] == 0x44) && (in[index + 1] == 0x4B) && (in[index + 2] == 0x49) && (in[index + 3] == 0x46))   // 444B4946 : DKIF
    {
        // IVF header
        index += 4; //bytes 0-3    signature: 'DKIF'
        index += 2; // plus bytes 4-5    version (should be 0)
        index += 2; // plus bytes 6-7    length of header in bytes
        index += 4; // plus bytes 8-11   codec FourCC (e.g., 'VP80')
        index += 2; // plus bytes 12-13  width in pixels
        index += 2; // plus bytes 14-15  height in pixels
        index += 4; // plus bytes 16-19  frame rate
        index += 4; // plus bytes 20-23  time scale
        index += 4; // plus bytes 24-27  number of frames in file
        index += 4; // plus bytes 28-31  unused
    }
    framesize = (in[index + 2] << 16) | (in[index + 1] << 8) | in[index];
    index += 4; //bytes 0-3    size of frame in bytes (not including the 12-byte header)
    index += 8; // plus bytes 4-11   64-bit presentation timestamp

    memcpy(out, in + index, framesize);
    *frame_size = framesize;
    if(get_head==1)
	*consumed =0;
    else
	*consumed = index+framesize;
    printf(" frame_size = %d, consumed = %d\n",*frame_size, *consumed);

    return 1;
}
