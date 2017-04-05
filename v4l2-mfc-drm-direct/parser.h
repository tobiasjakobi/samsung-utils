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

#if !defined(__PARSER_)
#define __PARSER_

#include <cstdint>

// Forward-declarations
class InputFile;

class Parser {
protected:
	enum flags {
		linked			= (1 << 0),

		got_start		= (1 << 1),
		got_end			= (1 << 2),
		seek_end		= (1 << 3),
		short_header	= (1 << 4),
	};

	InputFile *input;
	uint32_t codec;

	unsigned state;
	unsigned last_tag;
	unsigned main_count;
	unsigned headers_count;
	int tmp_code_start;
	int code_start;
	int code_end;
	uint8_t bytes[6];

	unsigned flags;

public:
	enum codecs {
		mpeg4,
		h264,
		h263,
		xvid,
		mpeg2,
		mpeg1,
		vp8,
	};

	Parser(uint32_t c);
	virtual ~Parser();

	Parser(const Parser &p) = delete;

	// Link/unlink the parser to/from an input file.
	// link() returns false if an error occurs.
	bool link(InputFile *in);
	void unlink();

	// Reset the parser's internal state and rewind
	// the linked input file.
	bool reset();

	// Returns true if the parser is linked to an input file.
	bool is_linked() const;

	// Returns true if the whole file was parsed.
	bool finished() const;

	// Check V4L2 codec type.
	uint32_t get_codec() const;

	// Parse and write resulting output into 'out'.
	// Returns false if an error occurs.
	virtual bool parse(uint8_t* out, unsigned out_size, int &frame_size,
					   bool& frame_finished, bool get_header) = 0;

	// Construct a parser from a codec enum.
	static Parser* get_parser_from_codec(enum codecs c);

};

class MPEG4Parser : public Parser {
public:
	MPEG4Parser(uint32_t c);

	bool parse(uint8_t* out, unsigned out_size, int &frame_size,
			   bool& frame_finished, bool get_header);

};

class H264Parser : public Parser {
public:
	H264Parser(uint32_t c);

	bool parse(uint8_t* out, unsigned out_size, int &frame_size,
			   bool& frame_finished, bool get_header);

};

class MPEG2Parser : public Parser {
public:
	MPEG2Parser(uint32_t c);

	bool parse(uint8_t* out, unsigned out_size, int &frame_size,
			   bool& frame_finished, bool get_header);

};

#endif // __PARSER_
