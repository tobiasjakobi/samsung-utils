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

#include "exynos_drm.h"
#include "main.h"
#include "cairo_text.h"

#include <string>
#include <map>
#include <memory>
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <cmath>

#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>
#include <linux/videodev2.h>


extern "C" {
#include <libdrm/exynos_drmif.h>
};


class FlipHandler {
private:
	struct pollfd fds;
	drmEventContext evctx;

public:
	FlipHandler(int fd);
	~FlipHandler() {}

	FlipHandler(const FlipHandler& fh) = delete;

	void wait();
};


struct CommonDRM {
	// key.first = object ID
	// key.second = property (see enum e_prop)
	typedef std::pair<uint32_t, unsigned> property_key;
	typedef std::map<property_key, uint32_t> property_map;

	unsigned crtc_index;

	// IDs for connector, CRTC and plane objects.
	uint32_t connector_id;
	uint32_t crtc_id;
	uint32_t plane_id[2];
	uint32_t mode_blob_id;

	property_map pmap;

	// Atomic requests for the initial and the restore modeset.
	drmModeAtomicReq *modeset_request;
	drmModeAtomicReq *restore_request;
};


namespace {

enum e_prop {
	connector_prop_crtc_id = 0,
	crtc_prop_active,
	crtc_prop_mode_id,
	plane_prop_fb_id,
	plane_prop_crtc_id,
	plane_prop_crtc_x,
	plane_prop_crtc_y,
	plane_prop_crtc_w,
	plane_prop_crtc_h,
	plane_prop_src_x,
	plane_prop_src_y,
	plane_prop_src_w,
	plane_prop_src_h,
	plane_prop_zpos,
};

struct drm_prop {
	uint32_t object_type;
	enum e_prop prop;
	std::string prop_name;
};

const std::array<drm_prop, 14> prop_template{{
	// Property IDs of the connector object.
	{ DRM_MODE_OBJECT_CONNECTOR, connector_prop_crtc_id, "CRTC_ID" },

	// Property IDs of the CRTC object.
	{ DRM_MODE_OBJECT_CRTC, crtc_prop_active, "ACTIVE" },
	{ DRM_MODE_OBJECT_CRTC, crtc_prop_mode_id, "MODE_ID" },

	// Property IDs of the plane object.
	{ DRM_MODE_OBJECT_PLANE, plane_prop_fb_id, "FB_ID" },
	{ DRM_MODE_OBJECT_PLANE, plane_prop_crtc_id, "CRTC_ID" },
	{ DRM_MODE_OBJECT_PLANE, plane_prop_crtc_x, "CRTC_X" },
	{ DRM_MODE_OBJECT_PLANE, plane_prop_crtc_y, "CRTC_Y" },
	{ DRM_MODE_OBJECT_PLANE, plane_prop_crtc_w, "CRTC_W" },
	{ DRM_MODE_OBJECT_PLANE, plane_prop_crtc_h, "CRTC_H" },
	{ DRM_MODE_OBJECT_PLANE, plane_prop_src_x, "SRC_X" },
	{ DRM_MODE_OBJECT_PLANE, plane_prop_src_y, "SRC_Y" },
	{ DRM_MODE_OBJECT_PLANE, plane_prop_src_w, "SRC_W" },
	{ DRM_MODE_OBJECT_PLANE, plane_prop_src_h, "SRC_H" },
	{ DRM_MODE_OBJECT_PLANE, plane_prop_zpos, "zpos" },
}};

struct prop_assign {
	enum e_prop prop;
	uint64_t value;
};

enum e_plane_type {
	plane_primary = 0,
	plane_video
};

enum drm_constants {
	overlay_width = 128,
	overlay_height = 64
};

// Dynamic memory management of DRM resources.
namespace drmMode {

	template <typename T>
	void free(T *p) { delete p; }

	template <>
	void free<drmModeObjectProperties>(drmModeObjectProperties *p) { drmModeFreeObjectProperties(p); }
	template <>
	void free<drmModePropertyRes>(drmModePropertyRes *p) { drmModeFreeProperty(p); }
	template <>
	void free<drmModeEncoder>(drmModeEncoder *p) { drmModeFreeEncoder(p); }
	template <>
	void free<drmModeAtomicReq>(drmModeAtomicReq *p) { drmModeAtomicFree(p); }
	template <>
	void free<exynos_device>(exynos_device *p) { exynos_device_destroy(p); }

	template <typename T>
	using drm_mode_unique_ptr = std::unique_ptr<T, decltype(*free<T>)>;

	template <typename T>
	drm_mode_unique_ptr<T> make_unique(T *p) { return drm_mode_unique_ptr<T>(p, free); }

};

// The main pageflip handler which is used by drmHandleEvent.
// Decreases the pending pageflip count and updates the current page.
void
page_flip_handler(int fd, unsigned frame, unsigned sec, unsigned usec, void *data)
{
	ExynosPage *page = static_cast<ExynosPage*>(data);

	page->handle_flip();
}

// Find the name of a compatible DRM device.
void
get_device_name(std::string &name)
{
	using namespace std;

	const string card_prefix = "/dev/dri/card";
	string card;

	int index = 0;
	bool found = false;

	while (!found) {
		card = card_prefix + to_string(index++);
		int fd;

		fd = open(card.c_str(), O_RDWR);
		if (fd < 0)
			break;

		drmVersionPtr ver = drmGetVersion(fd);
		found = (string(ver->name) == "exynos");

		drmFreeVersion(ver);
		::close(fd);
	}

	if (found)
		name = card;
}

// Get the ID of an object's property using the property name.
bool
get_propid_by_name(int fd, uint32_t object_id, uint32_t object_type,
				   const std::string &name, uint32_t &prop_id)
{
	auto properties = drmMode::make_unique(drmModeObjectGetProperties(fd, object_id, object_type));

	if (!properties)
		return false;

	bool found = false;

	for (unsigned i = 0; i < properties->count_props; ++i) {
		drmModePropertyRes *prop;

		prop = drmModeGetProperty(fd, properties->props[i]);
		if (!prop)
			continue;

		if (std::string(prop->name) == name) {
			prop_id = prop->prop_id;
			found = true;
		}

		drmModeFreeProperty(prop);

		if (found)
			break;
	}

	return found;
}

// Get the value of an object's property using the ID.
bool
get_propval_by_id(int fd, uint32_t object_id, uint32_t object_type,
				  uint32_t id, uint64_t &prop_value)
{
	auto properties = drmMode::make_unique(drmModeObjectGetProperties(fd, object_id, object_type));

	if (!properties)
		return false;

	bool found = false;

	for (unsigned i = 0; i < properties->count_props; ++i) {
		drmModePropertyRes *prop;

		prop = drmModeGetProperty(fd, properties->props[i]);
		if (!prop)
			continue;

		if (prop->prop_id == id) {
			prop_value = properties->prop_values[i];
			found = true;
		}

		drmModeFreeProperty(prop);

		if (found)
			break;
	}

	return found;
}

std::vector<uint32_t>
get_ids_from_type(const CommonDRM &drm, uint32_t object_type)
{
	using namespace std;

	switch (object_type) {
	case DRM_MODE_OBJECT_CONNECTOR:
		return vector<uint32_t>{drm.connector_id};

	case DRM_MODE_OBJECT_CRTC:
		return vector<uint32_t>{drm.crtc_id};

	case DRM_MODE_OBJECT_PLANE:
	default:
		return vector<uint32_t>{
			drm.plane_id[plane_primary],
			drm.plane_id[plane_video]};
	}
}

bool
setup_properties(int fd, CommonDRM &drm, drmModeRes &res,
				 drmModePlaneRes &pres)
{
	drm.pmap.clear();

	for (auto &i : prop_template) {
		const uint32_t object_type = i.object_type;
		const std::string &prop_name = i.prop_name;

		int object_count;
		uint32_t *object_ids;

		switch (object_type) {
		case DRM_MODE_OBJECT_CONNECTOR:
			object_count = res.count_connectors;
			object_ids = res.connectors;
			break;

		case DRM_MODE_OBJECT_CRTC:
			object_count = res.count_crtcs;
			object_ids = res.crtcs;
			break;

		case DRM_MODE_OBJECT_PLANE:
			default:
			object_count = pres.count_planes;
			object_ids = pres.planes;
			break;
		}

		for (int j = 0; j < object_count; ++j) {
			const uint32_t obj_id = object_ids[j];
			uint32_t prop_id;

			if (!get_propid_by_name(fd, obj_id, object_type, prop_name, prop_id)) {
				drm.pmap.clear();

				return false;
			}

			drm.pmap.emplace(std::make_pair(obj_id, i.prop), prop_id);
		}
	}

	return true;
}

bool
create_restore_req(int fd, CommonDRM &drm)
{
	auto req = drmMode::make_unique(drmModeAtomicAlloc());

	for (auto &i : prop_template) {
		const uint32_t object_type = i.object_type;

		for (auto &j : get_ids_from_type(drm, object_type)) {
			const uint32_t prop_id = drm.pmap.at(std::make_pair(j, i.prop));
			uint64_t prop_value;

			if (!get_propval_by_id(fd, j, object_type, prop_id, prop_value))
				return false;

			if (drmModeAtomicAddProperty(req.get(), j, prop_id, prop_value) < 0)
				return false;
		}
	}

	drm.restore_request = req.release();
	return true;
}

bool
add_overlay_props(drmModeAtomicReq *req, CommonDRM &drm, unsigned w, unsigned h)
{
	const unsigned overlay_x = (w <= overlay_width * 2) ?
		0 : (w - overlay_width * 2) / 2;
	const unsigned overlay_y = (h <= overlay_height * 2) ?
		0 : (h - overlay_height * 2) / 2;

	const struct prop_assign assign[] = {
		{ plane_prop_crtc_id, drm.crtc_id },
		{ plane_prop_crtc_x, overlay_x },
		{ plane_prop_crtc_y, overlay_y },
		// We make use of the pixel-doubling feature here.
		{ plane_prop_crtc_w, overlay_width * 2 },
		{ plane_prop_crtc_h, overlay_height * 2 },
		{ plane_prop_src_x, 0 },
		{ plane_prop_src_y, 0 },
		{ plane_prop_src_w, overlay_width << 16 },
		{ plane_prop_src_h, overlay_height << 16 },
		{ plane_prop_zpos, 2 },
	};

	const uint32_t obj_id = drm.plane_id[plane_primary];

	for (auto &i : assign) {
		const uint32_t prop_id = drm.pmap.at(std::make_pair(obj_id, i.prop));

		if (drmModeAtomicAddProperty(req, obj_id, prop_id, i.value) < 0)
			return false;
	}

	return true;
}

bool
add_video_props(drmModeAtomicReq *req, CommonDRM &drm, unsigned w, unsigned h,
				const videoinfo &vi)
{
	const float mode_aspect = float(w) / float(h);
	const float video_aspect = float(vi.crop_w) / float(vi.crop_h);

	unsigned width, height;

	if (std::fabs(mode_aspect - video_aspect) < 0.0001f) {
		width = w;
		height = h;
	} else if (mode_aspect > video_aspect) {
		width = float(w) * video_aspect / mode_aspect;
		height = h;
	} else {
		width = w;
		height = float(h) * mode_aspect / video_aspect;
	}

	const struct prop_assign assign[] = {
		{ plane_prop_crtc_id, drm.crtc_id },
		{ plane_prop_crtc_x, (w - width) / 2 },
		{ plane_prop_crtc_y, (h - height) / 2 },
		{ plane_prop_crtc_w, width },
		{ plane_prop_crtc_h, height },
		{ plane_prop_src_x, vi.crop_left },
		{ plane_prop_src_y, vi.crop_top },
		{ plane_prop_src_w, vi.crop_w << 16 },
		{ plane_prop_src_h, vi.crop_h << 16 },
		{ plane_prop_zpos, 0 },
	};

	const uint32_t obj_id = drm.plane_id[plane_video];

	for (auto &i : assign) {
		const uint32_t prop_id = drm.pmap.at(std::make_pair(obj_id, i.prop));

		if (drmModeAtomicAddProperty(req, obj_id, prop_id, i.value) < 0)
			return false;
	}

	return true;
}

bool
create_modeset_req(int fd, CommonDRM &drm, unsigned w, unsigned h,
				   const videoinfo &vi)
{
	using namespace std;

	auto req = drmMode::make_unique(drmModeAtomicAlloc());
	if (!req)
		return false;

	uint32_t prop_id;

	prop_id = drm.pmap[make_pair(drm.connector_id, connector_prop_crtc_id)];
	if (drmModeAtomicAddProperty(req.get(), drm.connector_id, prop_id, drm.crtc_id) < 0)
		return false;

	prop_id = drm.pmap[make_pair(drm.crtc_id, crtc_prop_active)];
	if (drmModeAtomicAddProperty(req.get(), drm.crtc_id, prop_id, 1) < 0)
		return false;

	prop_id = drm.pmap[make_pair(drm.crtc_id, crtc_prop_mode_id)];
	if (drmModeAtomicAddProperty(req.get(), drm.crtc_id, prop_id, drm.mode_blob_id) < 0)
		return false;

	if (!add_overlay_props(req.get(), drm, w, h))
		return false;

	if (!add_video_props(req.get(), drm, w, h, vi))
		return false;

	drm.modeset_request = req.release();

	return true;
}

bool validate_videoinfo(const videoinfo &vi)
{
	if (vi.w == 0 || vi.h == 0)
		return false;

	if (vi.crop_w == 0 || vi.crop_h == 0)
		return false;

	if (vi.crop_left + vi.crop_w > vi.w)
		return false;

	if (vi.crop_top + vi.crop_h > vi.h)
		return false;

	return true;
}

}; // anonymous namespace


FlipHandler::FlipHandler(int fd)
{
	zerostruct(&fds);
	zerostruct(&evctx);

	fds.fd = fd;
	fds.events = POLLIN;
	evctx.version = DRM_EVENT_CONTEXT_VERSION;
	evctx.page_flip_handler = page_flip_handler;
}

void FlipHandler::wait()
{
	const int timeout = -1;

	fds.revents = 0;

	if (poll(&fds, 1, timeout) < 0)
		return;

	if (fds.revents & (POLLHUP | POLLERR))
		return;

	if (fds.revents & POLLIN)
		drmHandleEvent(fds.fd, &evctx);
}

ExynosBuffer::ExynosBuffer(ExynosDRM *r) : bo(nullptr), root(r)
{
	// Nothing here.
}

ExynosBuffer::~ExynosBuffer()
{
	free();
}

ExynosBuffer::ExynosBuffer(ExynosBuffer &&b) noexcept
{
	root = b.root;
	bo = b.bo;
	b.bo = nullptr;
}

bool ExynosBuffer::alloc(unsigned size)
{
	if (bo)
		return false;

	const unsigned bo_flags = 0;

	bo = exynos_bo_create(root->device, size, bo_flags);
	if (!bo)
		return false;

	return true;
}

void ExynosBuffer::free()
{
	exynos_bo_destroy(bo);
	bo = nullptr;
}

void* ExynosBuffer::mmap()
{
	void *ptr = nullptr;

	if (bo)
		ptr = exynos_bo_map(bo);

	return ptr;
}

int ExynosBuffer::get_prime_fd()
{
	if (!bo)
		return -1;

	int prime_fd;

	// The buffer is only written to from userspace. The MFC only
	// reads from the buffer, hence we don't export it as r/w.
	int ret = drmPrimeHandleToFD(root->fd, bo->handle, DRM_CLOEXEC, &prime_fd);
	if (ret < 0)
		return -1;

	return prime_fd;
}

unsigned ExynosBuffer::get_size() const
{
	if (!bo)
		return 0;

	return bo->size;
}


ExynosPage::ExynosPage(ExynosDRM *r) : root(r), flags(0)
{
	// Nothing here
}

ExynosPage::~ExynosPage()
{
	destroy_request();
	remove();
	free();
}

ExynosPage::ExynosPage(ExynosPage &&p) noexcept
{
	root = p.root;

	atomic_request = p.atomic_request;

	buf_id[plane_primary] = p.buf_id[plane_primary];
	buf_id[plane_video] = p.buf_id[plane_video];

	bo[plane_primary] = p.bo[plane_primary];
	bo[plane_video] = p.bo[plane_video];

	renderer = p.renderer;

	flags = p.flags;
	p.flags = 0;
}

bool ExynosPage::alloc_overlay()
{
	const unsigned sz = overlay_width * overlay_height * sizeof(uint32_t);
	const unsigned bo_flags = 0;

	using namespace std;

	auto b = drmMode::make_unique(exynos_bo_create(root->device, sz, bo_flags));
	if (!b)
		return false;

	try {
		renderer = new CairoText(overlay_width, overlay_height);
		if (!renderer)
			throw exception();

		if (renderer->get_size() != sz)
			throw exception();

		if (!renderer->init(static_cast<uint8_t*>(exynos_bo_map(b.get()))))
			throw exception();
	}
	catch (exception &e) {
		delete renderer;
		return false;
	}

	renderer->clear();
	bo[plane_primary] = b.release();

	return true;
}

bool ExynosPage::alloc_video(unsigned size)
{
	// We don't access the video BO through userspace, hence don't map it.
	const unsigned bo_flags = 0;

	bo[plane_video] = exynos_bo_create(root->device, size, bo_flags);
	if (!bo[plane_video])
		return false;

	return true;
}

bool ExynosPage::add_overlay()
{
	uint32_t handles[4] = {bo[plane_primary]->handle, 0, 0, 0};
	uint32_t pitches[4] = {overlay_width * sizeof(uint32_t), 0, 0, 0};
	uint32_t offsets[4] = {0, 0, 0, 0};
	uint32_t fb_flags = 0;

	if (drmModeAddFB2(root->fd, overlay_width, overlay_height, DRM_FORMAT_ARGB8888,
					  handles, pitches, offsets, &buf_id[plane_primary], fb_flags))
		return false;

	return true;
}

bool ExynosPage::add_video(const fbinfo &fbi)
{
	unsigned pitch[2];

	switch (fbi.pixel_format) {
	case DRM_FORMAT_NV12:
		pitch[0] = fbi.w * sizeof(uint8_t);
		pitch[1] = fbi.w * sizeof(uint8_t);
		break;

	case DRM_FORMAT_NV21:
		pitch[0] = fbi.w * sizeof(uint8_t);
		pitch[1] = fbi.w * sizeof(uint8_t);
		break;

	default:
		return false;
	}

	uint32_t handles[4] = {bo[plane_video]->handle, bo[plane_video]->handle, 0, 0};
	uint32_t pitches[4] = {pitch[0], pitch[1], 0, 0};
	uint32_t offsets[4] = {0, pitch[0] * fbi.h, 0, 0};
	uint64_t modifiers[4] = {0};
	uint32_t fb_flags = 0;

	if (fbi.tiling) {
		fb_flags |= DRM_MODE_FB_MODIFIERS;
		modifiers[0] = modifiers[1] = DRM_FORMAT_MOD_SAMSUNG_64_32_TILE;
	}

	if (drmModeAddFB2WithModifiers(root->fd, fbi.w, fbi.h, fbi.pixel_format, handles, pitches,
								   offsets, modifiers, &buf_id[plane_video], fb_flags))
		return false;

	return true;
}

bool ExynosPage::alloc(unsigned size)
{
	static const std::string msg_prefix("ExynosPage::alloc(): ");

	if (flags & allocated)
		return false;

	using namespace std;

	if (!alloc_overlay()) {
		cerr << msg_prefix << "failed to allocate buffer for overlay.\n";
		return false;
	}

	if (!alloc_video(size)) {
		cerr << msg_prefix << "failed to allocate buffer for video.\n";
		return false;
	}

	flags |= allocated;

	return true;
}

void ExynosPage::free()
{
	if (!(flags & allocated))
		return;

	if (flags & added)
		return;

	delete renderer;
	exynos_bo_destroy(bo[plane_primary]);
	exynos_bo_destroy(bo[plane_video]);

	flags &= ~allocated;
}

bool ExynosPage::add(const fbinfo &fbi)
{
	static const std::string msg_prefix("ExynosPage::add(): ");

	if (flags & added)
		return false;

	if (!(flags & allocated))
		return false;

	using namespace std;

	if (!add_overlay()) {
		cerr << msg_prefix << "failed to add overlay buffer as FB.\n";
		return false;
	}

	if (!add_video(fbi)) {
		cerr << msg_prefix << "failed to add video buffer as FB.\n";
		return false;
	}

	flags |= added;

	return true;
}

void ExynosPage::remove()
{
	if (!(flags & added))
		return;

	if (flags & req_created)
		return;

	drmModeRmFB(buf_id[plane_primary], bo[plane_primary]->handle);
	drmModeRmFB(buf_id[plane_video], bo[plane_video]->handle);

	flags &= ~added;
}

bool ExynosPage::create_request()
{
	if (flags & req_created)
		return false;

	if (!(flags & added))
		return false;

	using namespace std;

	auto req = drmMode::make_unique(drmModeAtomicAlloc());
	if (!req)
		return false;

	const CommonDRM &drm = *root->drm;

	for (auto &i : {plane_primary, plane_video}) {
		const uint32_t obj_id = drm.plane_id[i];
		const uint32_t prop_id = drm.pmap.at(make_pair(obj_id, plane_prop_fb_id));

		if (drmModeAtomicAddProperty(req.get(), obj_id, prop_id, buf_id[i]) < 0)
			return false;
	}

	atomic_request = req.release();

	flags |= req_created;

	return true;
}

void ExynosPage::destroy_request()
{
	if (!(flags & req_created))
		return;

	drmModeAtomicFree(atomic_request);

	flags &= ~req_created;
}

bool ExynosPage::initial_modeset() const
{
	if (!(flags & req_created))
		return false;

	using namespace std;

	renderer->render("Hello world!"); // TODO

	auto req = drmMode::make_unique(drmModeAtomicDuplicate(root->drm->modeset_request));
	if (!req)
		return false;

	if (drmModeAtomicMerge(req.get(), atomic_request))
		return false;

	if (drmModeAtomicCommit(root->fd, req.get(), DRM_MODE_ATOMIC_ALLOW_MODESET, nullptr))
		return false;

	return true;
}

int ExynosPage::get_prime_fd()
{
	if (!(flags & added))
		return -1;

	int prime_fd, ret;

	// MFC writes the decoded data into this buffer, hence export it as r/w.
	ret = drmPrimeHandleToFD(root->fd, bo[plane_video]->handle,
							 DRM_RDWR | DRM_CLOEXEC, &prime_fd);
	if (ret < 0)
		return -1;

	return prime_fd;
}

void ExynosPage::handle_flip()
{
	static const std::string msg_prefix("ExynosPage::handle_flip(): ");

	std::cout << msg_prefix << "page = " << this << '\n';

	if (root->cur_page)
		root->cur_page->flags &= ~page_used;

	root->flags &= ~ExynosDRM::pageflip_pending;
	root->cur_page = this;
}


bool ExynosDRM::check_connector_type(enum connector_type ct, uint32_t drm_ct) const
{
	enum connector_type t;

	switch (drm_ct) {
	case DRM_MODE_CONNECTOR_HDMIA:
	case DRM_MODE_CONNECTOR_HDMIB:
		t = connector_hdmi;
		break;

	case DRM_MODE_CONNECTOR_VGA:
		t = connector_vga;
		break;

	default:
		t = connector_other;
		break;
	}

	return (t == ct);
}

ExynosDRM::ExynosDRM() : flags(0)
{
	// Nothing here.
}

ExynosDRM::~ExynosDRM()
{
	free_pages();
	free_buffers();
	deinit();
	close();
}

bool ExynosDRM::open(enum connector_type ct)
{
	static const std::string msg_prefix("ExynosDRM::open(): ");

	if (flags & opened)
		return false;

	using namespace std;

	string device_name;
	get_device_name(device_name);

	if (device_name.empty()) {
		cerr << msg_prefix << "no compatible DRM device found.\n";
		return false;
	}

	fd = ::open(device_name.c_str(), O_RDWR, 0);
	if (fd < 0) {
		cerr << msg_prefix << "failed to open DRM device.\n";
		return false;
	}

	try {
		drm = nullptr;

		// Request atomic DRM support. This also enables universal planes.
		if (drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1) < 0)
			throw runtime_error("failed to enable atomic support");

		drm = new CommonDRM;
		if (!drm)
			throw runtime_error("failed to allocate DRM");

		auto resources = drmMode::make_unique(drmModeGetResources(fd));
		if (!resources)
			throw runtime_error("failed to get DRM resources");

		auto plane_resources = drmMode::make_unique(drmModeGetPlaneResources(fd));
		if (!plane_resources)
			throw runtime_error("failed to get DRM plane resources");

		auto connector = drmMode::make_unique<drmModeConnector>(nullptr);
		for (unsigned i = 0; i < unsigned(resources->count_connectors); ++i) {
			connector.reset(drmModeGetConnector(fd, resources->connectors[i]));
			if (!connector)
				continue;

			if (check_connector_type(ct, connector->connector_type) &&
					connector->connection == DRM_MODE_CONNECTED &&
					connector->count_modes > 0) {
				drm->connector_id = connector->connector_id;
				break;
			}

			connector.reset(nullptr);
		}

		if (!connector)
			throw runtime_error("no currently active connector found");

		auto encoder = drmMode::make_unique<drmModeEncoder>(nullptr);
		for (unsigned i = 0; i < unsigned(connector->count_encoders); ++i) {
			encoder.reset(drmModeGetEncoder(fd, connector->encoders[i]));
			if (!encoder)
				continue;

			// Find a CRTC that is compatible with the encoder.
			unsigned j;
			for (j = 0; j < unsigned(resources->count_crtcs); ++j) {
				if (encoder->possible_crtcs & (1 << j))
					break;
			}

			// Stop when a suitable CRTC was found.
			if (j != unsigned(resources->count_crtcs)) {
				// Store index and ID of the CRTC.
				// We need the index again later, when we validate the planes.
				drm->crtc_index = j;
				drm->crtc_id = resources->crtcs[j];

				break;
			}

			encoder.reset(nullptr);
		}

		if (!encoder)
			throw runtime_error("no compatible encoder found");

		if (!setup_properties(fd, *drm, *resources, *plane_resources))
			throw runtime_error("failed to setup properties");
	}
	catch (exception &e) {
		delete drm;
		::close(fd);

		cerr << msg_prefix << e.what() << ".\n";

		return false;
	}

	cout << msg_prefix << "using DRM device \"" << device_name << "\" with:\n"
		 << "\tconnector ID = " << drm->connector_id << '\n'
		 << "\tcrtc ID = " << drm->crtc_id << '\n';

	flags |= opened;

	return true;
}

void ExynosDRM::close()
{
	if (!(flags & opened))
		return;

	if (flags & initialized)
		return;

	delete drm;
	::close(fd);

	flags &= ~opened;
}

bool ExynosDRM::init(unsigned w, unsigned h)
{
	static const std::string msg_prefix("ExynosDRM::init(): ");

	if (flags & initialized)
		return false;

	if (!(flags & opened))
		return false;

	using namespace std;

	auto connector = drmMode::make_unique(drmModeGetConnector(fd, drm->connector_id));
	if (!connector) {
		cerr << msg_prefix << "failed to get connector.\n";
		return false;
	}

	drmModeModeInfo *mode;
	if (w != 0 && h != 0) {
		mode = nullptr;

		for (unsigned i = 0; i < unsigned(connector->count_modes); ++i) {
			if (connector->modes[i].hdisplay == w &&
				connector->modes[i].vdisplay == h) {
				mode = &connector->modes[i];
				break;
			}
		}

		if (!mode) {
			cerr << msg_prefix << "requested resolution (" << w << 'x'
				 << h << ") not available.\n";
			return false;
		}

	} else {
		// Select first mode, which is the native one.
		mode = &connector->modes[0];
	}

	if (mode->hdisplay == 0 || mode->vdisplay == 0) {
		cerr << msg_prefix << "failed to select sane resolution.\n";
		return false;
	}

	fh = new FlipHandler(fd);
	if (!fh) {
		cerr << msg_prefix << "failed to allocate fliphandler.\n";
		return false;
	}

	if (drmModeCreatePropertyBlob(fd, mode, sizeof(drmModeModeInfo), &drm->mode_blob_id)) {
		cerr << msg_prefix << "failed to blobify mode info.\n";
		delete fh;
	}

	width = mode->hdisplay;
	height = mode->vdisplay;
	cout << msg_prefix << "resolution = " << width << " x " << height << '\n';

	flags |= initialized;

	return true;
}

void ExynosDRM::deinit()
{
	if (!(flags & initialized))
		return;

	if (flags & buffers_alloced)
		return;

	drmModeDestroyPropertyBlob(fd, drm->mode_blob_id);
	delete fh;

	flags &= ~initialized;
}

bool ExynosDRM::alloc_buffers(unsigned num_buffers, unsigned size, std::vector<ExynosBuffer> &buffers)
{
	static const std::string msg_prefix("ExynosDRM::alloc_buffers(): ");

	if (flags & buffers_alloced)
		return false;

	if (!(flags & initialized))
		return false;

	using namespace std;

	if (num_buffers == 0) {
		cerr << msg_prefix << "no buffers requested.\n";
		return false;
	}

	if (size == 0) {
		cerr << msg_prefix << "zero-sized buffers requested.\n";
		return false;
	}

	using namespace std;

	device = exynos_device_create(fd);
	if (!device) {
		cerr << msg_prefix << "failed to create device from fd.\n";
		return false;
	}

	buffers.clear();

	try {
		for (unsigned i = 0; i < num_buffers; ++i) {
			buffers.emplace_back(this);

			if (!buffers.back().alloc(size))
				throw exception();
		}
	}
	catch (exception &e) {
		buffers.clear();
		exynos_device_destroy(device);

		cerr << msg_prefix << "allocation of mmap-capable buffers failed.\n";

		return false;
	}

	flags |= buffers_alloced;

	return true;
}

void ExynosDRM::free_buffers()
{
	if (!(flags & buffers_alloced))
		return;

	if (flags & pages_alloced)
		return;

	exynos_device_destroy(device);

	flags &= ~buffers_alloced;
}

bool ExynosDRM::alloc_pages(unsigned num_pages, const videoinfo &vi)
{
	static const std::string msg_prefix("ExynosDRM::alloc_pages(): ");

	if (flags & pages_alloced)
		return false;

	if (!(flags & buffers_alloced))
		return false;

	using namespace std;

	if (!validate_videoinfo(vi)) {
		cerr << msg_prefix << "invalid video info.\n";
		return false;
	}

	uint32_t drm_fmt;
	bool tiling;

	switch (vi.pixel_format) {
	case V4L2_PIX_FMT_NV12:
		drm_fmt = DRM_FORMAT_NV12;
		tiling = false;
		break;

	case V4L2_PIX_FMT_NV21:
		drm_fmt = DRM_FORMAT_NV21;
		tiling = false;
		break;

	case V4L2_PIX_FMT_NV12MT:
		drm_fmt = DRM_FORMAT_NV12;
		tiling = true;
		break;

	default:
		cerr << msg_prefix << "unknown V4L2 pixel format.\n";
		return false;
	}

	auto plane_resources = drmMode::make_unique(drmModeGetPlaneResources(fd));
	if (!plane_resources) {
		cerr << msg_prefix << "failed to get DRM plane resources.\n";
		return false;
	}

	drm->plane_id[plane_primary] = 0;
	drm->plane_id[plane_video] = 0;

	for (unsigned i = 0; i < plane_resources->count_planes; ++i) {
		const uint32_t plane_id = plane_resources->planes[i];
		drmModePlane *plane;

		plane = drmModeGetPlane(fd, plane_id);

		if (!plane)
			continue;

		// Make sure that the plane can be used with the selected CRTC.
		if (plane->possible_crtcs & (1 << drm->crtc_index)) {
			// Check that the plane supports the chosen pixel format.
			for (unsigned j = 0; j < plane->count_formats; ++j) {
				const uint32_t f = plane->formats[j];

				if (f == drm_fmt && !drm->plane_id[plane_video]) {
					drm->plane_id[plane_video] = plane->plane_id;
					break;
				} else if (f == DRM_FORMAT_ARGB8888 &&
						   !drm->plane_id[plane_primary]) {
					drm->plane_id[plane_primary] = plane->plane_id;
					break;
				}
			}
		}

		drmModeFreePlane(plane);
	}

	if (!drm->plane_id[plane_primary]) {
		cerr << msg_prefix << "no primary plane with support for ARGB32 found.\n";
		return false;
	}

	if (!drm->plane_id[plane_video]) {
		cerr << msg_prefix << "no video plane found.\n";
		return false;
	}

	try {
		drm->modeset_request = nullptr;

		if (!create_restore_req(fd, *drm))
			throw runtime_error("failed to create restore atomic request");

		if (!create_modeset_req(fd, *drm, width, height, vi))
			throw runtime_error("failed to create modeset atomic request");

		const ExynosPage::fbinfo fbi = { // TODO
			vi.w, vi.h,
			drm_fmt,
			tiling
		};

		const unsigned fb_size = vi.buffer_size[0] + vi.buffer_size[1]; // TODO

		for (unsigned i = 0; i < num_pages; ++i) {
			pages.emplace_back(this);

			if (!pages.back().alloc(fb_size))
				throw runtime_error("failed to allocate BO for page");

			if (!pages.back().add(fbi))
				throw runtime_error("failed to add BO as framebuffer");

			if (!pages.back().create_request())
				throw runtime_error("failed to create atomic request for page");
		}

		cur_page = get_page();
		if (!cur_page->initial_modeset())
			throw runtime_error("initial atomic modeset failed");
	}
	catch (exception &e) {
		pages.clear();
		drmModeAtomicFree(drm->modeset_request);
		drmModeAtomicFree(drm->restore_request);

		cerr << msg_prefix << e.what() << ".\n";

		return false;
	}

	flags |= pages_alloced;

	return true;
}

void ExynosDRM::free_pages()
{
	static const std::string msg_prefix("ExynosDRM::free_pages(): ");

	if (!(flags & pages_alloced))
		return;

	// Restore the display state.
	if (drmModeAtomicCommit(fd, drm->restore_request,
							DRM_MODE_ATOMIC_ALLOW_MODESET, nullptr)) {
		std::cerr << msg_prefix << "failed to restore the display.\n";
	}

	pages.clear();

	drmModeAtomicFree(drm->modeset_request);
	drmModeAtomicFree(drm->restore_request);

	flags &= ~pages_alloced;
}

ExynosPage* ExynosDRM::get_page()
{
	for (auto& it : pages) {
		if (it.flags & ExynosPage::page_used)
			continue;

		it.flags |= ExynosPage::page_used;
		return &it;
	}

	return nullptr;
}

void ExynosDRM::wait_for_flip()
{
	fh->wait();
}

bool ExynosDRM::issue_flip(ExynosPage *p)
{
	static const std::string msg_prefix("ExynosDRM::issue_flip(): ");

	if (!p)
		return false;

	// We don't queue multiple page flips.
	if (flags & pageflip_pending)
		wait_for_flip();

	// Issue a page flip at the next vblank interval.
	if (drmModeAtomicCommit(fd, p->atomic_request, DRM_MODE_PAGE_FLIP_EVENT, p)) {
		std::cerr << msg_prefix << "failed to issue atomic page flip.\n";
		return false;
	}

	flags |= pageflip_pending;

	// On startup no frame is displayed. We therefore
	// wait for the initial flip to finish.
	if (!cur_page)
		wait_for_flip();

	return true;
}
