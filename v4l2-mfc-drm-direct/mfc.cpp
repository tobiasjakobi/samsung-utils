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

#include "mfc.h"
#include "main.h"
#include "parser.h"
#include "exynos_drm.h"

#include <string>
#include <iostream>
#include <algorithm>
#include <stdexcept>

#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>


class QueueHandler {
private:
	struct pollfd fds;

public:
	QueueHandler(int fd);
	~QueueHandler() {}

	QueueHandler(const QueueHandler& fh) = delete;

	bool wait();
};


namespace {

enum mfc_constants {
	// Maximum number of source buffers.
	max_source_buffer_count = 16,

	// Maximum number of destination buffers.
	// 32 is the limit imposed by the MFC.
	max_dest_buffer_count = 32,

	// Number of source planes.
	source_plane_count = 1,

	// Number of destination planes.
	dest_plane_count = 2,

	// We always need one destination buffer as scanout buffer. Another one
	// is needed to be queued to be the next scanout. This number is added
	// on top of the destination required for the MFC hardware to decode.
	dest_extra_buffer_count = 2,
};

enum flags {
	opened			= (1 << 0),
	parser_set		= (1 << 1),
	source_set		= (1 << 2),
	initialized		= (1 << 3),
	dest_stream		= (1 << 4),
};

enum buffer_flags {
	busy			= (1 << 0),
};

inline std::string
u8tostr(const uint8_t* d)
{
	return std::string(reinterpret_cast<const char*>(d));
}

const char*
v4l2_type_to_string(uint32_t type)
{
	switch (type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		return "destination";

	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		return "source";

	default:
		return "unknown";
	}
}

const char*
v4l2_stream_to_string(unsigned long req)
{
	switch (req) {
	case VIDIOC_STREAMON:
		return "enabled";

	case VIDIOC_STREAMOFF:
		return "disabled";

	default:
		return "unknown";
	}
}

// Check capabilities of the MFC encoder device.
// Returns false on error.
inline bool
check_caps(uint32_t c)
{
	if (!(c & V4L2_CAP_VIDEO_M2M_MPLANE))
		return false;

	if (!(c & V4L2_CAP_STREAMING))
		return false;

	return true;
}

}; // anonymous namespace


QueueHandler::QueueHandler(int fd)
{
	zerostruct(&fds);

	fds.fd = fd;
	fds.events = POLLIN;
}

bool QueueHandler::wait()
{
	const int timeout = 500;

	fds.revents = 0;

	if (poll(&fds, 1, timeout) < 0)
		return false;

	if (fds.revents & (POLLHUP | POLLERR))
		return false;

	if (!(fds.revents & POLLIN))
		return false;

	return true;
}

MFCDecoder::MFCDecoder() : flags(0) {}

MFCDecoder::~MFCDecoder()
{
	close();
	// TODO
}

bool MFCDecoder::open()
{
	static const std::string msg_prefix("MFCDecoder::open(): ");

	if (flags & opened)
		return false;

	using namespace std;

	bool found = false;
	struct v4l2_capability cap;
	const string video_prefix = "/dev/video";

	fd = -1;
	for (unsigned i = 0; ; ++i) {
		const string video_device = video_prefix + to_string(i);

		fd = ::open(video_device.c_str(), O_RDWR, 0);
		if (fd < 0)
			break;

		zerostruct(&cap);

		int ret = ioctl(fd, VIDIOC_QUERYCAP, &cap);
		if (ret != 0) {
			::close(fd);
			continue;
		}

		if (u8tostr(cap.card) != "s5p-mfc-dec") {
			::close(fd);
			continue;
		}

		cout << msg_prefix << "MFC decoder detected at " << video_device << ":\n";
		cout << '\t' << "driver = " << u8tostr(cap.driver) << '\n'
			 << '\t' << "bus_info = " << u8tostr(cap.bus_info) << '\n'
			 << '\t' << "card = " << u8tostr(cap.card) << '\n';

		if (!check_caps(cap.capabilities)) {
			cerr << msg_prefix << "MFC decoder device is missing critical caps.\n";
			::close(fd);
			break;
		}

		found = true;
		break;
	}

	if (!found)
		return false;

	qh = new QueueHandler(fd);

	flags |= opened;

	return true;
}

void MFCDecoder::close()
{
	if (!(flags & opened))
		return;

	delete qh;
	::close(fd);

	flags &= ~opened;
}

bool MFCDecoder::set_parser(Parser *p)
{
	static const std::string msg_prefix("MFCDecoder::set_parser(): ");

	if (flags & parser_set)
		return false;

	if (!(flags & opened))
		return false;

	if (!p->is_linked()) {
		std::cerr << msg_prefix << "parser is not linked.\n";
		return false;
	}

	parser = p;

	flags |= parser_set;

	return true;
}


void MFCDecoder::unset_parser()
{
	if (!(flags & parser_set))
		return;

	if (flags & source_set)
		return;

	parser->reset();

	flags &= ~parser_set;
}

bool MFCDecoder::set_source(std::vector<ExynosBuffer> &buffers)
{
	static const std::string msg_prefix("MFCDecoder::set_source(): ");

	if (!(flags & parser_set))
		return false;

	using namespace std;

	if (buffers.empty()) {
		cerr << msg_prefix << "source buffers vector empty.\n";
		return false;
	}

	source_buffer_size = 0;

	unsigned index = 0;
	source_buffers.clear();
	for (auto &i : buffers) {
		buffer b = {
			reinterpret_cast<uint8_t*>(i.mmap()),
			index++,
			i.get_prime_fd(),
			0
		};

		if (source_buffer_size != 0) {
			if (source_buffer_size != i.get_size()) {
				std::cerr << msg_prefix << "source buffer size mismatch.\n";
				return false;
			}
		} else {
			source_buffer_size = i.get_size();
		}

		source_buffers.emplace_back(b);
	}

	if (!set_source_v4l2())
		return false;

	int frame_size;
	bool fs;

	if (!parser->parse(source_buffers[0].addr, source_buffer_size,
					   frame_size, fs, true)) {
		cerr << msg_prefix << "failed to extract header from stream.\n";
		return false;
	}

	cout << msg_prefix << "extracted a header of size " << frame_size << ".\n";

	// For H263, the header is passed with the first frame, hence we need to
	// feed it into the decoder again. Reset the parser for this.
	if (parser->get_codec() == V4L2_PIX_FMT_H263)
		parser->reset();

	if (!qsrc(0, frame_size)) {
		cerr << msg_prefix << "failed to queue initial source buffer.\n";
		return false;
	}

	source_buffers[0].flags |= busy;

	if (!stream(MFCDecoder::source, true)) {
		cerr << msg_prefix << "failed to enabling streaming for source buffer.\n";
		return false;
	}

	flags |= source_set;

	return true;
}

void MFCDecoder::unset_source()
{
	if (!(flags & source_set))
		return;

	if (flags & initialized)
		return;

	// TODO

	flags &= ~source_set;
}

bool MFCDecoder::init(unsigned &num_buffers, videoinfo &vi)
{
	static const std::string msg_prefix("MFCDecoder::init(): ");

	if (flags & initialized)
		return false;

	if (!(flags & source_set))
		return false;

	using namespace std;

	zerostruct(&vi);

	if (!set_dest_v4l2(vi))
		return false;

	cout << msg_prefix << "MFC buffer parameters:\n"
		 << "\tresolution (full) = " << vi.w << " x " << vi.h << '\n'
		 << "\tbuffer_size[0] = " << vi.buffer_size[0]
		 << ", buffer_size[1] = " << vi.buffer_size[1] << '\n'
		 << "\tbuffer_size[2] = " << vi.buffer_size[2]
		 << ", buffer_size[3] = " << vi.buffer_size[3] << '\n';

	cout << msg_prefix << "MFC crop parameters:\n"
		 << "\twidth = " << vi.crop_w
		 << ", height = " << vi.crop_h << '\n'
		 << "\tleft = " << vi.crop_left
		 << ", top = " << vi.crop_top << '\n';

	num_buffers = dest_buffer_count;

	// TODO

	flags |= initialized;

	return true;
}

void MFCDecoder::deinit()
{
	if (!(flags & initialized))
		return;

	// TODO

	flags &= ~initialized;
}

bool MFCDecoder::ready() const
{
	if (!(flags & initialized))
		return false;

	if (dest_num_queued < dest_queue_min)
		return false;

	return true;
}

enum MFCDecoder::run_state MFCDecoder::run()
{
	static const std::string msg_prefix("MFCDecoder::run(): ");

	if (!(flags & initialized))
		return run_error;

	using namespace std;

	if (dest_num_queued < dest_queue_min) {
		cerr << "DEBUG: num error.\n";
		return run_error;
	}

	// If the MFC decoder is ready, enabling streaming for the
	// destination queue.
	if (!(flags & dest_stream)) {
		if (!stream(destination, true))
			return run_error;

		flags |= dest_stream;
	}

	enum run_state ret = run_nop;

	// Queue non-busy source buffers if the parser is still active.
	for (auto &i : source_buffers) {
		if (i.flags & busy)
			continue;

		int size;
		bool finished;

		if (!parser->parse(i.addr, source_buffer_size, size, finished, false))
			return run_error;

		cout << msg_prefix << "parser extracted " << size << " bytes.\n";

		if (finished && parser->finished()) {
			cout << msg_prefix << "parser has extracted all frames.\n";

			ret = run_finished;
			break;
		}

		if (!qsrc(i.index, size))
			return run_error;

		i.flags |= busy;
	}

	// If we have busy source buffers, try to dequeue one.
	if (is_src_busy()) {
		unsigned index;

		if (!dqsrc(index))
			return run_error;

		source_buffers[index].flags &= ~busy;

		if (ret != run_finished)
			ret = run_active;
	}

	return ret;
}

bool MFCDecoder::queue_dest(ExynosPage *page)
{
	static const std::string msg_prefix("MFCDecoder::queue_dest(): ");

	unsigned index;

	using namespace std;

	auto p = std::find(dest_buffers.begin(), dest_buffers.end(), page);

	if (p == dest_buffers.end()) {
		index = dest_buffers.size();

		cout << msg_prefix << "adding new buffer with index "
			 << index << ".\n";

		dest_buffers.push_back(page);
	} else {
		index = p - dest_buffers.begin();
	}

	if (index >= dest_buffer_count) {
		cerr << msg_prefix << "index out of bounds.\n";
		return false;
	}

	if (!qdst(index, page->get_prime_fd()))
		return false;

	dest_num_queued++;

	return true;
}

ExynosPage* MFCDecoder::dequeue_dest()
{
	static const std::string msg_prefix("MFCDecoder::dequeue_dest(): ");

	unsigned index;
	bool finished;

	using namespace std;

	if (!dqdst(index, finished))
		return nullptr;

	try {
		auto p = dest_buffers.at(index);

		dest_num_queued--;
		return p;
	}
	catch (exception &e) {
		cerr << msg_prefix << "unknown buffer destinaton buffer deqeued.\n";
		return nullptr;
	}

	return nullptr;
}

bool MFCDecoder::set_source_v4l2()
{
	static const std::string msg_prefix("MFCDecoder::set_source_v4l2(): ");

	using namespace std;

	struct v4l2_format fmt;

	zerostruct(&fmt);
	fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	fmt.fmt.pix_mp.pixelformat = parser->get_codec();
	fmt.fmt.pix_mp.plane_fmt[0].sizeimage = source_buffer_size;
	fmt.fmt.pix_mp.num_planes = source_plane_count;

	if (ioctl(fd, VIDIOC_S_FMT, &fmt)) {
		cerr << msg_prefix << "setting V4L2 data format failed (errno="
			 << errno << ").\n";
		return false;
	}

	struct v4l2_requestbuffers reqbuf;

	zerostruct(&reqbuf);
	reqbuf.count = source_buffers.size();
	reqbuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	reqbuf.memory = V4L2_MEMORY_DMABUF;

	if (ioctl(fd, VIDIOC_REQBUFS, &reqbuf)) {
		cerr << msg_prefix << "V4L2 memory mapping init failed (errno="
			 << errno << ").\n";
		return false;
	}

	cout << msg_prefix << "got " << reqbuf.count << " source buffers (requested="
		 << source_buffers.size() << ")\n";

	source_buffers.resize(reqbuf.count);

	return true;
}

bool MFCDecoder::set_dest_v4l2(videoinfo &vi)
{
	static const std::string msg_prefix("MFCDecoder::set_dest_v4l2(): ");

	using namespace std;

	struct v4l2_format fmt;
	struct v4l2_requestbuffers reqbuf;
	struct v4l2_control ctrl;
	struct v4l2_crop crop;

	zerostruct(&fmt);
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

	// FIXME: Reading the format if we haven't enabled streaming on the
	// OUTPUT side. Apparantly doing this start kicks off the hardware?
	if (ioctl(fd, VIDIOC_G_FMT, &fmt)) {
		cerr << msg_prefix << "failed to read format (errno="
			 << errno << ").\n";
		return false;
	}

	vi.w = fmt.fmt.pix_mp.width;
	vi.h = fmt.fmt.pix_mp.height;
	vi.pixel_format = fmt.fmt.pix_mp.pixelformat;

	dest_plane_size[0] = fmt.fmt.pix_mp.plane_fmt[0].sizeimage;
	dest_plane_size[1] = fmt.fmt.pix_mp.plane_fmt[1].sizeimage;
	std::memcpy(vi.buffer_size, dest_plane_size, sizeof(unsigned) * 2);

	zerostruct(&ctrl);
	ctrl.id = V4L2_CID_MIN_BUFFERS_FOR_CAPTURE;

	if (ioctl(fd, VIDIOC_G_CTRL, &ctrl)) {
		cerr << msg_prefix << "failed to get the number of buffers required by MFC.\n";
		return false;
	}

	dest_buffer_count = ctrl.value + dest_extra_buffer_count;

	dest_queue_min = ctrl.value;
	dest_num_queued = 0;

	zerostruct(&crop);
	crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

	if (ioctl(fd, VIDIOC_G_CROP, &crop)) {
		cerr << msg_prefix << "failed to get crop information (errno="
			 << errno << ").\n";
		return false;
	}

	vi.crop_w = crop.c.width;
	vi.crop_h = crop.c.height;
	vi.crop_left = crop.c.left;
	vi.crop_top = crop.c.top;

	zerostruct(&reqbuf);
	reqbuf.count = dest_buffer_count;
	reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	reqbuf.memory = V4L2_MEMORY_DMABUF;

	if (ioctl(fd, VIDIOC_REQBUFS, &reqbuf)) {
		cerr << msg_prefix << "V4L2 memory mapping init failed (errno="
			 << errno << ").\n";
		return false;
	}

	cout << msg_prefix << "got " << reqbuf.count << " destination buffers (requested="
		 << dest_buffer_count << ", extra=" << dest_extra_buffer_count
		 << ").\n";

	dest_buffer_count = reqbuf.count;

	return true;
}

bool MFCDecoder::is_src_busy() const
{
	for (auto &i : source_buffers) {
		if (i.flags & busy)
			return true;
	}

	return false;
}

bool MFCDecoder::qsrc(unsigned index, unsigned frame_size)
{
	static const std::string msg_prefix("MFCDecoder::qsrc(): ");

	using namespace std;

	if (index >= source_buffers.size()) {
		cerr << msg_prefix << "index out of bounds.\n";
		return false;
	}

	struct v4l2_buffer qbuf;
	struct v4l2_plane planes[source_plane_count];

	zerostruct(&qbuf);
	qbuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	qbuf.memory = V4L2_MEMORY_DMABUF;
	qbuf.index = index;
	qbuf.length = source_plane_count;
	qbuf.m.planes = planes;

	zerostruct(&planes, source_plane_count);
	planes[0].m.fd = source_buffers[index].fd;
	planes[0].length = source_buffer_size;
	planes[0].bytesused = frame_size;

	if (ioctl(fd, VIDIOC_QBUF, &qbuf)) {
		cerr << msg_prefix << "failed to queue source with index "
				  << index << " (errno=" << errno << ").\n";
		return false;
	}

	cout << msg_prefix << "queued source with index " << index << ".\n";

	return true;
}

bool MFCDecoder::qdst(unsigned index, int dma_fd)
{
	static const std::string msg_prefix("MFCDecoder::qdst(): ");

	using namespace std;

	struct v4l2_buffer qbuf;
	struct v4l2_plane planes[dest_plane_count];

	zerostruct(&qbuf);
	qbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	qbuf.memory = V4L2_MEMORY_DMABUF;
	qbuf.index = index;
	qbuf.length = dest_plane_count;
	qbuf.m.planes = planes;

	zerostruct(planes, dest_plane_count);
	planes[0].m.fd = dma_fd;
	planes[0].length = dest_plane_size[0];
	planes[1].m.fd = dma_fd;
	planes[1].length = dest_plane_size[1];
	planes[0].data_offset = 0;
	planes[1].data_offset = dest_plane_size[0];

	if (ioctl(fd, VIDIOC_QBUF, &qbuf)) {
		cerr << msg_prefix << "failed to queue destination with index "
			 << index << ".\n";
		return false;
	}

	cout << msg_prefix << "queued destination with index "
		 << index << ".\n";

	return true;
}

bool MFCDecoder::dqsrc(unsigned &index)
{
	static const std::string msg_prefix("MFCDecoder::dqsrc(): ");

	using namespace std;

	struct v4l2_buffer qbuf;
	struct v4l2_plane planes[source_plane_count];

	zerostruct(&qbuf);
	qbuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	qbuf.memory = V4L2_MEMORY_DMABUF;
	qbuf.length = source_plane_count;
	qbuf.m.planes = planes;

	zerostruct(planes, source_plane_count);

	if (ioctl(fd, VIDIOC_DQBUF, &qbuf)) {
		cerr << msg_prefix << "failed to dequeue source (errno="
			 << errno << ").\n";
		return false;
	}

	index = qbuf.index;

	cout << msg_prefix << "dequeued source with index "
		 << index << ".\n";

	return true;
}

bool MFCDecoder::dqdst(unsigned &index, bool &finished)
{
	static const std::string msg_prefix("MFCDecoder::dqdst(): ");

	using namespace std;

	struct v4l2_buffer qbuf;
	struct v4l2_plane planes[dest_plane_count];

	zerostruct(&qbuf);
	qbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	qbuf.memory = V4L2_MEMORY_DMABUF;
	qbuf.length = dest_plane_count;
	qbuf.m.planes = planes;

	zerostruct(planes, dest_plane_count);

	if (ioctl(fd, VIDIOC_DQBUF, &qbuf)) {
		cerr << msg_prefix << "failed to dequeue destination (errno="
			 << errno << ").\n";
		return false;
	}

	finished = (planes[0].bytesused == 0);
	index = qbuf.index;

	cout << msg_prefix << "dequeued destination with index "
		 << index << ".\n";

	return true;
}

bool MFCDecoder::stream(enum buffer_type type, bool enable)
{
	static const std::string msg_prefix("MFCDecoder::stream(): ");

	using namespace std;

	unsigned long request;
	int v4l2_buffer_type;

	request = (enable ? VIDIOC_STREAMON : VIDIOC_STREAMOFF);

	switch (type) {
	case source:
		v4l2_buffer_type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
		break;

	case destination:
	default:
		v4l2_buffer_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		break;
	}

	if (ioctl(fd, request, &v4l2_buffer_type))
		return false;

	cout << msg_prefix << v4l2_stream_to_string(request) << " streaming for "
		 << v4l2_type_to_string(v4l2_buffer_type) << " queue.\n";

	return true;
}

bool MFCDecoder::stop()
{
	static const std::string msg_prefix("MFCDecoder::stop(): ");

	using namespace std;

	struct v4l2_decoder_cmd dcmd;

	zerostruct(&dcmd);
	dcmd.cmd = V4L2_ENC_CMD_STOP;

	if (ioctl(fd, VIDIOC_DECODER_CMD, &dcmd)) {
		cerr << msg_prefix << "failed to stop decoder (errno="
			 << errno << ").\n";
		return false;
	}

	return true;
}
