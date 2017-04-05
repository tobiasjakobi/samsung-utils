/*
 * Copyright (C) 2017 - Tobias Jakobi
 *
 * This is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation, either version 2 of the License,
 * or (at your option) any later version.
 *
 * It is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with it. If not, see <http://www.gnu.org/licenses/>.
 */

#include "parser.h"
#include "main.h"
#include "input_file.h"

#include <string>
#include <iostream>

#include <linux/videodev2.h>

namespace {

enum h264_parser_states {
	H264_PARSER_NO_CODE,
	H264_PARSER_CODE_0x1,
	H264_PARSER_CODE_0x2,
	H264_PARSER_CODE_0x3,
	H264_PARSER_CODE_1x1,
	H264_PARSER_CODE_SLICE,
};

// H264 recent tag type.
enum h264_tag_type {
	H264_TAG_HEAD,
	H264_TAG_SLICE,
};

enum mpeg4_parser_states {
	MPEG4_PARSER_NO_CODE,
	MPEG4_PARSER_CODE_0x1,
	MPEG4_PARSER_CODE_0x2,
	MPEG4_PARSER_CODE_1x1,
};

// MPEG4 recent tag type.
enum mpeg4_tag_type {
	MPEG4_TAG_HEAD,
	MPEG4_TAG_VOP,
};

}; // anonymous namespace


Parser::Parser(uint32_t c) : codec(c), flags(0)
{
	// Nothing here.
}

Parser::~Parser()
{
	unlink();
}

bool Parser::link(InputFile *in)
{
	if (flags & linked)
		return false;

	if (!in->is_open())
		return false;

	input = in;
	reset();

	flags |= linked;

	return true;
}

void Parser::unlink()
{
	flags &= ~linked;
}

bool Parser::reset()
{
	if (!(flags & linked))
		return false;

	state = 0;
	last_tag = 0;
	main_count = 0;
	headers_count = 0;
	tmp_code_start = 0;
	code_start = 0;
	code_end = 0;

	zerostruct(bytes, 6);
	input->rewind();

	return true;
}

bool Parser::is_linked() const
{
	return (flags & linked);
}

bool Parser::finished() const
{
	if (!(flags & linked))
		return true;

	return input->eof();
}

uint32_t Parser::get_codec() const
{
	return codec;
}

Parser* Parser::get_parser_from_codec(enum codecs c)
{
	Parser* p;

	switch (c) {
	case mpeg4:
		p = new MPEG4Parser(V4L2_PIX_FMT_MPEG4);
		break;

	case h264:
		p = new H264Parser(V4L2_PIX_FMT_H264);
		break;

	case h263:
		p = new MPEG4Parser(V4L2_PIX_FMT_H263);
		break;

	case xvid:
		p = new MPEG4Parser(V4L2_PIX_FMT_XVID);
		break;

	case mpeg2:
		p = new MPEG2Parser(V4L2_PIX_FMT_MPEG2);
		break;

	case mpeg1:
		p = new MPEG2Parser(V4L2_PIX_FMT_MPEG1);
		break;

	case vp8: // V4L2_PIX_FMT_VP8
	default:
		p = nullptr;
	}

	return p;
}

MPEG4Parser::MPEG4Parser(uint32_t c) : Parser(c)
{
	// Nothing here.
}

bool MPEG4Parser::parse(uint8_t* out, unsigned out_size, int &frame_size,
						bool& frame_finished, bool get_header)
{
	static const std::string msg_prefix("MPEG4Parser::parse(): ");

	if (!(flags & linked))
		return false;

	uint8_t tmp;
	int consumed = 0;

	frame_finished = false;

	input->save_pos();

	while (!input->eof()) {
		const uint8_t in = input->read();

		switch (state) {
		case MPEG4_PARSER_NO_CODE:
			if (in == 0x0) {
				state = MPEG4_PARSER_CODE_0x1;
				tmp_code_start = consumed;
			}
			break;

		case MPEG4_PARSER_CODE_0x1:
			state = (in == 0x0) ? MPEG4_PARSER_CODE_0x2 : MPEG4_PARSER_NO_CODE;
			break;

		case MPEG4_PARSER_CODE_0x2:
			if (in == 0x1) {
				state = MPEG4_PARSER_CODE_1x1;
			} else if ((in & 0xFC) == 0x80) {
				// Short header.
				state = MPEG4_PARSER_NO_CODE;

				// Ignore the short header if the current hasn't
				// been started with a short header.
				if (get_header && !(flags & short_header)) {
					last_tag = MPEG4_TAG_HEAD;
					headers_count++;
					flags |= short_header;
				} else if (!(flags & seek_end) ||
					((flags & seek_end) && (flags & short_header))) {
					last_tag = MPEG4_TAG_VOP;
					main_count++;
					flags |= short_header;
				}
			} else if (in == 0x0) {
				tmp_code_start++;
			} else {
				state = MPEG4_PARSER_NO_CODE;
			}
			break;

		case MPEG4_PARSER_CODE_1x1:
			tmp = in & 0xF0;
			if (tmp == 0x00 || tmp == 0x01 || tmp == 0x20 ||
				in == 0xB0 || in == 0xB2 || in == 0xB3 ||
				in == 0xB5) {
				state = MPEG4_PARSER_NO_CODE;
				last_tag = MPEG4_TAG_HEAD;
				headers_count++;
			} else if (in == 0xB6) {
				state = MPEG4_PARSER_NO_CODE;
				last_tag = MPEG4_TAG_VOP;
				main_count++;
			} else
				state = MPEG4_PARSER_NO_CODE;
			break;
		}

		if (get_header && headers_count >= 1 && main_count == 1) {
			code_end = tmp_code_start;
			flags |= got_end;
			break;
		}

		if (!(flags & got_start) && headers_count == 1 && main_count == 0) {
			code_start = tmp_code_start;
			flags |= got_start;
		}

		if (!(flags & got_start) && headers_count == 0 && main_count == 1) {
			code_start = tmp_code_start;
			flags |= got_start;
			flags |= seek_end;
			headers_count = 0;
			main_count = 0;
		}

		if (!(flags & seek_end) && headers_count > 0 && main_count == 1) {
			flags |= seek_end;
			headers_count = 0;
			main_count = 0;
		}

		if ((flags & seek_end) && (headers_count > 0 || main_count > 0)) {
			code_end = tmp_code_start;
			flags |= got_end;
			if (headers_count == 0)
				flags |= seek_end;
			else
				flags &= ~seek_end;
			break;
		}

		consumed++;
		input->advance();
	}

	frame_size = 0;

	int frame_length = (flags & got_end) ? code_end : consumed;
	size_t offset = 0;

	input->restore_pos();

	if (code_start >= 0) {
		frame_length -= code_start;
		offset = code_start;
	} else {
		memcpy(out, bytes, -code_start);
		frame_size += -code_start;
		out += -code_start;
		//in_size -= -code_start; // TODO: needed?
	}

	if (flags & got_start) {
		if (int(out_size) < frame_length) {
			std::cerr << msg_prefix << "output buffer too small for current frame.\n";
			return false;
		}

		input->read(out, frame_length, offset);
		frame_size += frame_length;

		if (flags & got_end) {
			code_start = code_end - consumed;
			flags |= got_start;
			flags &= ~got_end;
			frame_finished = true;
			if (last_tag == MPEG4_TAG_VOP) {
				flags |= seek_end;
				main_count = 0;
				headers_count = 0;
			} else {
				flags &= ~seek_end;
				main_count = 0;
				headers_count = 1;
				flags &= ~short_header;
				// If the last frame used the short then
				// we shall save this information, otherwise
				// it is necessary to clear it
			}

			input->read(bytes, consumed - code_end, code_end);
		} else {
			code_start = 0;
			frame_finished = false;
		}
	}

	tmp_code_start -= consumed;

	input->advance(consumed);

	return true;
}

H264Parser::H264Parser(uint32_t c) : Parser(c)
{
	// Nothing here.
}

bool H264Parser::parse(uint8_t* out, unsigned out_size, int &frame_size,
					   bool& frame_finished, bool get_header)
{
	static const std::string msg_prefix("H264Parser::parse(): ");

	uint8_t tmp;
	int consumed = 0;

	frame_finished = false;

	input->save_pos();

	while (!input->eof()) {
		const uint8_t in = input->read();

		switch (state) {
		case H264_PARSER_NO_CODE:
			if (in == 0x0) {
				state = H264_PARSER_CODE_0x1;
				tmp_code_start = consumed;
			}
			break;

		case H264_PARSER_CODE_0x1:
			state = (in == 0x0) ? H264_PARSER_CODE_0x2 : H264_PARSER_NO_CODE;
			break;

		case H264_PARSER_CODE_0x2:
			if (in == 0x1) {
				state = H264_PARSER_CODE_1x1;
			} else if (in == 0x0) {
				state = H264_PARSER_CODE_0x3;
			} else {
				state = H264_PARSER_NO_CODE;
			}
			break;

		case H264_PARSER_CODE_0x3:
			if (in == 0x1)
				state = H264_PARSER_CODE_1x1;
			else if (in == 0x0)
				tmp_code_start++;
			else
				state = H264_PARSER_NO_CODE;
			break;

		case H264_PARSER_CODE_1x1:
			tmp = in & 0x1F;

			if (tmp == 1 || tmp == 5) {
				state = H264_PARSER_CODE_SLICE;
			} else if (tmp == 6 || tmp == 7 || tmp == 8) {
				state = H264_PARSER_NO_CODE;
				last_tag = H264_TAG_HEAD;
				headers_count++;
			}
			else
				state = H264_PARSER_NO_CODE;
			break;

		case H264_PARSER_CODE_SLICE:
			if ((in & 0x80) == 0x80) {
				main_count++;
				last_tag = H264_TAG_SLICE;
			}
			state = H264_PARSER_NO_CODE;
			break;
		}

		if (get_header && headers_count >= 1 && main_count == 1) {
			code_end = tmp_code_start;
			flags |= got_end;
			break;
		}

		if (!(flags & got_start) && headers_count == 1 && main_count == 0) {
			code_start = tmp_code_start;
			flags |= got_start;
		}

		if (!(flags & got_start) && headers_count == 0 && main_count == 1) {
			code_start = tmp_code_start;
			flags |= got_start;
			flags |= seek_end;
			headers_count = 0;
			main_count = 0;
		}

		if (!(flags & seek_end) && headers_count > 0 && main_count == 1) {
			flags |= seek_end;
			headers_count = 0;
			main_count = 0;
		}

		if ((flags & seek_end) && (headers_count > 0 || main_count > 0)) {
			code_end = tmp_code_start;
			flags |= got_end;
			if (headers_count == 0)
				flags |= seek_end;
			else
				flags &= ~seek_end;
			break;
		}

		consumed++;
		input->advance();
	}

	frame_size = 0;

	int frame_length = (flags & got_end) ? code_end : consumed;
	size_t offset = 0;

	input->restore_pos();

	if (code_start >= 0) {
		frame_length -= code_start;
		offset = code_start;
	} else {
		memcpy(out, bytes, -code_start);
		frame_size += -code_start;
		out += -code_start;
		//in_size -= -code_start; // TODO: needed?
	}

	if (flags & got_start) {
		if (int(out_size) < frame_length) {
			std::cout << msg_prefix << "out_size: " << out_size << ", frame_length: "
					  << frame_length << ".\n";
			std::cerr << msg_prefix << "output buffer too small for current frame.\n";
			return false;
		}

		input->read(out, frame_length, offset);
		frame_size += frame_length;

		if (flags & got_end) {
			code_start = code_end - consumed;
			flags |= got_start;
			flags &= ~got_end;
			frame_finished = true;
			if (last_tag == H264_TAG_SLICE) {
				flags |= seek_end;
				main_count = 0;
				headers_count = 0;
			} else {
				flags &= ~seek_end;
				main_count = 0;
				headers_count = 1;
			}

			input->read(bytes, consumed - code_end, code_end);
		} else {
			code_start = 0;
			frame_finished = false;
		}
	}

	tmp_code_start -= consumed;

	input->advance(consumed);

	return frame_finished;
}

MPEG2Parser::MPEG2Parser(uint32_t c) : Parser(c)
{
	// Nothing here.
}

bool MPEG2Parser::parse(uint8_t* out, unsigned out_size, int &frame_size,
						bool& frame_finished, bool get_header)
{
	static const std::string msg_prefix("H264Parser::parse_stream(): ");

	int consumed = 0;

	frame_finished = false;

	input->save_pos();

	while (!input->eof()) {
		const uint8_t in = input->read();

		switch (state) {
		case MPEG4_PARSER_NO_CODE:
			if (in == 0x0) {
				state = MPEG4_PARSER_CODE_0x1;
				tmp_code_start = consumed;
			}
			break;

		case MPEG4_PARSER_CODE_0x1:
			state = (in == 0x0) ? MPEG4_PARSER_CODE_0x2 : MPEG4_PARSER_NO_CODE;
			break;

		case MPEG4_PARSER_CODE_0x2:
			if (in == 0x1) {
				state = MPEG4_PARSER_CODE_1x1;
			} else if (in == 0x0) {
				// We still have two zeroes.
				tmp_code_start++;
				// TODO: XXX check in h264 and MPEG4
			} else {
				state = MPEG4_PARSER_NO_CODE;
			}
			break;

		case MPEG4_PARSER_CODE_1x1:
			if (in == 0xb3 || in == 0xb8) {
				state = MPEG4_PARSER_NO_CODE;
				last_tag = MPEG4_TAG_HEAD;
				headers_count++;
				std::cout << msg_prefix << "found header at 0x" << std::dec
						  << consumed << ".\n";
			} else if (in == 0x00) {
				state = MPEG4_PARSER_NO_CODE;
				last_tag = MPEG4_TAG_VOP;
				main_count++;
				std::cout << msg_prefix << "found picture at 0x" << std::dec
						  << consumed << ".\n";
			} else
				state = MPEG4_PARSER_NO_CODE;
			break;
		}

		if (get_header && headers_count >= 1 && main_count == 1) {
			code_end = tmp_code_start;
			flags |= got_end;
			break;
		}

		if (!(flags & got_start) && headers_count == 1 && main_count == 0) {
			code_start = tmp_code_start;
			flags |= got_start;
		}

		if (!(flags & got_start) && headers_count == 0 && main_count == 1) {
			code_start = tmp_code_start;
			flags |= got_start;
			flags |= seek_end;
			headers_count = 0;
			main_count = 0;
		}

		if (!(flags & seek_end) && headers_count > 0 && main_count == 1) {
			flags |= seek_end;
			headers_count = 0;
			main_count = 0;
		}

		if ((flags & seek_end) && (headers_count > 0 || main_count > 0)) {
			code_end = tmp_code_start;
			flags |= got_end;
			if (headers_count == 0)
				flags |= seek_end;
			else
				flags &= ~seek_end;
			break;
		}

		input->advance();
		consumed++;
	}

	input->restore_pos();

	frame_size = 0;

	int frame_length = (flags & got_end) ? code_end : consumed;
	size_t offset = 0;

	if (code_start >= 0) {
		frame_length -= code_start;
		offset = code_start;
	} else {
		memcpy(out, bytes, -code_start);
		frame_size += -code_start;
		out += -code_start;
		//in_size -= -code_start; // TODO: needed?
	}

	if (flags & got_start) {
		if (int(out_size) < frame_length) {
			std::cerr << msg_prefix << "output buffer too small for current frame.\n";
			return false;
		}

		input->read(out, frame_length, offset);
		frame_size += frame_length;

		if (flags & got_end) {
			code_start = code_end - consumed;
			flags |= got_start;
			flags &= ~got_end;
			frame_finished = true;
			if (last_tag == MPEG4_TAG_VOP) {
				flags |= seek_end;
				main_count = 0;
				headers_count = 0;
			} else {
				flags &= ~seek_end;
				main_count = 0;
				headers_count = 1;
			}

			input->read(bytes, consumed - code_end, code_end);
		} else {
			code_start = 0;
			frame_finished = false;
		}
	}

	tmp_code_start -= consumed;

	input->advance(consumed);

	return true;
}

