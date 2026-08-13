#pragma once

#include <string>
#include <opencv2/opencv.hpp>

// ============================================================================
// gst_io.h - GStreamer + Rockchip MPP 硬解/硬编封装（任务 4）
//
// GstVideoReader：filesrc → qtdemux → h264parse → mppvideodec → videoconvert
//                 → BGR → appsink（硬件解码，替代 OpenCV/FFmpeg 软解）
// GstVideoWriter：appsrc(BGR) → mpph264enc → h264parse → mp4mux → filesink
//                 （硬件 H.264 编码，替代 OpenCV mp4v 软编）
// ============================================================================

/**
 * @brief MPP 硬件解码视频读取器（接口对齐 cv::VideoCapture 常用子集）。
 */
class GstVideoReader
{
	public:
		GstVideoReader();
		~GstVideoReader();

		/** @brief 打开视频文件（H.264 MP4），失败返回 false。 */
		bool open(const std::string& path);

		/** @brief 读取下一帧（BGR），EOF/失败返回 false。 */
		bool read(cv::Mat& frame);

		bool   isOpened() const;
		double fps() const;
		int    width() const;
		int    height() const;
		void   release();

	private:
		struct Impl;
		Impl* impl_;
};

/**
 * @brief MPP 硬件编码视频写入器（H.264 MP4，接口对齐 cv::VideoWriter 常用子集）。
 */
class GstVideoWriter
{
	public:
		GstVideoWriter();
		~GstVideoWriter();

		/** @brief 打开输出文件（H.264 MP4），失败返回 false。 */
		bool open(const std::string& path, double fps, cv::Size size);

		bool          isOpened() const;
		bool          write(const cv::Mat& frame);
		void          release();

	private:
		struct Impl;
		Impl* impl_;
};
