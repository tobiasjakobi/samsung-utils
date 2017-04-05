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

#include "cairo_text.h"

#include <iostream>

#include <cairo/cairo.h>

CairoText::CairoText(unsigned w, unsigned h) : width(w), height(h), flags(0)
{
	// Nothing here.
}

CairoText::~CairoText()
{
	deinit();
}

unsigned CairoText::get_size() const
{
	if (width == 0 || height == 0)
		return 0;

	const unsigned stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, width);

	return stride * height;
}

bool CairoText::init(uint8_t *b)
{
	static const std::string msg_prefix("CairoText::init(): ");

	if (flags & initialized)
		return false;

	if (width == 0 || height == 0)
		return false;

	if (!b)
		return false;

	using namespace std;

	const unsigned stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, width);

	surf = cairo_image_surface_create_for_data(b, CAIRO_FORMAT_ARGB32,
											   width, height, stride);
	if (!surf) {
		cerr << msg_prefix << "failed to create Cairo surface from buffer.\n";
		return false;
	}

	ctx = cairo_create(surf);
	if (!ctx) {
		cerr << msg_prefix << "failed to create Cairo context.\n";
		cairo_surface_destroy(surf);
		return false;
	}

	flags |= initialized;

	return true;
}

void CairoText::deinit()
{
	if (!(flags & initialized))
		return;

	cairo_destroy(ctx);
	cairo_surface_destroy(surf);

	flags &= ~initialized;
}

void CairoText::render(const std::string &str)
{
	if (!(flags & initialized))
		return;

	cairo_set_font_size(ctx, 20.0);
	cairo_select_font_face(ctx, "Liberation Sans", CAIRO_FONT_SLANT_NORMAL,
						   CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_source_rgba(ctx, 0.0, 0.0, 0.0, 1.0);

	cairo_move_to(ctx, 1.0, double(height) - 1.0);
	cairo_show_text(ctx, str.c_str());
}

void CairoText::clear()
{
	if (!(flags & initialized))
		return;

	cairo_rectangle(ctx, 0.0, 0.0, width, height);
	cairo_set_source_rgba(ctx, 0.0, 0.0, 0.0, 0.0);

	cairo_fill(ctx);
}
