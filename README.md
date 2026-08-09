# RT-DETR-R18 端侧目标检测系统 (RK3588 RGA_DMA 零拷贝加速)

本项目实现了一套基于 **RT-DETR-R18** 量化模型的端到端目标检测系统，专为 **RK3588** 开发板设计，利用硬件加速和零拷贝技术，实现低延迟、高吞吐的视频流实时检测。系统支持 **V4L2 摄像头** 和 **MP4 视频文件** 输入，输出带检测框的编码视频，并可输出性能统计数据。

---

## 🚀 核心特性

- **端到端零拷贝流水线**  
  从摄像头采集（V4L2 MMAP）→ RGA 硬件预处理（DMA↔DMA）→ NPU 推理（rknn 零拷贝）→ 后处理画框，全程无 CPU 内存拷贝，实现极致吞吐。

- **硬件加速预处理**  
  使用 RK3588 内置的 **RGA（图像处理器）** 完成 `resize + 颜色转换`，单次 `ioctl` 调用完成，CPU 负载极低。

- **多核心并行流水线**  
  预处理器、NPU 推理器、后处理器可配置多线程，充分利用 RK3588 的 8 核 CPU 和 3 核 NPU。

- **灵活的输入源**  
  - V4L2 摄像头（/dev/videoX）零拷贝采集  
  - 本地 MP4 视频文件（通过 OpenCV 解码，桥接至 DMA 内存）  
  - 回退模式：标准 OpenCV 摄像头采集

- **输出视频编码**  
  支持将检测结果绘制于原图后，使用 OpenCV 编码为 MP4/H.264 视频文件。

- **性能监控**  
  自动统计各阶段耗时（预处理、NPU 推理、后处理）及整体 FPS，便于调优。

- **健壮性测试脚本**  
  提供 `test_robustness.sh` 用于环境检查、功能验证和压力测试。

---

## 📦 依赖项

| 依赖         | 版本/说明                                                |
| ------------ | -------------------------------------------------------- |
| **RKNN SDK** | 须包含 `librknnrt.so` 和 `rknn_api.h`，已适配 RK3588 NPU |
| **RGA 库**   | `librga.so`，提供 `wrapbuffer_fd`、`imresize` 等 API     |
| **DRM 库**   | `libdrm.so`，用于 dumb buffer 分配及 PRIME fd 导出       |
| **V4L2**     | 内核支持，设备节点 `/dev/video*`                         |
| **OpenCV**   | 推荐 4.5+，用于视频解码/编码和图像显示                   |
| **编译工具** | GCC 9+，CMake 3.10+，支持 `-fopenmp`                     |

> **注意**：所有库须为 aarch64 版本（板端运行）或交叉编译环境。

---

## 🛠 编译与安装

### 1. 获取源码
```bash
git clone https://your-repo/rtdetr_rk3588_rga_dma.git
cd rtdetr_rk3588_rga_dma
```

### 2. 准备模型文件
将 RT-DETR-R18 量化后的 RKNN 模型（例如 `rtdetr_r18.rknn`）放入项目根目录或指定路径。

### 3. 配置编译选项（CMake）
默认 CMakeLists.txt 已针对 RK3588 板端 SDK 路径进行预设。若需交叉编译，请设置环境变量 `RK3588_TOOLCHAIN`：
```bash
export RK3588_TOOLCHAIN=/path/to/aarch64-rockchip-linux-gnu
```

若库文件路径不同，可修改 CMakeLists.txt 中的：
- `RKNN_RT_LIB_DIR` （包含 librknnrt.so 和 rknn_api.h）
- `RGA_INCLUDE_DIR` （rga.h 所在目录）
- `RGA_LIB_DIR` （librga.so 所在目录）

### 4. 编译
```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

生成两个可执行文件：
- `rtdetr_pipeline`：主程序
- `test_unit`：单元测试

---

## 🎯 使用方法

### 基本命令行格式
```bash
./rtdetr_pipeline -m <模型路径> [选项]
```

### 常用选项

| 参数                 | 说明                                                     | 默认值              |
| -------------------- | -------------------------------------------------------- | ------------------- |
| `-m, --model`        | **必须**，RKNN 模型文件路径                              | -                   |
| `-v, --video`        | 视频文件路径（若指定则从文件读取）                       | 无（使用摄像头）    |
| `-d, --device`       | V4L2 设备路径（摄像头模式）                              | `/dev/video0`       |
| `-W, --width`        | 采集宽度（建议匹配摄像头实际分辨率）                     | 1920                |
| `-H, --height`       | 采集高度                                                 | 1080                |
| `-F, --fps`          | 期望帧率（仅对摄像头有效）                               | 30                  |
| `-o, --output`       | 输出视频文件路径（MP4）                                  | ./result_output.mp4 |
| `-c, --conf`         | 置信度阈值                                               | 0.45                |
| `-n, --npu-workers`  | NPU 推理线程数                                           | 3                   |
| `-p, --pre-workers`  | 预处理线程数                                             | 2                   |
| `-P, --post-workers` | 后处理线程数                                             | 1                   |
| `-q, --queue-cap`    | 各阶段队列容量                                           | 16                  |
| `--npu-cores`        | NPU 核心选择：`auto` / `0` / `1` / `2` / `0,1` / `0,1,2` | `auto`              |
| `--opencv`           | 强制使用 OpenCV 摄像头（回退）                           | 使用 V4L2           |
| `-h, --help`         | 显示帮助                                                 | -                   |

### 示例

#### 1. 使用 V4L2 摄像头（默认）
```bash
./rtdetr_pipeline -m rtdetr_r18.rknn -d /dev/video0 -W 1920 -H 1080 -o output.mp4
```

#### 2. 处理本地视频文件
```bash
./rtdetr_pipeline -m rtdetr_r18.rknn -v test.mp4 -o result.mp4 -c -0.13f  #模型未做归一化
```

#### 3. 仅显示性能（不保存视频）
```bash
./rtdetr_pipeline -m rtdetr_r18.rknn -v test.mp4
```

> **提示**：程序运行中会每隔 30 帧输出各阶段平均耗时及实时 FPS。按 `Ctrl+C` 优雅退出，自动刷新视频缓存。

---

## 🧱 系统架构

整体设计为 **三段式流水线**，通过有界阻塞队列（`BoundedSafeQueue`）连接，各阶段由独立线程池处理。

```
┌────────────┐   raw_q   ┌────────────┐   npu_q   ┌────────────┐   post_q   ┌────────────┐
│  输入源    │ ────────> │ 预处理     │ ────────> │ NPU推理    │ ────────> │ 后处理     │
│ V4L2/文件  │           │ (RGA)      │           │ (RKNN)     │           │ (解码+画框)│
└────────────┘           └────────────┘           └────────────┘           └────────────┘
                                                                                   │
                                                                                   ▼
                                                                           ┌────────────┐
                                                                           │ 视频编码   │
                                                                           │ (OpenCV)   │
                                                                           └────────────┘
```

### 关键模块说明

- **DmaBufferPool**  
  通过 DRM `CREATE_DUMB` 分配物理连续内存，导出 PRIME fd，支持 RGA/NPU/V4L2 间零拷贝共享。

- **RgaPreprocessor**  
  封装 RGA 硬件加速，提供 `DMA→DMA` 和 `cv::Mat→DMA` 两种预处理路径。  
  - `preprocess_dma_to_dma`：直接将源 DMA buffer resize 至 640×640 并转换为 RGB，零 CPU 介入。  
  - `bridge_mat_to_dma`：将 OpenCV `cv::Mat` 拷贝至 DMA 内存（视频文件模式）。

- **V4l2ZeroCopyCapture**  
  V4L2 MMAP 采集 + EXPBUF 导出 PRIME fd，直接生成 `DmaBuffer`，供后续 RGA/NPU 使用。

- **RKNNDetector**  
  封装 RKNN 推理，支持零拷贝输入（`infer_zero_copy`）和传统 `cv::Mat` 输入（`infer_only`），自动识别模型输出 `pred_boxes` 与 `pred_logits`。

- **PipelineManager**  
  管理线程池和队列，协调预处理/NPU 推理/后处理。  
  后处理线程还负责按帧序号缓存并连续写入输出视频，保证无丢帧。

- **后处理**  
  解码 RT-DETR 输出（300 候选框），进行置信度过滤和 NMS（非极大抑制），并在原图上绘制检测框及类别标签。

---

## ⚙️ 性能优化要点

- **零拷贝路径**：摄像头 DMA buffer → RGA DMA buffer → NPU 零拷贝输入，全程无 `memcpy`。
- **RGA 单指令多任务**：`imresize` + `imcvtcolor` 可合并为 `imresize_then_cvtcolor`（当前实现为两步，但已足够高效）。
- **NPU 多核心**：通过 `rknn_set_core_mask` 可分配推理负载到多个 NPU 核心。
- **多线程并行**：预处理、推理、后处理各阶段线程池，最大化 CPU/NPU 重叠。
- **预分配内存池**：DMA 缓冲池循环复用，消除动态分配开销。
- **性能计数器**：各阶段耗时以微秒级精度采集，便于定位瓶颈。

---

## 📊 性能参考（RK3588，-p 2 -n 14 -P 3）

| 阶段                         | 耗时（均值）   |
| ---------------------------- | -------------- |
| RGA 预处理（DMA→DMA）        | ~ 4ms          |
| NPU 推理（RT-DETR-R18 INT8） | ~ 800ms        |
| 后处理 + 画框                | ~ 45ms         |
| 端到端延迟（不含采集）       | ~ 极低         |
| **整体 FPS**（3 核 NPU）     | **~ 15.7 FPS** |

> 实际性能受模型复杂度、输入分辨率、CPU 调度等影响。

---

## 🧪 测试与验证

### 单元测试
编译后运行 `./test_unit`，覆盖：
- DMA buffer 分配/释放/池容量
- RGA 预处理（Mat↔DMA）
- 后处理解码正确性
- 有界队列并发安全

### 健壮性测试脚本
```bash
chmod +x test_robustness.sh
./test_robustness.sh <模型路径>
```
自动检查：
- 环境（DRM/V4L2/库文件）
- 硬件能力（RGA/NPU/内存）
- 端到端功能（单图/摄像头）
- 压力测试（30 秒运行）
- 异常恢复（SIGINT 优雅退出）

---

## 📁 项目目录结构

```
.
├── CMakeLists.txt            # 构建配置
├── include/                  # 公共头文件
│   ├── types.h               # 基础类型、常量
│   ├── drm_alloc.h           # DMA 缓冲池
│   ├── rga_utils.h           # RGA 预处理
│   ├── v4l2_capture.h        # V4L2 零拷贝采集
│   ├── rknn_detector.h       # NPU 推理封装
│   ├── npu_pipeline.h        # 流水线管理器
│   └── postprocess.h         # 后处理
├── src/                      # 实现文件
│   ├── drm_alloc.cc
│   ├── rga_utils.cc
│   ├── v4l2_capture.cc
│   ├── rknn_detector.cc
│   ├── npu_pipeline.cc
│   ├── postprocess.cc
│   └── main.cc               # 主程序入口
├── tests/                    # 测试代码
│   ├── test_unit.cc          # 单元测试
│   └── test_opencv.cc        # OpenCV 信息打印
├── test_robustness.sh        # 自动化测试脚本
└── README.md                 # 本文档
```

---

## ❗ 注意事项

1. **权限问题**  
   - 确保当前用户属于 `video` 和 `render` 组，以便访问 `/dev/video*` 和 `/dev/dri/renderD128`。  
   - 若使用 `card0` 需要 root 权限，建议使用 `renderD128`。

2. **模型兼容性**  
   - 模型需为 RT-DETR-R18 架构，输出为 `pred_boxes`（[300,4]）和 `pred_logits`（[300,10]），类别数固定为 10（VisDrone 数据集）。  
   - 若修改类别数，需同步修改 `types.h` 中的 `NUM_CLASSES`。

3. **RGA 版本**  
   - 不同 RGA 库对 `wrapbuffer_fd` 的支持可能有差异，建议使用 RK3588 官方 SDK 中的版本。

4. **视频编码性能**  
   - OpenCV 编码器可能成为瓶颈，如需更高性能可考虑硬件编码器（如 MPP）。

5. **内存占用**  
   - 默认 DMA 池容量约为 8+8 个缓冲区（源+目标），每个 640×640×3≈1.2MB，加上 V4L2 缓存，总内存占用 < 100MB。

---

## 🤝 贡献与反馈

欢迎提交 Issue 或 Pull Request。若有性能优化或新功能建议，请详细描述测试环境及复现步骤。

---

## 📄 许可证

本项目仅供学习和研究使用。如需商业用途，请自行获取相关依赖库的授权。

---

**Enjoy real-time object detection on RK3588!** 🚀