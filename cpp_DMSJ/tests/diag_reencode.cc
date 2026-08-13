// 临时诊断：GstVideoReader → GstVideoWriter 纯转码（无检测），评估编码质量
// 用法：diag_reencode <in> <out> [rc_mode] [qp] [profile]
#include "gst_io.h"
#include <cstdio>
#include <string>
#include <cstdlib>

int main(int argc, char** argv)
{
	if (argc < 3) return 1;
	GstVideoReader r;
	if (!r.open(argv[1])) return 2;
	cv::Mat f;
	if (!r.read(f)) return 3;
	// 与主程序一致：预读第二帧，让读取器通过 PTS 估算真实帧率（VFR 录屏 60fps）
	cv::Mat f2;
	r.read(f2);
	cv::Size sz(r.width(), r.height());
	double fps = r.fps();
	GstVideoWriter w;
	if (argc > 3)
	{
		std::string rc  = argv[3];
		int qp          = argc > 4 ? atoi(argv[4]) : 26;
		std::string prof = argc > 5 ? argv[5] : "high";
		w.set_encoder_params(rc, qp, prof);
	}
	if (!w.open(argv[2], fps, sz)) return 4;
	int n = 1;
	w.write(f);
	if (!f2.empty())
	{
		w.write(f2);
		n++;
	}
	while (r.read(f) && n < 2000)
	{
		w.write(f);
		n++;
	}
	w.release();
	printf("frames=%d fps=%.2f dims=%dx%d rc=%s qp=%d profile=%s\n",
	       n, fps, sz.width, sz.height,
	       argc > 3 ? argv[3] : "default",
	       argc > 4 ? atoi(argv[4]) : 26,
	       argc > 5 ? argv[5] : "high");
	r.release();
	return 0;
}
