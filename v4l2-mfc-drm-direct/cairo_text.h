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

#if !defined(__CAIRO_TEXT_)
#define __CAIRO_TEXT_

#include <cstdint>
#include <string>

// Forward-declarations
struct _cairo;
struct _cairo_surface;
typedef _cairo cairo_t;
typedef _cairo_surface cairo_surface_t;


class CairoText {
private:
	enum flags {
		initialized		= (1 << 0),
	};

	cairo_surface_t *surf;
	cairo_t *ctx;

	unsigned width;
	unsigned height;

	unsigned flags;

public:
	CairoText(unsigned w, unsigned h);
	~CairoText();

	CairoText(const CairoText& ct) = delete;

	unsigned get_size() const;

	bool init(uint8_t *b);
	void deinit();

	void render(const std::string &str);
	void clear();

};

#endif // __CAIRO_TEXT_
