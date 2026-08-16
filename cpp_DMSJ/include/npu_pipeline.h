#pragma once

#include <vector>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <string>
#include <map>
#include <atomic>
#include <chrono>
#include <opencv2/opencv.hpp>

#include "types.h"
#include "rknn_detector.h"
#include "rknn_api.h"
#include "gst_io.h"

// ============================================================================
// BoundedSafeQueue（有界阻塞队列）
// ============================================================================
/**
 * @brief 线程安全的有界阻塞队列，支持毒丸（nullptr）终止。
 * @tparam T 存储的元素类型
 */
template <typename T>
class BoundedSafeQueue
{
	private:
		std::queue<T>        queue_;
		mutable std::mutex   mtx_;
		std::condition_variable cv_not_full_;
		std::condition_variable cv_not_empty_;
		size_t               capacity_;
		std::atomic<bool>    is_running_{true};

	public:
		explicit BoundedSafeQueue(size_t cap) : capacity_(cap) {}

		/** @brief 关闭队列，唤醒所有等待线程。 */
		void shutdown()
		{
			is_running_ = false;
			cv_not_full_.notify_all();
			cv_not_empty_.notify_all();
		}

		/**
		 * @brief 入队一个任务，若队列满则阻塞。
		 * @param task 要推入的任务（支持移动语义）
		 * @note 若队列已关闭，任务会被丢弃。
		 */
		void push(T task)
		{
			std::unique_lock<std::mutex> lock(mtx_);
			cv_not_full_.wait(lock, [this]()
			{
				return queue_.size() < capacity_ || !is_running_.load();
			});
			if (!is_running_.load()) return;
			queue_.push(std::move(task));
			lock.unlock();
			cv_not_empty_.notify_one();
		}

		/**
		 * @brief 实时显示用：队满时丢弃最旧元素，绝不阻塞调用方（流水线）。
		 * @param task 要推入的元素
		 */
		void push_drop_oldest(T task)
		{
			std::unique_lock<std::mutex> lock(mtx_);
			if (capacity_ > 0 && queue_.size() >= capacity_)
			{
				queue_.pop();
			}
			queue_.push(std::move(task));
			lock.unlock();
			cv_not_empty_.notify_one();
		}

		/**
		 * @brief 出队一个任务，若队列空则阻塞。
		 * @param task 输出参数，接收元素
		 * @return true 成功取出，false 队列已关闭且为空
		 */
		bool pop(T& task)
		{
			std::unique_lock<std::mutex> lock(mtx_);
			cv_not_empty_.wait(lock, [this]()
			{
				return !queue_.empty() || !is_running_.load();
			});
			if (!is_running_.load() && queue_.empty()) return false;
			task = std::move(queue_.front());
			queue_.pop();
			lock.unlock();
			cv_not_full_.notify_one();
			return true;
		}

		size_t size() const
		{
			std::lock_guard<std::mutex> lock(mtx_);
			return queue_.size();
		}

		size_t capacity() const
		{
			return capacity_;
		}
};

// ============================================================================
// 实时显示帧（任务 8）
// ============================================================================
struct DisplayFrame
{
	int     frame_id    = -1;   //!< 帧序号（用于按序显示）
	int64_t t_post_done = 0;    //!< 后处理完成时刻（μs，显示延迟统计基准）
	cv::Mat img;                //!< 已绘制的检测帧
};

/**
 * @brief 显示帧重排序器：保证输出 frame_id 严格递增，杜绝"画面倒退/往返抖动"。
 *
 * 后处理线程池（-P > 1）完成顺序与帧号不一致，直接按完成顺序播放会出现
 * 帧号回退的往返抖动。本类按 frame_id 缓冲：
 *  - 优先按序弹出连续帧（pop_next）；
 *  - 前序帧缺失且超时/缓冲超容时，前向跳帧到最低可用帧（pop_skip，只前进不后退）。
 */
class DisplayReorderer
{
	public:
		explicit DisplayReorderer(size_t max_pending = 8, int64_t max_wait_us = 120000)
			: max_pending_(max_pending), max_wait_us_(max_wait_us) {}

		/** @brief 入缓冲（同帧号重复时覆盖）。 */
		void push(int id, cv::Mat img, int64_t t_post_done)
		{
			if (img.empty()) return;
			if (next_id_ < 0) next_id_ = id;
			pending_[id] = DisplayFrame{id, t_post_done, std::move(img)};
		}

		/** @brief 若 next_id 已就绪：按序弹出（输出帧号严格递增）。 */
		bool pop_next(DisplayFrame& out, int64_t now_us)
		{
			if (next_id_ < 0) return false;
			auto it = pending_.find(next_id_);
			if (it == pending_.end()) return false;
			out = std::move(it->second);
			pending_.erase(it);
			last_pop_us_ = now_us;
			next_id_++;
			return true;
		}

		/**
		 * @brief 前序缺失且等待超时/缓冲超容：前向跳帧到最低可用帧并弹出。
		 * @return true 表示发生了前向跳帧（输出帧号 > 旧 next_id）。
		 */
		bool pop_skip(DisplayFrame& out, int64_t now_us)
		{
			if (pending_.empty() || next_id_ < 0) return false;
			// 丢弃所有早于已显示序列的过期帧（迟到帧），绝不倒退输出
			while (!pending_.empty() && pending_.begin()->first < next_id_)
			{
				pending_.erase(pending_.begin());
			}
			if (pending_.empty()) return false;
			if (pending_.size() <= max_pending_ &&
			    (last_pop_us_ == 0 || now_us - last_pop_us_ <= max_wait_us_))
			{
				return false;
			}
			auto it = pending_.begin();
			out = std::move(it->second);
			pending_.erase(it);
			last_pop_us_ = now_us;
			next_id_ = out.frame_id + 1;
			return true;
		}

		bool   empty() const
		{
			return pending_.empty();
		}
		size_t size() const
		{
			return pending_.size();
		}
		int    next_id() const
		{
			return next_id_;
		}

	private:
		std::map<int, DisplayFrame> pending_;
		int     next_id_ = -1;
		size_t  max_pending_;
		int64_t max_wait_us_;
		int64_t last_pop_us_ = 0;
};

// ============================================================================
// PipelineManager
// ============================================================================
/**
 * @brief 三级流水线管理器（预处理 → NPU推理 → 后处理/视频输出）。
 *
 * 采用有界队列解耦各阶段，支持多线程并行。
 * 视频输出按帧 ID 顺序写入，确保不会丢帧或乱序。
 */
class PipelineManager
{
	private:
		BoundedSafeQueue<FrameBundlePtr> queue_raw_;
		BoundedSafeQueue<FrameBundlePtr> queue_npu_;
		BoundedSafeQueue<FrameBundlePtr> queue_post_;

		std::vector<std::thread> workers_pre_;
		std::vector<std::thread> workers_npu_;
		std::vector<std::thread> workers_post_;

		std::atomic<bool> is_running_{true};
		// 【健壮性】NPU worker 初始化失败熔断：全部失败时输入直接丢帧，避免队列卡死
		std::atomic<bool> workers_ok_{true};
		std::atomic<int>  npu_init_failures_{0};
		std::string       model_path_;
		int               num_npu_workers_;
		float             conf_thres_ = 0.45f;
		rknn_core_mask    npu_mask_;          // NPU核心掩码

		// 视频输出（延迟初始化）
		std::string       video_output_path_;
		double            video_fps_ = 30.0;
		bool              video_initialized_ = false;
		GstVideoWriter    video_writer_;
		std::map<int, FrameBundlePtr> frame_buffer_;   // 缓存未按序写入的帧
		int               next_write_frame_id_ = 0;     // 期望写入的下一个帧 ID
		std::mutex        writer_mtx_;
		std::atomic<bool> video_flushed_{false};

		// 性能统计
		std::atomic<int64_t> frames_completed_{0};
		std::atomic<int64_t> total_pre_us_{0};
		std::atomic<int64_t> total_npu_us_{0};
		std::atomic<int64_t> total_post_us_{0};

		// 性能统计 ② :: fps
		std::chrono::steady_clock::time_point start_time_;
		bool started_ = false;

		// 实时显示（任务 7.3）
		std::atomic<bool> display_enabled_{false};
		std::atomic<bool> display_quit_{false};
		BoundedSafeQueue<DisplayFrame> display_queue_;
		std::thread display_thread_;
		std::function<void()> quit_callback_;   // 显示窗口按 q/ESC 时通知主流程退出
		std::atomic<int64_t> display_shown_{0};      // 已显示帧数
		std::atomic<int64_t> display_latency_us_{0}; // 显示延迟累计（post-done→show）
		std::atomic<int64_t> display_skips_{0};      // 前向跳帧次数（前序缺失）
		std::atomic<int64_t> display_backwards_{0};  // 倒退显示次数（应为 0）

		// function
		void worker_preprocess();
		void worker_npu_infer(int core_id);
		void worker_postprocess();
		void display_worker();
		void flush_video_buffer();  // 析构时强制写入所有缓存帧

	public:
		/**
		 * @brief 构造流水线管理器。
		 * @param num_pre    预处理线程数
		 * @param num_npu    NPU推理线程数（每个线程独立加载模型）
		 * @param num_post   后处理线程数
		 * @param model_path RKNN 模型文件路径
		 * @param queue_cap  各队列容量
		 * @param conf_thres 检测置信度阈值
		 * @param npu_mask   NPU 核心掩码（多核分配策略）
		 */
		PipelineManager(int num_pre, int num_npu, int num_post,
		                const std::string& model_path,
		                size_t queue_cap = 16,
		                float conf_thres = 0.45f,
		                rknn_core_mask npu_mask = RKNN_NPU_CORE_AUTO);
		~PipelineManager();

		/**
		 * @brief 输入 DMA 帧（零拷贝路径）。
		 * @param frame_id  帧序号（用于输出排序）
		 * @param src_buf   DMA 缓冲（来自 V4L2 或桥接）
		 * @param orig_img  原始图像（用于画框，可为空）
		 */
		void push_dma_frame(int frame_id, const DmaBufferPtr& src_buf, const cv::Mat& orig_img = cv::Mat());

		/** @brief 输入队列是否有空位（相机实时丢帧策略用，任务 8）。 */
		bool raw_queue_has_room() const
		{
			return queue_raw_.size() < queue_raw_.capacity();
		}

		/**
		 * @brief 输入 cv::Mat 图像（兼容路径，内部会转为 DMA）。
		 * @param frame_id  帧序号
		 * @param img       输入图像（BGR）
		 */
		void push_image(int frame_id, const cv::Mat& img);

		/**
		 * @brief 同步单帧图片检测（图片输入模式）。
		 *
		 * 内部完成 cv::Mat → DMA 桥接、RGA 预处理、NPU 推理、后处理画框，
		 * 不经过线程池队列，便于单张图片端到端测时延。
		 * @param src 输入图片（BGR）
		 * @param out 输出图片（绘制检测框与类别标签）
		 * @return true 成功；false 输入为空/模型不可用/推理失败
		 */
		bool detect_image(const cv::Mat& src, cv::Mat& out);

		/**
		 * @brief 设置输出视频文件路径。
		 * @param path 输出文件路径（如 .mp4）
		 * @param fps  输出帧率
		 */
		void set_video_output(const std::string& path, double fps = 30.0);

		/**
		 * @brief 启用/关闭实时检测画面显示（任务 7.3）。
		 * @param enable true 时由专用线程调用 cv::imshow + waitKey 播放检测帧。
		 */
		void set_display(bool enable);

		/** @brief 用户是否通过显示窗口请求退出（按 q 或 ESC）。 */
		bool display_quit_requested() const
		{
			return display_quit_.load();
		}

		/** @brief 注册显示窗口退出回调（通常置全局退出标志）。 */
		void set_quit_callback(std::function<void()> cb)
		{
			quit_callback_ = std::move(cb);
		}

		/** @brief 等待所有队列处理完毕（用于优雅退出）。 */
		void wait_idle();

		/** @brief 打印性能汇总（平均耗时、总 FPS）。 */
		void print_perf_summary();
};
