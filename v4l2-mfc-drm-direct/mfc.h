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

#if !defined(__MFC_DECODER_)
#define __MFC_DECODER_

#include <cstdint>
#include <vector>
#include <map>

// Forward-declarations
class Parser;
class ExynosBuffer;
class ExynosPage;
class QueueHandler;
struct videoinfo;


class MFCDecoder {
private:
	// The MFC decoder reads from a V4L2 output buffer, and writes its results
	// to a V4L2 capture buffer. We just denote output buffers as 'source', and
	// capture buffers as 'destination'.

	struct buffer {
		uint8_t *addr;
		unsigned index;
		int fd;
		unsigned flags;
	};

	int fd;
	Parser *parser;
	QueueHandler *qh;

	std::vector<buffer> source_buffers;
	unsigned source_buffer_size;

	std::vector<ExynosPage*> dest_buffers;
	unsigned dest_buffer_count;
	unsigned dest_plane_size[4];
	unsigned dest_queue_min;
	unsigned dest_num_queued;

	unsigned flags;

public:
	enum buffer_type {
		source = 0,
		destination,
	};

	enum run_state {
		run_active = 0,
		run_finished,
		run_nop,
		run_error
	};

	MFCDecoder();
	~MFCDecoder();

	MFCDecoder(const MFCDecoder &dec) = delete;

	// Order of operations:
	// open(), set_parser(), set_source(), init()
	// Any other order is going to result in an error.

	// Open/close the MFC decoder.
	// open() returns false if an error occurs.
	bool open();
	void close();

	// Set/unset the parser of the MFC decoder.
	// set_parser() returns false if an error occurs.
	bool set_parser(Parser *p);
	void unset_parser();

	// Set/unset source buffers for the MFC decoder.
	// These buffers are filled by the parser and
	// are then passed to the decoder.
	// set_input() returns false if an error occurs.
	bool set_source(std::vector<ExynosBuffer> &buffers);
	void unset_source();

	// Initialize/deinitialize the MFC decoder.
	// This does the decoding destination setup. The destination buffers
	// are filled filled with the decoded frames coming from the decoder.
	//
	// @num_buffers: number of required destination buffers
	// @vi: reference to a struct filled with video information
	bool init(unsigned &num_buffers, videoinfo &vi);
	void deinit();

	// TODO: docu
	bool ready() const;
	enum run_state run();

	// TODO: docu
	bool queue_dest(ExynosPage *page);
	ExynosPage* dequeue_dest();

private:
	bool set_source_v4l2();
	bool set_dest_v4l2(videoinfo &vi);

	bool is_src_busy() const;

	bool qsrc(unsigned index, unsigned frame_size);
	bool qdst(unsigned index, int dma_fd);

	bool dqsrc(unsigned &index);
	bool dqdst(unsigned &index, bool &finished);

	bool stream(enum buffer_type type, bool enable);

	bool stop();
};

#endif // __MFC_DECODER_
