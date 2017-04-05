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

#if !defined(__INPUT_FILE_)
#define __INPUT_FILE_

#include <string>

class InputFile {
private:
	enum flags {
		opened			= (1 << 0),
	};

	int fd;
	void *p;
	size_t size, offs, saved_offs;

	unsigned flags;

public:
	InputFile();
	~InputFile();

	// Open/close the input file.
	// open() returns false if an error occurs.
	bool open(const std::string &name);
	void close();

	// Returns true if associated with a file.
	bool is_open() const;

	// Save/restore the current file position.
	void save_pos();
	void restore_pos();

	// Read 'sz' bytes from file at the file position 'pos' into 'dst'.
	// 'pos' is computed as: pos = o + <current file position>
	// Returns false if an error occurs.
	bool read(void *dst, size_t sz, size_t o);

	// Get the next byte from the file.
	// Returns 0x0 if no file is open.
	uint8_t read() const;

	// Advance the current file position by 'd' bytes.
	void advance(unsigned d = 1);

	// Rewind the current file position to the
	// beginning of the file.
	void rewind();

	// Check for EOF state.
	bool eof() const;

};

#endif // __INPUT_FILE_
