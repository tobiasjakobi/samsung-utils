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

#if !defined(__MAIN_)
#define __MAIN_

#include <cstring>
#include <cstdint>

template <typename T>
inline void
zerostruct(T* t, unsigned num = 1)
{
	std::memset(t, 0, sizeof(T) * num);
}

// video information struct
//
// @{w,h}: total video width and height
// @crop_{w,h,left,top}: cropping rectangle defining the visible video area
// @pixel_format: V4L2 pixel format
// @buffer_size: size of video planes (chroma, luma, etc.)
struct videoinfo {
	unsigned w, h;
	unsigned crop_w, crop_h;
	unsigned crop_left, crop_top;
	uint32_t pixel_format;
	unsigned buffer_size[4];
};

#endif // __MAIN_
