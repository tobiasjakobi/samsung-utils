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

#include "main.h"
#include "mfc.h"
#include "exynos_drm.h"
#include "parser.h"
#include "input_file.h"

#include <iostream>
#include <thread>
#include <atomic>

#include <linux/videodev2.h>

enum common_constants {
	pages_count = 3,

	// This is the size of the buffer for the compressed stream.
	// It limits the maximum compressed frame size.
	input_buffer_size = 1024 * 1024,

	// The number of compressed stream buffers
	input_buffer_count = 2,
};

enum state_flags {
	finished	= (1 << 0),
	error		= (1 << 1),
};

struct thread_data {
	ExynosDRM *drm;
	MFCDecoder *mfcdec;

	std::atomic<unsigned> decoding_state;
};

int presentation_thread(thread_data *data)
{
	while (true) {
		if (data->decoding_state.load() & (finished | error))
			break;

		ExynosPage *p;

		p = data->mfcdec->dequeue_dest();
		if (!p) {
			std::cerr << "DEBUG: dequeue failed.\n";
			data->decoding_state.fetch_or(error);
			break;
		}

		if (!data->drm->issue_flip(p)) {
			std::cerr << "DEBUG: flip failed.\n";
			data->decoding_state.fetch_or(error);
			break;
		}

		p = nullptr;

		while (!p) {
			p = data->drm->get_page();

			if (!p)
				data->drm->wait_for_flip();
		}

		if (!data->mfcdec->queue_dest(p)) {
			std::cerr << "DEBUG: queue failed.\n";
			data->decoding_state.fetch_or(error);
			break;
		}
	}

	if (data->decoding_state.load() & finished) {
		// TODO: drain
	}

	return 0;
}

int main(int argc, char* argv[]) {
	using namespace std;

	InputFile *input;
	ExynosDRM *drm;
	MFCDecoder *mfcdec;
	Parser *parser;
	std::thread *pres;

	std::vector<ExynosBuffer> input_buffers;

	try {
		input = new InputFile;
		drm = new ExynosDRM;
		mfcdec = new MFCDecoder;
		parser = Parser::get_parser_from_codec(Parser::h264);

		if (!input->open("/dev/shm/test.h264"))
			throw exception();
		if (!parser->link(input))
			throw exception();

		// TODO: parse resolution from command line
		if (!drm->open(ExynosDRM::connector_hdmi))
			throw exception();
		if (!drm->init(1920, 1080))
			throw exception();
		if (!drm->alloc_buffers(input_buffer_count, input_buffer_size, input_buffers))
			throw exception();

		videoinfo vi;
		unsigned num_pages;

		if (!mfcdec->open())
			throw exception();
		if (!mfcdec->set_parser(parser))
			throw exception();
		if (!mfcdec->set_source(input_buffers))
			throw exception();
		if (!mfcdec->init(num_pages, vi))
			throw exception();

		if (!drm->alloc_pages(num_pages, vi))
			throw exception();

		// MFC needs some destination buffers queued, before it can begin
		// operation. Queue these buffers here.
		while (!mfcdec->ready()) {
			if (!mfcdec->queue_dest(drm->get_page()))
				throw exception();
		}

		// First page that we dequeue in the presentation thread.
		if (!mfcdec->queue_dest(drm->get_page()))
			throw exception();
	}
	catch (exception &e) {
		cerr << "initialization failed.\n";

		delete mfcdec;
		delete drm;
		delete parser;
		delete input;

		return 1;
	}

	thread_data td;
	td.drm = drm;
	td.mfcdec = mfcdec;
	td.decoding_state.store(0);

	pres = new std::thread(presentation_thread, &td);

	while (true) {
		if (td.decoding_state.load() & (finished | error))
			break;

		switch (mfcdec->run()) {
		case MFCDecoder::run_active:
			break;

		case MFCDecoder::run_finished:
			td.decoding_state.fetch_or(finished);
			break;

		case MFCDecoder::run_nop:
			// TODO
			break;

		case MFCDecoder::run_error:
			std::cerr << "DEBUG: run() failed.\n";
			td.decoding_state.fetch_or(error);
			break;
		}
	}

	pres->join();

	delete mfcdec;
	delete drm;
	delete parser;
	delete input;

	return 0;
}
