// 临时诊断：直接查看 appsink 送出的原始 NV12 前 16 行 Y/UV 统计
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <cstdio>
#include <cstring>

int main(int argc, char** argv)
{
	if (argc < 2) return 1;
	gst_init(nullptr, nullptr);
	char desc[1024];
	snprintf(desc, sizeof(desc),
	         "filesrc location=\"%s\" ! qtdemux ! h264parse ! mppvideodec arm-afbc=false ! appsink name=sink",
	         argv[1]);
	GError* err = nullptr;
	GstElement* pipe = gst_parse_launch(desc, &err);
	if (!pipe) return 1;
	GstElement* sink = gst_bin_get_by_name(GST_BIN(pipe), "sink");
	g_object_set(G_OBJECT(sink), "sync", FALSE, "max-buffers", 4, NULL);
	gst_element_set_state(pipe, GST_STATE_PLAYING);
	GstSample* s = gst_app_sink_try_pull_sample(GST_APP_SINK(sink), 3 * GST_SECOND);
	if (!s) return 2;
	GstBuffer* b = gst_sample_get_buffer(s);
	GstMapInfo m;
	gst_buffer_map(b, &m, GST_MAP_READ);
	const unsigned char* d = m.data;
	const int w = 1920, h = 1080;
	int top_bright = 0, all_bright = 0;
	for (int i = 0; i < 16 * w; ++i) if (d[i] > 100) top_bright++;
	for (int i = 0; i < h * w; ++i) if (d[i] > 100) all_bright++;
	printf("mapsize=%zu top16_bright=%d all_bright=%d\n", m.size, top_bright, all_bright);
	const unsigned char* uv = d + h * w;
	unsigned long usum = 0, vsum = 0;
	int n = 0;
	for (int r = 0; r < 16; ++r)
	{
		for (int x = 0; x < w; x += 2)
		{
			usum += uv[r * w + x];
			vsum += uv[r * w + x + 1];
			n++;
		}
	}
	printf("top16 Uavg=%lu Vavg=%lu n=%d\n", usum / n, vsum / n, n);
	gst_buffer_unmap(b, &m);
	gst_sample_unref(s);
	gst_element_set_state(pipe, GST_STATE_NULL);
	gst_object_unref(pipe);
	return 0;
}
