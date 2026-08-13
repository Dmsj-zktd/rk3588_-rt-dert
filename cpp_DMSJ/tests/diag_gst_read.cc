// 临时诊断：用 GstVideoReader 读首帧并导出原始 BGR，供 frame_check.py 分析
#include "gst_io.h"
#include <cstdio>

int main(int argc, char** argv)
{
	if (argc < 3)
	{
		printf("usage: %s <video> <out.rgb>\n", argv[0]);
		return 1;
	}
	GstVideoReader r;
	if (!r.open(argv[1]))
	{
		printf("open fail\n");
		return 2;
	}
	cv::Mat f;
	if (!r.read(f))
	{
		printf("read fail\n");
		return 3;
	}
	printf("dims=%dx%d fps=%.2f\n", f.cols, f.rows, r.fps());
	FILE* fp = fopen(argv[2], "wb");
	if (!fp) return 4;
	fwrite(f.data, 1, f.total() * f.elemSize(), fp);
	fclose(fp);
	r.release();
	return 0;
}
