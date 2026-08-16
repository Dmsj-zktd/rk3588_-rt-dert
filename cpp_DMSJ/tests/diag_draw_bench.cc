// ============================================================================
// diag_draw_bench.cc - draw_results 新旧实现微基准（任务 7.2 A/B）
// 编译: g++ -O3 -std=c++17 tests/diag_draw_bench.cc src/postprocess.cc
//        -Iinclude $(pkg-config --cflags --libs opencv4) -o build/diag_draw_bench
// ============================================================================
#include "../include/types.h"
#include "../include/postprocess.h"

#include <opencv2/opencv.hpp>
#include <chrono>
#include <cstdio>
#include <vector>

// 旧实现（提交 9fc657b 之前的 draw_results）：每框随机色 + 固定 2px/0.6 字号 + 逐框 getTextSize
static void draw_results_old(cv::Mat& image, const std::vector<DetectResult>& results)
{
	for (const auto& res : results)
	{
		cv::RNG rng(res.class_id + 100);
		cv::Scalar color(rng.uniform(0, 255), rng.uniform(0, 255), rng.uniform(0, 255));
		cv::rectangle(image, res.box, color, 2);
		char label[256];
		snprintf(label, sizeof(label), "%s %.2f", CLASSES[res.class_id].c_str(), res.score);
		cv::putText(image, label, cv::Point(res.box.x, res.box.y - 5),
		            cv::FONT_HERSHEY_SIMPLEX, 0.6, color, 2);
	}
}

static std::vector<DetectResult> make_results(int n, int w, int h)
{
	std::vector<DetectResult> rs;
	rs.reserve(n);
	for (int i = 0; i < n; ++i)
	{
		DetectResult r;
		r.class_id = i % NUM_CLASSES;
		r.score = 0.5f + (float)(i % 50) / 100.0f;
		// 20% 大目标（画框+文字），80% 小目标（新实现只画细框）
		int bw = (i % 5 == 0) ? 180 : 18;
		int bh = (i % 5 == 0) ? 130 : 16;
		r.box = cv::Rect((i * 97) % (w - bw), (i * 53) % (h - bh), bw, bh);
		rs.push_back(r);
	}
	return rs;
}

template <typename F>
static double bench(F&& f, int iters)
{
	auto t0 = std::chrono::steady_clock::now();
	for (int i = 0; i < iters; ++i) f();
	auto t1 = std::chrono::steady_clock::now();
	return std::chrono::duration<double, std::micro>(t1 - t0).count() / iters;
}

int main()
{
	{
		cv::Mat img(720, 1280, CV_8UC3, cv::Scalar(30, 40, 50));
		for (int n : {0, 10, 40, 100, 200})
		{
			auto rs = make_results(n, 1280, 720);
			for (int i = 0; i < 10; ++i)
			{
				draw_results_old(img, rs);
				draw_results(img, rs);
			}
			double old_us = bench([&] { draw_results_old(img, rs); }, 100);
			double new_us = bench([&] { draw_results(img, rs); }, 100);
			std::printf("720p res=%3d old=%7.1f us new=%7.1f us speedup=%.2fx\n",
			            n, old_us, new_us, old_us / new_us);
		}
	}
	{
		cv::Mat img(1080, 1920, CV_8UC3, cv::Scalar(30, 40, 50));
		auto rs = make_results(40, 1920, 1080);
		for (int i = 0; i < 10; ++i)
		{
			draw_results_old(img, rs);
			draw_results(img, rs);
		}
		double old_us = bench([&] { draw_results_old(img, rs); }, 100);
		double new_us = bench([&] { draw_results(img, rs); }, 100);
		std::printf("1080p res=40 old=%7.1f us new=%7.1f us speedup=%.2fx\n",
		            old_us, new_us, old_us / new_us);
	}
	return 0;
}
