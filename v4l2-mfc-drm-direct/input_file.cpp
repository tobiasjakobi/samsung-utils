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

#include "input_file.h"
#include "main.h"

#include <iostream>

#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

InputFile::InputFile() : flags(0)
{
	// Nothing here.
}

InputFile::~InputFile()
{
	close();
}

bool InputFile::open(const std::string &name)
{
	static const std::string msg_prefix("InputFile::open(): ");

	if (flags & opened)
		return false;

	struct stat in_stat;

	fd = ::open(name.c_str(), O_RDONLY);
	if (fd < 0) {
		std::cerr << msg_prefix << "failed to open file: "
				  << name << ".\n";
		return false;
	}

	fstat(fd, &in_stat);
	size = in_stat.st_size;
	saved_offs = offs = 0;

	p = mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED) {
		std::cerr << msg_prefix << "failed to map input file.\n";
		return false;
	}

	flags |= opened;

	return true;
}

void InputFile::close()
{
	if (!(flags & opened))
		return;

	munmap(p, size);
	::close(fd);

	flags &= ~opened;
}

bool InputFile::is_open() const
{
	return (flags & opened);
}

void InputFile::save_pos()
{
	saved_offs = offs;
}

void InputFile::restore_pos()
{
	offs = saved_offs;
}

bool InputFile::read(void *dst, size_t sz, size_t o)
{
	if (!(flags & opened))
		return false;

	const size_t real_offset = offs + o;

	if (real_offset >= size)
		return false;

	memcpy(dst, static_cast<uint8_t*>(p) + real_offset, sz);

	return true;
}

uint8_t InputFile::read() const
{
	if (!(flags & opened) || (offs >= size))
		return 0x0;

	return *(static_cast<uint8_t*>(p) + offs);
}

void InputFile::advance(unsigned d)
{
	offs += d;
}

bool InputFile::eof() const
{
	if (!(flags & opened))
		return true;

	return (offs >= size);
}

void InputFile::rewind()
{
	offs = 0;
}
