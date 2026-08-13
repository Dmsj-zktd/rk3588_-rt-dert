#include "gst_io.h"
#include "logger.h"

#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/video/video.h>

#include <cstdio>
#include <cstring>
#include <cctype>
#include <vector>
#include <mutex>

// ============================================================================
// GStreamer 一次性初始化
// ============================================================================
static std::once_flag g_gst_init_flag;

static void ensure_gst_init()
{
	std::call_once(g_gst_init_flag, []()
	{
		gst_init(nullptr, nullptr);
	});
}

// ============================================================================
// GstVideoReader：MPP 硬件解码
// ============================================================================
struct GstVideoReader::Impl
{
	GstElement* pipeline    = nullptr;
	GstElement* sink        = nullptr;
	double      fps         = 30.0;
	int         width       = 0;
	int         height      = 0;
	bool        opened      = false;
};

GstVideoReader::GstVideoReader() : impl_(new Impl()) {}
GstVideoReader::~GstVideoReader() { release(); delete impl_; }

bool GstVideoReader::open(const std::string& path)
{
	ensure_gst_init();
	release();

	// 注意：不用 videoconvert 转 BGR（实测 720p 软转换 ~60ms/帧，成为瓶颈）；
	// 直接拉 NV12/BGR 原始帧，用 OpenCV NEON 版 cvtColor 转 BGR（~1.5ms/帧）。
	std::string ext = path.substr(path.find_last_of('.') + 1);
	for (auto& c : ext) c = (char)tolower((unsigned char)c);
	std::string desc;
	if (ext == "jpg" || ext == "jpeg")
	{
		// 图片：MPP JPEG 硬件解码
		desc = "filesrc location=\"" + path + "\" ! jpegparse ! mppjpegdec ! appsink name=sink";
	}
	else if (ext == "png")
	{
		desc = "filesrc location=\"" + path + "\" ! pngdec ! appsink name=sink";
	}
	else
	{
		// 视频：MPP H.264 硬件解码
		// arm-afbc=false：禁用 AFBC 压缩输出（否则按线性 NV12 读取会出现顶部绿/紫条）
		desc = "filesrc location=\"" + path
		       + "\" ! qtdemux ! h264parse ! mppvideodec arm-afbc=false ! appsink name=sink";
	}
	GError* err = nullptr;
	GstElement* pipe = gst_parse_launch(desc.c_str(), &err);
	if (!pipe)
	{
		LOG(MOD_PIPELINE, LOG_ERROR) << "GstVideoReader: parse failed: "
		          << (err ? err->message : "unknown") << "\n";
		if (err) g_error_free(err);
		return false;
	}

	GstElement* sink = gst_bin_get_by_name(GST_BIN(pipe), "sink");
	if (!sink)
	{
		LOG(MOD_PIPELINE, LOG_ERROR) << "GstVideoReader: no appsink\n";
		gst_object_unref(pipe);
		return false;
	}

	impl_->pipeline = pipe;
	impl_->sink = sink;

	g_object_set(G_OBJECT(sink), "sync", FALSE, "drop", FALSE, "max-buffers", 4, NULL);

	if (gst_element_set_state(pipe, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
	{
		LOG(MOD_PIPELINE, LOG_ERROR) << "GstVideoReader: PLAYING failed\n";
		release();
		return false;
	}
	impl_->opened = true;
	return true;
}

bool GstVideoReader::read(cv::Mat& frame)
{
	if (!impl_->opened || !impl_->sink) return false;

	GstSample* sample = gst_app_sink_try_pull_sample(GST_APP_SINK(impl_->sink), 2 * GST_SECOND);
	if (!sample) return false;

	GstBuffer* buf = gst_sample_get_buffer(sample);
	GstCaps* caps = gst_sample_get_caps(sample);
	GstMapInfo map;
	if (!gst_buffer_map(buf, &map, GST_MAP_READ))
	{
		gst_sample_unref(sample);
		return false;
	}

	int w = 0, h = 0;
	const char* fmt = nullptr;
	if (caps)
	{
		GstStructure* s = gst_caps_get_structure(caps, 0);
		static bool caps_logged = false;
		if (!caps_logged)
		{
			gchar* caps_str = gst_caps_to_string(caps);
			LOG(MOD_PIPELINE, LOG_INFO) << "appsink caps=" << (caps_str ? caps_str : "?") << "\n";
			if (caps_str) g_free(caps_str);
			caps_logged = true;
		}
		gst_structure_get_int(s, "width", &w);
		gst_structure_get_int(s, "height", &h);
		fmt = gst_structure_get_string(s, "format");
		int fr_n = 0, fr_d = 0;
		if (gst_structure_get_fraction(s, "framerate", &fr_n, &fr_d) && fr_n > 0 && fr_d > 0)
		{
			impl_->fps = (double)fr_n / fr_d;
		}
	}
	if (w > 0 && h > 0)
	{
		impl_->width = w;
		impl_->height = h;
	}

	if (w > 0 && h > 0 && fmt)
	{
		cv::Mat bgr;
		std::string f = fmt;
		if (f == "NV12" && map.size >= (size_t)w * h * 3 / 2)
		{
			// 【修复】mppvideodec 会把高度按 16 对齐（如 1080→1088），caps 只报 1080；
			// 若按 1080 找 UV 平面，会把 Y 的 8 行当色度 → 顶部 16 行绿条。
			// 优先使用 GstVideoMeta 的真实平面偏移/stride，否则按缓冲区大小推断。
			const unsigned char* yp = map.data;
			const unsigned char* uvp = map.data + (size_t)w * h;
			int stride_y = w;
			int stride_uv = w;
			GstVideoMeta* vmeta = gst_buffer_get_video_meta(buf);
			if (vmeta)
			{
				yp = map.data + vmeta->offset[0];
				uvp = map.data + vmeta->offset[1];
				stride_y = vmeta->stride[0];
				stride_uv = vmeta->stride[1];
			}
			else if (map.size > (size_t)w * h * 3 / 2)
			{
				int total_rows = (int)(map.size / w);
				if (total_rows > h * 3 / 2)
				{
					uvp = map.data + (size_t)((total_rows * 2) / 3) * w;
				}
			}
			// 组装紧凑 NV12（Y + UV 按真实 stride 逐行拷贝）
			std::vector<unsigned char> nv12buf((size_t)w * h * 3 / 2);
			unsigned char* dst = nv12buf.data();
			for (int r = 0; r < h; ++r)
			{
				std::memcpy(dst + (size_t)r * w, yp + (size_t)r * stride_y, w);
			}
			unsigned char* uvdst = dst + (size_t)w * h;
			for (int r = 0; r < h / 2; ++r)
			{
				std::memcpy(uvdst + (size_t)r * w, uvp + (size_t)r * stride_uv, w);
			}
			cv::Mat nv12(h * 3 / 2, w, CV_8UC1, nv12buf.data());
			cv::cvtColor(nv12, bgr, cv::COLOR_YUV2BGR_NV12);
		}
		else if (f == "BGR" && map.size >= (size_t)w * h * 3)
		{
			cv::Mat tmp(h, w, CV_8UC3, map.data);
			tmp.copyTo(bgr);
		}
		else if (f == "RGB" && map.size >= (size_t)w * h * 3)
		{
			cv::Mat tmp(h, w, CV_8UC3, map.data);
			cv::cvtColor(tmp, bgr, cv::COLOR_RGB2BGR);
		}
		if (!bgr.empty()) bgr.copyTo(frame);
	}
	else
	{
		frame.release();
	}

	gst_buffer_unmap(buf, &map);
	gst_sample_unref(sample);
	return !frame.empty();
}

bool GstVideoReader::isOpened() const { return impl_->opened; }
double GstVideoReader::fps() const    { return impl_->fps; }
int GstVideoReader::width() const     { return impl_->width; }
int GstVideoReader::height() const    { return impl_->height; }

void GstVideoReader::release()
{
	if (impl_->pipeline)
	{
		gst_element_set_state(impl_->pipeline, GST_STATE_NULL);
		if (impl_->sink) gst_object_unref(impl_->sink);
		gst_object_unref(impl_->pipeline);
		impl_->pipeline = nullptr;
		impl_->sink = nullptr;
	}
	impl_->opened = false;
	impl_->width = 0;
	impl_->height = 0;
}

// ============================================================================
// GstVideoWriter：MPP 硬件编码（H.264 MP4）
// ============================================================================
struct GstVideoWriter::Impl
{
	GstElement* pipeline = nullptr;
	GstElement* src      = nullptr;
	double      fps      = 30.0;
	cv::Size    size     = {0, 0};
	guint64     frame_index = 0;
	bool        opened   = false;
};

GstVideoWriter::GstVideoWriter() : impl_(new Impl()) {}
GstVideoWriter::~GstVideoWriter() { release(); delete impl_; }

bool GstVideoWriter::open(const std::string& path, double fps, cv::Size size)
{
	ensure_gst_init();
	release();

	impl_->fps = fps > 0 ? fps : 30.0;
	impl_->size = size;
	impl_->frame_index = 0;

	char desc[1536];
	snprintf(desc, sizeof(desc),
	         "appsrc name=src ! video/x-raw,format=BGR,width=%d,height=%d,framerate=%d/1 "
	         "! mpph264enc ! h264parse ! mp4mux ! filesink location=\"%s\"",
	         size.width, size.height, (int)(impl_->fps + 0.5), path.c_str());

	GError* err = nullptr;
	GstElement* pipe = gst_parse_launch(desc, &err);
	if (!pipe)
	{
		LOG(MOD_PIPELINE, LOG_ERROR) << "GstVideoWriter: parse failed: "
		          << (err ? err->message : "unknown") << "\n";
		if (err) g_error_free(err);
		return false;
	}

	GstElement* src = gst_bin_get_by_name(GST_BIN(pipe), "src");
	if (!src)
	{
		LOG(MOD_PIPELINE, LOG_ERROR) << "GstVideoWriter: no appsrc\n";
		gst_object_unref(pipe);
		return false;
	}
	impl_->pipeline = pipe;
	impl_->src = src;

	GstCaps* caps = gst_caps_new_simple("video/x-raw",
	                                    "format", G_TYPE_STRING, "BGR",
	                                    "width", G_TYPE_INT, size.width,
	                                    "height", G_TYPE_INT, size.height,
	                                    "framerate", GST_TYPE_FRACTION, (int)(impl_->fps + 0.5), 1, NULL);
	g_object_set(G_OBJECT(src), "caps", caps, "is-live", FALSE,
	             "format", GST_FORMAT_TIME, "stream-type", GST_APP_STREAM_TYPE_STREAM, NULL);
	gst_caps_unref(caps);

	if (gst_element_set_state(pipe, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
	{
		LOG(MOD_PIPELINE, LOG_ERROR) << "GstVideoWriter: PLAYING failed\n";
		release();
		return false;
	}
	impl_->opened = true;
	return true;
}

bool GstVideoWriter::isOpened() const { return impl_->opened; }

bool GstVideoWriter::write(const cv::Mat& frame)
{
	if (!impl_->opened || !impl_->src) return false;

	cv::Mat cont = frame.isContinuous() ? frame : frame.clone();
	GstBuffer* buf = gst_buffer_new_allocate(NULL, cont.total() * cont.elemSize(), NULL);
	GstMapInfo map;
	gst_buffer_map(buf, &map, GST_MAP_WRITE);
	std::memcpy(map.data, cont.data, map.size);
	gst_buffer_unmap(buf, &map);

	guint64 f = (guint64)(impl_->fps + 0.5);
	GST_BUFFER_PTS(buf) = gst_util_uint64_scale(impl_->frame_index, GST_SECOND, f);
	GST_BUFFER_DTS(buf) = GST_BUFFER_PTS(buf);
	GST_BUFFER_DURATION(buf) = gst_util_uint64_scale(GST_SECOND, 1, f);
	impl_->frame_index++;

	GstFlowReturn ret = gst_app_src_push_buffer(GST_APP_SRC(impl_->src), buf);
	return ret == GST_FLOW_OK;
}

void GstVideoWriter::release()
{
	if (impl_->pipeline)
	{
		if (impl_->src)
		{
			gst_app_src_end_of_stream(GST_APP_SRC(impl_->src));
			GstBus* bus = gst_element_get_bus(impl_->pipeline);
			GstMessage* msg = gst_bus_timed_pop_filtered(bus, 5 * GST_SECOND,
			                (GstMessageType)(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
			if (msg) gst_message_unref(msg);
			gst_object_unref(bus);
		}
		gst_element_set_state(impl_->pipeline, GST_STATE_NULL);
		if (impl_->src) gst_object_unref(impl_->src);
		gst_object_unref(impl_->pipeline);
		impl_->pipeline = nullptr;
		impl_->src = nullptr;
	}
	impl_->opened = false;
	impl_->frame_index = 0;
}
