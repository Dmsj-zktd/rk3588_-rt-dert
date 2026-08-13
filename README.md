# RT-DETR-R18 端侧目标检测系统 (RK3588 RGA_DMA 零拷贝加速)

本项目实现了一套基于 **RT-DETR-R18** 量化模型的端到端目标检测系统，专为 **RK3588** 开发板设计，利用硬件加速和零拷贝技术，实现低延迟、高吞吐的视频流实时检测。系统支持 **V4L2 摄像头** 和 **MP4 视频文件** 输入，输出带检测框的编码视频，并可输出性能统计数据。

---

## 🚀 核心特性

- **端到端零拷贝流水线**  
  从摄像头采集（V4L2 MMAP）→ RGA 硬件预处理（DMA↔DMA）→ NPU 推理（rknn 零拷贝）→ 后处理画框，全程无 CPU 内存拷贝，实现极致吞吐（视频文件模式存在一次 Mat→DMA 桥接拷贝）。

- **硬件加速预处理**  
  使用 RK3588 内置的 **RGA（图像处理器）** 完成 `resize + 颜色转换`（当前实现为 `imresize` + `imcvtcolor` 两步），CPU 负载极低。

- **多核心并行流水线**  
  预处理器、NPU 推理器、后处理器可配置多线程，充分利用 RK3588 的 8 核 CPU 和 3 核 NPU。

- **灵活的输入源**  
  - V4L2 摄像头（/dev/videoX）零拷贝采集  
  - 本地视频文件（**GStreamer + RK MPP 硬件解码**，支持 mp4/avi/mkv/ts/webm/hevc/h264 等；失败自动回退 OpenCV 软解）  
  - 本地图片（jpg/jpeg 走 mppjpegdec 硬解，png/bmp/webp 软解，失败回退 OpenCV）  
  - 回退模式：标准 OpenCV 摄像头采集

- **输出视频编码**  
  支持将检测结果绘制于原图后，使用 **GStreamer + RK MPP 硬件编码**（H.264 High profile，恒定质量 fixqp，BT.709 色彩学），替代 OpenCV/FFmpeg 软编。

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
| **GStreamer**| 1.22+（gstreamer-1.0 / app / video）+ rockchipmpp 插件（mppvideodec/mpph264enc） |
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
| `-i, --image`        | 图片文件路径（单张图片检测，与 `-v` 互斥）               | 无                  |
| `-d, --device`       | V4L2 设备路径（摄像头模式）                              | `/dev/video0`       |
| `-W, --width`        | 采集宽度-(暂时废弃，已auto适配src尺寸)                   | 1920                |
| `-H, --height`       | 采集高度-(暂时废弃，已auto适配src尺寸)                   | 1080                |
| `-F, --fps`          | 期望帧率（仅对摄像头有效）                               | 30                  |
| `-o, --output`       | 输出路径（视频 MP4 / 图片 jpg、png）                     | 视频 result_output.mp4 / 图片 out_detect.jpg |
| `-c, --conf`         | 置信度阈值（模型输出未归一化，有效范围 > -1）            | 0.45                |
| `-n, --npu-workers`  | NPU 推理线程数                                           | 3                   |
| `-p, --pre-workers`  | 预处理线程数                                             | 2                   |
| `-P, --post-workers` | 后处理线程数                                             | 1                   |
| `-q, --queue-cap`    | 各阶段队列容量                                           | 16                  |
| `--npu-cores`        | NPU 核心选择：`auto` / `0` / `1` / `2` / `0,1` / `0,1,2` | `auto`              |
| `--opencv`           | 强制使用 OpenCV 摄像头（回退）                           | 使用 V4L2           |
| `-G, --debug`        | 日志模块：0=仅错误+报告；1=[Main]；2=+[RKNN]；…；8=全部   | 全部                |
| `-h, --help`         | 显示帮助                                                 | -                   |

### 示例

#### 1. 使用 V4L2 摄像头（默认）
```bash
./rtdetr_pipeline -m rtdetr_r18.rknn -d /dev/video0 -W 1920 -H 1080 -o output.mp4
```

#### 2. 处理本地视频文件
```bash
./rtdetr_pipeline -m rtdetr_r18.rknn -v test.mp4 -o result.mp4 -c -0.13  # 模型输出未归一化，阈值范围 > -1
```

#### 3. 处理单张图片
```bash
./rtdetr_pipeline -m rtdetr_r18.rknn -i uav.jpg -o result_detect.jpg -c -0.13
```

#### 4. 仅显示性能（不保存视频）
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
                                                                          │ (MPP 硬编) │
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
  解码 RT-DETR 输出（300 候选框），对每个候选框取分数最高的类别并做置信度过滤，然后在原图上绘制检测框及类别标签。

---

## ⚙️ 性能优化要点

- **零拷贝路径**：摄像头 DMA buffer → RGA DMA buffer → NPU 零拷贝输入，全程无 `memcpy`（视频文件模式仅一次 Mat→DMA 桥接拷贝）。
- **RGA 单指令多任务**：`imresize` + `imcvtcolor` 可合并为 `imresize_then_cvtcolor`（当前实现为两步，但已足够高效）。
- **NPU 多核心**：通过 `rknn_set_core_mask` 可分配推理负载到多个 NPU 核心。
- **多线程并行**：预处理、推理、后处理各阶段线程池，最大化 CPU/NPU 重叠。
- **预分配内存池**：DMA 缓冲池循环复用，消除动态分配开销。
- **性能计数器**：各阶段耗时以微秒级精度采集，便于定位瓶颈。

---

## 📊 性能参考（RK3588 开发板实测参考值）

| 阶段                         | 耗时（均值）   |
| ---------------------------- | -------------- |
| 预处理（RGA virt→DMA，mat 输入）        | ~ 5.5ms        |
| NPU 推理（640×640 INT8，3 核饱和）      | ~ 850ms/帧     |
| 后处理 + 画框                | ~ 3ms（硬编卸载后） |
| **整体 FPS**（最佳性能 -p 2 -n 14 -P 3，硬解硬编） | **~ 15.8 FPS**（CPU ~386%、RSS ~1487MB） |
| **整体 FPS**（甜点位 -p 2 -n 8 -P 3，硬解硬编）    | **~ 15.4 FPS**（CPU ~322%、RSS ~919MB） |

> 以上为一次实测的均值参考，实际性能受模型复杂度、输入分辨率、线程/核心配置及 CPU 调度等影响，请以实际测量为准。

---

## 🔧 已知问题与修复记录

- **2026-08-12｜任务 1：图片输入检测 + 检测一致性修复**
  - 新增图片检测模式 `-i/--image`（同步单帧，输出画框图片，`-o` 自定义文件名）。
  - 修复"原版可检、RGA_DMA 版不可检"问题（如 uav.jpg、录屏 mp4）：
    1. 输出张量误选 4200 元素 `pred_logits`，正确为 3000 元素 logits → 改为按形状匹配（300×4 / 300×10，最后匹配者胜），与原版一致；
    2. RGA DMA→DMA 源 stride 对齐 bug（非对齐宽度输入被逐行偏移污染）→ mat 输入改走 CPU `resize+cvtColor` 紧致 DMA 拷贝。
  - 对照实测（cars.mp4，最佳参数）：CPU 平均 510%→453%，峰值 RSS 2753MB→1527MB，FPS 持平 ~15.7。
  - 遗留：V4L2/相机 DMA→DMA 路径同类 stride 隐患，待任务 3 处理。

- **2026-08-12｜任务 2：日志等级化模块化统一管理**
  - 新增 `include/logger.h`：模块级联 + 级别（ERROR<WARN<INFO<DEBUG<TRACE）+ `LOG/LOGT/LOGR` 流式宏，替换全项目 106 处散落打印，零缓冲直写 cerr。
  - 新增 `-G/--DEBUG`：`-G 0` 仅错误+运行报告；`-G N` 开启前 N 个模块（1=Main，2=+RKNN，3=+Pipeline，…，8=全部）；默认全部开启，ERROR 恒打印。
  - 单测 14/14；FPS 无回归（performance 下 15.53 vs 基线 15.68）。

---

- **2026-08-12｜任务 3：RGA_DMA 链路分析与迭代优化**
  - 分析：DRM dumb buffer → PRIME fd → RGA/NPU 三方共享；发现 DRM 源 stride 对齐（如 1360 宽 → 4096）与 RGA `wstride=width` 在 3 字节格式下非整除冲突。
  - 迭代 1：mat 输入改走 RGA virt→DMA（cv::Mat 连续内存 → 640×640 RGB DMA），消除 CPU resize/cvtColor 与桥接 memcpy，失败回退 CPU；CPU 453%→416%、RSS 1527→1492MB、pre 8.16→5.46ms、FPS 15.53→15.85。
  - 迭代 2：相机 DMA→DMA stride 安全化（wstride=实际 stride/bpp，非整除自动回退 CPU 逐行读取）+ 模拟测试；单测 15/15。
  - 基准参数双档：最佳性能 `-p 2 -n 14 -P 3`（FPS ~15.8 / CPU ~430% / RSS ~1500MB）；甜点位 `-p 2 -n 8 -P 3`（FPS ~15.3 / CPU ~365% / RSS ~953MB）。

- **2026-08-13｜任务 4：GStreamer + RK MPP 硬解/硬编替代软解/软编（迭代 1~4）**
  - 迭代 1：新增 `include/gst_io.{h,cc}`——`GstVideoReader`（qtdemux→h264parse→mppvideodec 硬解 NV12，OpenCV NEON cvtColor 转 BGR，规避 videoconvert 60ms/帧瓶颈）与 `GstVideoWriter`（appsrc(BGR)→mpph264enc→mp4mux 硬编）；图片 JPEG 走 mppjpegdec 硬解；OpenCV 全链路回退保留。
  - 迭代 2：修复 1080p 顶部绿条（mpp 高度对齐 1080→1088 的 UV 平面偏移，按 GstVideoMeta 真实 offset/stride 组装 NV12 + 禁用 AFBC）；RGA 并发偶发 `Invalid argument` 用全局互斥锁串行化（实测零开销）。
  - 迭代 3：修复特殊尺寸视频（480×332@60）慢放（PTS 帧率估算）；多格式支持（avi/mkv/ts/webm/hevc/h264/bmp/webp，H.264→H.265 自动重试 + 800ms 数据流验证）。
  - 迭代 4：**特殊尺寸“仍模糊”根治**——根因① mpph264enc 把 appsrc 默认 sRGB 色彩学写入 H.264 VUI（`pc/gbr/sRGB`）导致解码对比度损失 ~13%，修复为显式 `colorimetry=bt709`（输出 `tv/bt709`）；根因② 默认 cbr+baseline 低码率块状伪影，修复为默认 **fixqp + qp-init=22 + profile=high**（恒定质量，`set_encoder_params(rc,qp,profile)` 可配置）。PTS 帧率估算改众数统计。
  - 健壮性：NPU 模型加载失败不再挂起（熔断丢帧 + wait_idle 30s 上限 + 析构先 shutdown 队列），新增回归测试。
  - 实测（-n 8，performance）：特殊 480×332@60 **15.64 FPS / CPU 328% / RSS 822MB / 8.95Mbps**；720p **15.38 / 333% / 966MB / 4.35Mbps**；1080p **15.02 / 352% / 1148MB / 5.17Mbps**；单测 **21/21**。

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

4. **视频编码**  
   - 已默认使用 MPP 硬件编码（H.264 High / fixqp22 / BT.709），恒定质量、按内容自适应码率；可通过 `GstVideoWriter::set_encoder_params()` 调整 rc 模式/QP/profile。

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
