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

#if !defined(__EXYNOS_DRM_)
#define __EXYNOS_DRM_

#include <vector>
#include <cstdint>

// Forward-declarations
class ExynosDRM;
class FlipHandler;
class CairoText;
struct CommonDRM;
struct videoinfo;
struct exynos_device;
struct _drmModeAtomicReq;
typedef _drmModeAtomicReq drmModeAtomicReq;


// Buffers can be created via ExynosDRM::alloc_buffers() and remain valid
// until free_buffers() is called.
class ExynosBuffer {
	friend class ExynosDRM;

private:
	struct exynos_bo *bo;

	ExynosDRM *root;

	// Allocate/free a Exynos buffer.
	// alloc() returns false if an error occurs.
	bool alloc(unsigned size);
	void free();

public:
	ExynosBuffer(ExynosDRM *r);
	~ExynosBuffer();

	ExynosBuffer(const ExynosBuffer& b) = delete;
	ExynosBuffer(ExynosBuffer &&b) noexcept;

	void* mmap();
	int get_prime_fd();
	unsigned get_size() const;
};


class ExynosPage {
	friend class ExynosDRM;

private:
	enum flags {
		allocated		= (1 << 0),
		added			= (1 << 1),
		req_created		= (1 << 2),

		// Page is currently in use.
		page_used		= (1 << 3),
	};

	// Framebuffer information struct
	//
	// @{w,h}: total framebuffer width and height
	// @pixel_format: DRM pixel format
	// @tiling: framebuffer uses tiling layout
	struct fbinfo {
		unsigned w, h;
		uint32_t pixel_format;
		bool tiling;
	};

	// Buffer objects and IDs for the primary and the video plane.
	// The video plane is our actual main plane, while the
	// primary plane functions as an overlay to display text.
	struct exynos_bo *bo[2];
	uint32_t buf_id[2];

	// The Cairo text renderer that is used for the primary plane.
	CairoText *renderer;

	// Atomic request to display the page.
	drmModeAtomicReq *atomic_request;

	ExynosDRM *root;

	unsigned flags;

	// Internal methods
	bool alloc_overlay();
	bool alloc_video(unsigned size);
	bool add_overlay();
	bool add_video(const fbinfo &fbi);

	// Order of operations:
	// alloc(), add(), create_request()
	// Any other order is going to result in an error.

	// Allocate/free a Exynos page.
	// alloc() returns false if an error occurs.
	//
	// @size: total size of page in bytes
	bool alloc(unsigned size);
	void free();

	// Add/remove a Exynos page.
	// add() returns false if an error occurs.
	//
	// @fbi: reference to a struct containing framebuffer information
	bool add(const fbinfo &fbi);
	void remove();

	// Create/destroy the atomic request for the page.
	// create_request() returns false if an error occurs.
	bool create_request();
	void destroy_request();

	bool initial_modeset() const;

public:
	ExynosPage(ExynosDRM *r);
	~ExynosPage();

	ExynosPage(const ExynosPage &p) = delete;
	ExynosPage(ExynosPage &&p) noexcept;

	int get_prime_fd();
	void handle_flip();
};


class ExynosDRM {
	friend class ExynosPage;
	friend class ExynosBuffer;

public:
	enum connector_type {
		connector_hdmi = 0,
		connector_vga,
		connector_other,
	};

private:
	enum flags {
		opened				= (1 << 0),
		initialized			= (1 << 1),
		buffers_alloced		= (1 << 2),
		pages_alloced		= (1 << 3),
		pageflip_pending	= (1 << 4),
	};

	int fd;
	enum connector_type preferred_connector;
	struct exynos_device *device;

	CommonDRM *drm;
	FlipHandler *fh;

	std::vector<ExynosPage> pages;

	// currently displayed page
	ExynosPage *cur_page;

	// dimensions of the selected mode
	unsigned width, height;

	unsigned flags;

	bool check_connector_type(enum connector_type ct, uint32_t drm_ct) const;

public:
	ExynosDRM();
	~ExynosDRM();

	ExynosDRM(const ExynosDRM& drm) = delete;

	// Order of operations:
	// open(), init(), alloc_buffers(), alloc_pages()
	// Any other order is going to result in an error.

	// Open/close the Exynos DRM.
	// open() returns false if an error occurs.
	//
	// @ct: connector type that should be used
	bool open(enum connector_type ct);
	void close();

	// Initialize/deinitialize the Exynos DRM.
	// Does some basic initialization for the requested mode.
	// init() returns false if an error occurs.
	//
	// @{w,h}: width, height of the mode to select
	// If w or h is zero, the default mode is selected.
	bool init(unsigned w, unsigned h);
	void deinit();

	// Allocate/free the Exynos DRM buffers.
	// Allocates mmap-capable buffer to be used as source for MFC decoding.
	// alloc_buffers() returns false if an error occurs.
	//
	// @num_buffers: number of buffers to allocate
	// @size: size of each buffer in bytes
	// @bufs: reference to vector of buffer pointers
	bool alloc_buffers(unsigned num_buffers, unsigned size,
					   std::vector<ExynosBuffer> &buffers);
	void free_buffers();

	// Allocate/free the Exynos DRM pages.
	// Allocates pages (buffers that are not mmapped) to be used a destination
	// for MFC decoding.
	// alloc_pages() returns false if an error occurs.
	//
	// @num_pages: number of pages to allocate
	// @vi: reference to a struct containing video information
	bool alloc_pages(unsigned num_pages, const videoinfo &vi);
	void free_pages();

	// Get a pointer to a free page.
	ExynosPage* get_page();

	void wait_for_flip();
	bool issue_flip(ExynosPage *p);
};

#endif // __EXYNOS_DRM_
