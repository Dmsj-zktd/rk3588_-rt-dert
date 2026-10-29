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
  - V4L2 摄像头（/dev/videoX）零拷贝采集：支持 BGR3/RGB3/YUYV 格式协商（USB 摄像头如 /dev/video41 Web Camera 走 YUYV，RGA 硬件转 RGB 保持零拷贝）  
  - 本地视频文件（**GStreamer + RK MPP 硬件解码**，支持 mp4/avi/mkv/ts/webm/hevc/h264 等；失败自动回退 OpenCV 软解）  
  - 本地图片（jpg/jpeg 走 mppjpegdec 硬解，png/bmp/webp 软解，失败回退 OpenCV）  
  - 回退模式：标准 OpenCV 摄像头采集

- **输出视频编码**  
  支持将检测结果绘制于原图后，使用 **GStreamer + RK MPP 硬件编码**（H.264 High profile，恒定质量 fixqp，BT.709 色彩学），替代 OpenCV/FFmpeg 软编。

- **性能监控**  
  自动统计各阶段耗时（预处理、NPU 推理、后处理）及整体 FPS，便于调优。

- **实时检测显示**  
  `--display` 开启后由专用线程实时播放检测画面（丢旧保新队列，不阻塞流水线），按 q/ESC 退出；无显示环境自动降级、不中断检测。

- **自适应画框**  
  `draw_results` 线宽/字号随输入分辨率自动调节（720p=2px/0.6 基准，1080p=3px/0.9，4K 封顶 4px/1.2），小目标只画细框不写文字，避免遮挡密集目标。

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
cd /home/neardi/Workspace_Codex/rk3588_-rt-detr/cpp_DMSJ/build
MODEL=/home/neardi/Workspace_Codex/models/RT-DETR-RK3588-Models/.rknn/rtdetr_i8.rknn
./rtdetr_pipeline -m "$MODEL" [选项]
```

### 常用选项

| 参数                 | 说明                                                     | 默认值              |
| -------------------- | -------------------------------------------------------- | ------------------- |
| `-m, --model`        | **必须**，RKNN 模型文件路径                              | -                   |
| `-v, --video`        | 视频文件路径（若指定则从文件读取）                       | 无（使用摄像头）    |
| `-i, --image`        | 图片文件路径（单张图片检测，与 `-v` 互斥）               | 无                  |
| `--image-list`       | 批量图片清单文件（每行一个图片路径，单次加载模型顺序推理，用于离线精度评估） | 无 |
| `--dump-det`         | 导出检测框文件，每行 `<stem> <class_id> <score> <x1> <y1> <x2> <y2>`（配合 `--image-list` 使用） | 无 |
| `-d, --device`       | V4L2 设备路径（摄像头模式）                              | `/dev/video0`       |
| `-W, --width`        | 采集宽度-(暂时废弃，已auto适配src尺寸)                   | 1920                |
| `-H, --height`       | 采集高度-(暂时废弃，已auto适配src尺寸)                   | 1080                |
| `-F, --fps`          | 输入/输出帧率覆盖（视频文件：输入限速+输出fps；摄像头：采集请求+输出fps；未指定=自动/源） | 自动/源            |
| `-o, --output`       | 输出路径（视频 MP4 / 图片 jpg、png）                     | 视频 result_output.mp4 / 图片 out_detect.jpg |
| `-c, --conf`         | 置信度阈值（模型输出未归一化，有效范围 > -1）            | 0.45                |
| `--conf-class`       | 按类置信度阈值，如 `"0:-0.40,3:0.00,9:-0.55"`（未列出的类别沿用 `-c`） | 关闭 |
| `--profile`          | 阈值预设档位：`balanced`（全局阈值，默认行为）/ `recall`（按类标定向量，宏平均 F1 最优） | balanced |
| `-n, --npu-workers`  | NPU 推理线程数                                           | 3                   |
| `-p, --pre-workers`  | 预处理线程数                                             | 2                   |
| `-P, --post-workers` | 后处理线程数                                             | 1                   |
| `-q, --queue-cap`    | 各阶段队列容量                                           | 16                  |
| `--npu-cores`        | NPU 核心选择：`auto` / `0` / `1` / `2` / `0,1` / `0,1,2` | `auto`              |
| `--opencv`           | 强制使用 OpenCV 摄像头（回退）                           | 使用 V4L2           |
| `--display`          | 实时显示检测画面（需 X11/显示环境；q/ESC 退出）          | 关闭                |
| `-G, --debug`        | 日志模块：0=仅错误+报告；1=[Main]；2=+[RKNN]；…；8=全部   | 全部                |
| `-h, --help`         | 显示帮助                                                 | -                   |

### 示例

#### 1. 使用 V4L2 摄像头（默认）
```bash
./rtdetr_pipeline -m "$MODEL" -d /dev/video41 -W 1280 -H 720 -F 30 -p 2 -n 10 -P 3 -c -0.53f --display
```

#### 2. 处理本地视频文件
```bash
./rtdetr_pipeline -m "$MODEL" -v test.mp4 -o result.mp4 -c -0.53f  # 模型输出未归一化，阈值范围 > -1
```

#### 3. 处理单张图片
```bash
./rtdetr_pipeline -m "$MODEL" -i uav.jpg -o result_detect.jpg -c -0.53f
```

#### 4. 仅显示性能（不保存视频）
```bash
./rtdetr_pipeline -m "$MODEL" -v test.mp4 -p 2 -n 10 -P 3 -c -0.53f
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
| 后处理 + 画框                | ~ 0.5ms（任务7.2 优化后，cars.mp4 n8；相机 ~0.01~0.35ms） | 
| **整体 FPS**（最佳性能 -p 2 -n 14 -P 3，硬解硬编） | **~ 15.8 FPS**（CPU ~386%、RSS ~1487MB） |
| **整体 FPS**（甜点位 -p 2 -n 8 -P 3，硬解硬编）    | **~ 15.4 FPS**（CPU ~322%、RSS ~919MB） |
| **摄像头 /dev/video41**（YUYV 720p，源上限 ~10.8fps，-p 2 -n 8 -P 3） | **~ 10.8 FPS**（CPU ~119%、RSS ~780MB） |

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

- **2026-08-14｜任务 4 迭代 5：VFR 帧率误判修复（时长压缩 + 闪烁花屏）**
  - 问题：特殊尺寸 VFR 视频（如 480×332 录屏）输出时长被压（源 21.7s → 输出 9.2s），
    且 2.35× 快进播放产生部分帧部分行“闪烁花屏”观感。
  - 根因：writer 打开时仅依据前两帧 PTS 间隔（前段 60fps burst）→ 误判 60fps；
    PTS 全量统计显示间隔众数/中位数均为 33ms（30fps）、平均 26.6fps，正确输出帧率约 25.5fps。
  - 修复：`GstVideoReader` 新增 `caps_fps_authoritative()` 与 `measured_avg_fps()`；
    VFR 源（caps framerate 为 0/1）采用**两遍法**——先用 MPP 硬解全量扫描统计容器时长/平均帧率，
    再正常处理；CFR 视频（caps 帧率有效）零额外开销。
  - 实测（-n 8）：输出 **26fps / 21.31s**（源 21.7s），FPS **15.63** / CPU **321%** / RSS **827MB**（与基线持平），
    全片抽样 **0 帧花屏**；720p/1080p 回归无变化；单测 **22/22**。

- **2026-08-14｜任务 4 迭代 5 修复：CFR 视频误判 VFR 走两遍法**
  - 问题：迭代 5 声称“CFR 零开销”不实——`caps_fps_valid` 仅在 `read()` 解析 caps 时置位，
    而两遍法判断发生在 `open()` 后、首次 `read()` 前，导致所有视频都被误判为 VFR；
    长视频（如 test_people_small_little.mp4，25fps/300s）需先等 probe 全量解码（约 30s+）
    才打开 VideoWriter，且 fps 用自算值而非视频自带的 25fps。
  - 修复：`try_start_pipeline` 的 verify 出帧阶段直接从 sample 的 caps 解析 framerate
    并置 `caps_fps_valid`；CFR 视频 open 后即判定有效 → 直接用 caps 帧率、零两遍；
    VFR 源（caps 0/1）仍走两遍法。
  - 验证：长 CFR 无两遍、fps=25、writer 立即打开；特殊 VFR 仍 26fps/21.31s；
    cars 30fps/13.27s；单测 **22/22**。

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

---

## 任务6 Stage1：RKNN 输出预分配与索引缓存（2026-08-15）

- RKNNDetector 初始化阶段缓存输入/输出属性与 boxes/logits 索引，逐帧不再重复 rknn_query。
- 输出 buffer 预分配，rknn_outputs_get 使用 is_prealloc=1；FrameBundle 入队时预分配 pred_boxes/pred_logits。
- 单测 24/24 通过；cars.mp4 n8 15.16 FPS、特殊VFR 15.58 FPS，与任务6前基线持平（±1% 噪声范围）。
- 任务6其余候选（零拷贝输入、SRAM、显式核心掩码、专用每核拓扑、动态批处理）经实测无收益或未达目标，未保留。

---

## 任务7：摄像头通路 + draw_results 优化 + 实时播放（2026-08-15/16）

### 7.1 摄像头通路（/dev/video41 Web Camera）
- 协商 YUYV 1280×720（stride 2560，源上限 ~10.8fps）→ DMA→RGA→NPU→画框→MPP 硬编全链路打通；无检测目标 cases 正常（属预期）。
- 修复：V4L2 格式协商链 BGR3→RGB3→YUYV；相机 buffer 生命周期（不提前 munmap/close）；预处理后提前归还相机 buffer（7.6→10.8 FPS）；采集对象生命周期（退出不再 core dump）；read_frame poll 500ms 优雅退出；默认不写视频（仅 -o 指定）。
- 实测（performance，50s 采样）：n8 10.82 FPS / CPU ~119% / RSS 780MB / pre 1.95ms / npu 233ms / post 0.35ms；n14 同 FPS / RSS 1308MB。

### 7.2 draw_results 优化
- 类别固定调色板（去逐框 RNG）、每帧每类文字度量缓存、去掉 putText 抗锯齿、小目标细框不画文字；线宽/字号随分辨率自适应。
- 微基准（同帧同目标 A/B）：720p 密集场景 3.4~3.8x、1080p 3.4x；流水线 post 视频 1.34→0.49ms（-63%）、相机 0.35→0.013ms（-96%）。

### 7.3 实时播放（--display）
- 专用显示线程 + 丢旧保新队列（容量 4，不阻塞流水线）；q/ESC 退出；headless 自动降级。
- 实测：视频 n8 显示 15.27 FPS（无显示 15.37，差 <1%）；相机 n8 显示 10.81 FPS（持平）。
- 单测 **29/29**；视频双基准复测 n8 15.37 FPS/943MB、n14 15.67 FPS/1508MB，与基线持平。

---

## 任务10：权威分析报告（2026-10-06）

### 10.1 新增能力：批量图片推理与检测导出（默认关闭）

- `--image-list <file>`：单次加载模型、顺序处理清单中的图片（复用同一 NPU context，避免逐图重复 `rknn_init`），用于大规模离线评估。
- `--dump-det <file>`：导出检测框，每行 `<stem> <class_id> <score> <x1> <y1> <x2> <y2>`（`score` 为模型原始 logit，非概率；`<stem>` 为图片文件名去扩展名）。
- 配套新增 `PipelineManager::detect_image_reuse()`（懒加载并复用独立 NPU context）；**未指定新参数时行为与既有版本完全一致**。
- 实测批量吞吐：VisDrone val 548 张 **4.74–4.84 img/s**（约 207–211 ms/图，含 JPEG 硬解 + RGA 预处理 + NPU 推理 + 导出），CPU 43.8–45.0%。

### 10.2 系统性能复测（RK3588，performance governor，cars.mp4 720p，MPP 硬解、不写输出视频）

| `-n` | 吞吐 (FPS) | pre / NPU / post (ms) | CPU (%) | 峰值 RSS (MB) | NPU 核占用峰值 | 说明 |
|---|---|---|---|---|---|---|
| 3 | 12.69 | 3.56 / 235.1 / 0.57 | 148.5 | 452 | 86/64/37 | 单 context 时延 235 ms |
| **8** | **15.62 ± 0.05** | 4.51 / 502.5 / 0.49 | **327.7** | **916** | 89/86/83 | 甜点位（四次重复 σ=0.26%） |
| **14** | **15.96** | 5.27 / 840.8 / 0.45 | **395.7** | **1503** | 95/95/93 | 最佳性能（+2% 吞吐，+17% CPU、+64% 内存） |

- 1080p（1920×1080@25）：n8 15.36 FPS / 346.3% / 1093 MB；n14 15.98 FPS / 418.4% / 1618 MB（NPU 75.8 °C，无降频）。
- 摄像头 `/dev/video41`（当前协商 YUYV 1920×1080，stride 3840）：**5.18–5.19 FPS**（USB2 源头受限，计算侧余量约 3×）；V4L2 零拷贝相对 OpenCV 回退 pre 3.69→2.28 ms、post 4.89→3.69 ms。
- 端到端含 MPP H.264 硬编：n8 15.52 FPS（post 0.49→2.28 ms）、n14 15.94 FPS，**吞吐损失 <1%**。
- NPU 利用率：三个核忙比 89–95%，但按 78.8 GFLOPs/帧 ÷ 6 TOPS 折算等效算力利用率仅 **≈21%**；显式 `--npu-cores` 掩码使吞吐下降 **42–49%**（9.15/8.72/7.95 FPS），3 核对 1 核仅 **1.97×** ⇒ **访存/调度受限**，`auto` 为唯一推荐配置。

### 10.3 检测精度（VisDrone2019-DET-val 全量 548 张，板端 INT8 端到端）

| 指标 | 值 | 指标 | 值 |
|---|---|---|---|
| AP (0.50:0.95, rank 口径) | **0.0947** | AP50 | **0.2358** |
| AP75 | 0.0598 | APs / APm / APl | 0.0191 / 0.1268 / 0.1113 |
| 工作点 `-c -0.13` | P 0.5904 / R 0.3589 / F1 0.4464 | 工作点 `-c -0.53` | P 0.4765 / R 0.4090 / F1 0.4402 |
| 阈值截断口径（`-c -0.53`） | AP 0.0897 / AP50 0.2193 | 逐类最优阈值宏平均 F1 | **0.3458**（相对全局 −0.13 **+9.3%**） |

- **AP 与阈值无关**（排序型指标）：`-c` 只决定工作点；报告 AP 请使用 rank 口径。
- 与论文 PC/FP32 参考（UAV-DETR+-R18：AP 30.6 / AP50 50.1，同数据集）相比，端到端保留率 **AP 30.9% / AP50 47.1%**（**下界**，含 INT8 PTQ 与「部署各向异性拉伸 vs 训练 letterbox」等几何/协议差异）。
- 按类最优阈值（IoU≥0.5，F1 最大）：Car 0.00、Bus −0.05、Van −0.25、Truck −0.30、Pedestrian −0.40、Motor −0.55、People/Tricycle/Awning −0.70、Bicycle ≤−0.75。

### 10.4 量化体积

`best-original.pt` 230.7 MiB → `best_rtdetr_npu.onnx` 77.1 MiB → **`rtdetr_i8.rknn` 30.8 MiB**（对 ONNX **2.50×**、对 checkpoint **7.49×** 压缩；1.30 B/参数，含未量化层与元数据）；运行时峰值 RSS 916 MB（占板载 RAM **11.6%**），DMA 池仅 14.7 MB（`-n 8`）。

### 10.5 结论与产物索引

| 产物 | 说明 |
|---|---|
| `标准分析表_[2026-10-06-01].md` | 主报告：平台/协议、性能、精度、量化体积、局限与后续实验计划 |
| `标准分析表_[2026-10-06-02].md` | `-c -0.53f` 专项：rank vs filtered 口径、阈值扫描、逐类 P/R/F1 |
| `标准分析表_[2026-10-06-03].md` | 按类阈值标定：逐类最优阈值与三套策略对照 |
| `tools/visdrone_map.py` | COCO 风格 mAP 评测（101 点插值 + VisDrone 忽略区规则开关） |
| `tools/sweep_thresholds.py` | 全局阈值扫描与工作点分析 |
| `tools/calib_class_thresholds.py` | 按类阈值标定 |
| `tools/val548_detections.txt.gz` / `_c053.txt.gz` | 板端检测导出原始数据（164 400 / 34 806 框），可复算全部精度指标 |

---

## 任务11：精度差距归因（量化 vs 非量化）与按类阈值优化落地（2026-10-07）

### 11.1 新增能力：按类置信度阈值（默认关闭）

- `--profile balanced|recall`：`balanced` = 全局 `-c`（默认，行为与旧版逐行一致）；`recall` = VisDrone val 全量标定的按类阈值向量（宏平均 F1 最优）。
- `--conf-class "0:-0.40,3:0.00,9:-0.55"`：自定义按类阈值（未列出的类别沿用 `-c`）。
- 实现：`postprocess.h/.cc` 新增 `ConfConfig` / `parse_conf_class()` / `apply_conf_profile()`，`decode_rtdetr_output()` 增加可选按类阈值参数；`npu_pipeline` 统一注入三条解码路径；**不传参数时与原逻辑完全一致**（回归验证：24 191 行与 4 447 行逐行相同）；单测 **31/31**。
- `recall` 档位向量：Pedestrian −0.40 / People −0.70 / Bicycle −0.90 / Car 0.00 / Van −0.25 / Truck −0.30 / Tricycle −0.70 / Awning-tricycle −0.80 / Bus −0.05 / Motor −0.55。

### 11.2 精度差距归因（VisDrone val 全量 548 张，同一后处理与忽略区规则）

| 模型 / 来源 | 运行位置 | AP | AP50 | AP75 | 工作点 P / R / F1 |
|---|---|---|---|---|---|
| 论文 UAV-DETR+-R18 | PC/GPU FP32 | 30.6 | 50.1 | — | — |
| FP32 ONNX（同一模型） | PC CPU | 21.80 | 37.25 | 22.08 | 0.802 / 0.475 / 0.597 |
| FP16 RKNN（同一模型） | RK3588 NPU | 20.96 | 36.59 | 20.81 | 0.796 / 0.472 / 0.593 |
| **INT8 RKNN（部署）** | RK3588 NPU | **9.47** | **23.58** | 5.98 | 0.590 / 0.359 / 0.446 |

**三段式分解（论文 → 板端 INT8，共 −21.1 AP / −26.5 AP50）**：

- **INT8 量化：−11.49 AP（54.4%）/ −13.01 AP50（49.1%）** ← 最大单项，需重转换（QAT/更充分校准）回收；
- 平台/精度格式（FP32→FP16）：−0.84 AP（4.0%）——板端链路本身代价很小；
- 模型/评测差异（论文 vs 本工作 FP32 ONNX）：−8.80 AP（41.6%）——与权重版本（`uav-detr-r18_best-original`）及数据集副本/评测协议有关，待逐类 sigmoid 评测闭合；
- **几何预处理不是原因**：letterbox 代理实验（69 张配对）AP 0.1108→0.0651、AP50 0.2563→0.1742，说明"拉伸到 640×640"正是该模型期望的预处理，**不要改成 letterbox**；
- 量化损伤集中在定位：AP75 保留率仅 **28.7%**（AP 45.2%、AP50 64.4%）。

### 11.3 优化落地实测（板端）

| 数据 / 策略 | Precision | Recall | 合并 F1 | 宏平均 F1 |
|---|---|---|---|---|
| val 548：全局 `-c -0.13` | 0.5758 | 0.3594 | 0.4425 | 0.3164 |
| val 548：`--profile recall` | 0.4835 | 0.3934 | 0.4338 | **0.3473（+9.8%）** |
| test-dev 202：全局 `-c -0.13` | 0.5648 | 0.3197 | 0.4083 | 0.2654 |
| test-dev 202：`--profile recall` | 0.4979 | 0.3339 | 0.3997 | **0.2947（+11.0%）** |

- val 上 **10/10 类 F1 全部上升**（+0.004~+0.072），9/10 类召回上升（Car 例外，阈值收紧后召回略降但 F1 仍升）。
- test-dev（未参与标定）增益更大且方向一致 ⇒ 标定未过拟合；代价是合并 F1 −2.1%、FP 增加（下游有误报惩罚时请用 `balanced`）。

### 11.4 产物索引（2026-10-07）

| 产物 | 说明 |
|---|---|
| `标准分析表_[2026-10-07-01].md` | 本任务的完整报告（归因实验设计、三段式分解、优化实现与验证、复现命令） |
| `tools/onnx_dump.py` | FP32 ONNX 同管线推理与检测导出 |
| `tools/verify_profile.py` | 阈值档位工作点评估（逐类 P/R/F1 + 宏平均） |
| `tools/val548_detections_fp32.txt.gz` / `_fp16.txt.gz` / `_recall.txt.gz` / `_balanced.txt.gz` | 归因与优化验证的原始检测导出，可离线复算 |

---

## 任务15：RKNN auto-hybrid INT8 精度恢复（2026-10-25）

### 15.1 结论与模型选型

在 RKNN-Toolkit2 2.3.2 中使用 100 张 VisDrone train 同域图片校准、per-channel、`auto_hybrid=True`、`cos_threshold=0.90`，可在保留 INT8 主导部署形式的同时恢复接近 FP16 的精度。

| 模型 | AP | AP50 | AP75 | FPS（1080p, n8） | 模型体积 |
|------|----|------|------|------------------|----------|
| 论文 UAV-DETR+-R18（FP32） | 0.3060 | 0.5010 | — | — | — |
| FP16 RKNN | **0.2096** | **0.3659** | **0.2081** | 10.02 | 55.68 MB |
| 原纯 INT8 | 0.0947 | 0.2358 | 0.0598 | **15.36** | **32.30 MB** |
| **auto-hybrid c0.90** | **0.2076** | **0.3651** | **0.2055** | **13.00±0.05** | **36.66 MB** |
| auto-hybrid c0.98 | 0.2077 | 0.3651 | 0.2059 | 12.83 | 42.18 MB |
| auto-hybrid 2.5.0 | 0.2080 | 0.3646 | 0.2044 | 12.42 | 35.46 MB |

- c0.90 相对原纯 INT8 提升 AP **+11.29 点**、AP50 **+12.93 点**、AP75 **+14.57 点**；相对 FP16 保留 AP/AP50/AP75 **99.0%/99.8%/98.8%**。
- c0.85 的 AP 骤降至 **0.1004**，属于量化精度悬崖；c0.90 是当前精度、体积与 FPS 的最佳综合拐点。
- c0.90 相对 c0.98 仅低 0.01 AP，但体积缩小 **13.1%**、FPS 提高约 **1.3%**。
- 主部署文件尚未替换；候选模型通过 `-m` 选择：

```bash
-m /home/neardi/Workspace_Codex/models/RT-DETR-RK3588-Models/.rknn/experiments_20261025/rtdetr_i8_auto_channel_c090.rknn
```

### 15.2 阈值与召回率

| 模型 / 策略 | Precision | Recall | F1 | 宏 F1 |
|-------------|-----------|--------|----|-------|
| 纯 INT8，全局 −0.13 | 0.5904 | 0.3589 | 0.4464 | 0.3185 |
| auto-hybrid c0.90，全局 −0.13 | **0.7979** | 0.4687 | 0.5905 | 0.4035 |
| auto-hybrid c0.90，全局 −0.53 | 0.6615 | **0.5611** | **0.6072** | 0.4458 |
| auto-hybrid c0.90，按类标定 | 0.6602 | 0.5550 | 0.6030 | **0.4624** |

按类阈值向量（类别顺序：Pedestrian、People、Bicycle、Car、Van、Truck、Tricycle、Awning-tricycle、Bus、Motor）：

```text
--conf-class "0:-0.55,1:-0.65,2:-1.50,3:-0.10,4:-0.45,5:-0.40,6:-0.90,7:-0.65,8:-0.15,9:-0.50"
```

- val 548 张上，按类阈值相对 −0.13 提升宏 F1 **14.6%**。
- test-dev 固定 202 张外部验证中，宏 F1 从 0.3332 提升到 **0.3833（+15.0%）**，增益方向与 val 一致。
- 若下游对误报更敏感，优先使用全局 `-0.53`；若更重视稀有类召回和类别均衡，使用按类阈值。

### 15.3 转换与阈值工具

| 工具 | 用途 |
|------|------|
| `tools/convert_rknn_variant.py` | 通用 ONNX→RKNN 量化实验入口，支持 auto-hybrid、cos/euc 阈值、per-channel、规则禁用等参数 |
| `tools/run_hybrid_step1.py` / `run_hybrid_step2.py` | 复现旧版手工 hybrid 两步流程及失败边界 |
| `tools/probe_onnx_squeeze.py` | 定位 ONNX `Squeeze` 节点与邻接关系 |
| `tools/prune_rtdetr_outputs.py` | 裁剪 RT-DETR 冗余尾部输出 |
| `tools/export_rtdetr_split_head.py` | 导出 box/score 分离头，用于 split 实验 |
| `tools/alias_quant_cfg.py` | 量化 cfg 张量名映射诊断 |
| `tools/bench_subprocess.py` | 统一采集 wall time、CPU 利用率与峰值 RSS |
| `tools/calib_class_thresholds.py` | 逐类阈值扫描，支持 VisDrone ignored-region |
| `tools/verify_profile.py` | 验证全局/按类阈值工作点的 P/R/F1 与宏 F1 |

完整阈值分析见 `阈值调控检全提升对比_[2026-10-25-01].md`。split ONNX 虽能编译，但运行时输出语义被破坏（`detects=0`），不可部署；`w8a16`、`gdq` 在 RK3588 不支持，`compress_weight=True` 仅减少 128 B。

---

## 任务16：板端高性能模式核验与稳定启动（2026-10-25）

### 16.1 当前状态

板端 `192.168.5.103` 的计算热路径均处于当前内核暴露的最高受支持 OPP：

| 域 | governor | 当前频率 | 最高可用频率 |
|----|----------|----------|--------------|
| CPU A55 / `policy0` | performance | 1.800 GHz | 1.800 GHz |
| CPU A76 / `policy4` | performance | 2.256 GHz | 2.256 GHz |
| CPU A76 / `policy6` | performance | 2.256 GHz | 2.256 GHz |
| NPU | performance | 1.000 GHz | 1.000 GHz |
| DMC | performance | 2.112 GHz | 2.112 GHz |

- 当前 sysfs 不再暴露旧记录中的 A76 `2.352 GHz`；本次以实测最高 OPP **2.256 GHz** 为准。
- 不存在可合法开启的更高 CPU/NPU/DMC 档位；`userspace` 锁频不能超过硬件 OPP，关闭热保护、改电压或非官方超频不在支持范围内。
- GPU 保持 `simple_ondemand`，VOP 保持 `vop2_ondemand`。两者临时锁最高档后的视频吞吐为 **12.804 FPS**，默认档为 **12.829±0.015 FPS（n=4）**，无收益，已恢复默认。
- 空载 SoC/CPU/GPU/NPU 约 **49.9–55.5°C**；高负载短测约 **67.5°C**，低于 **75/85°C** 被动降频点和 **115°C** critical，未发现限频日志。

### 16.2 启动与核验

高性能状态不会在重启后自动持久化。每次重启或进入新的测试会话后执行：

```bash
cd /home/neardi/Workspace_Codex/rk3588_-rt-detr
./tools/rk3588_performance.sh
```

脚本会幂等设置 CPU×3、NPU、DMC 五个 governor，校验 `current_freq == max_freq`，并打印各热区温度；任一计算域未达到最高 OPP 时以非零状态退出。PC 仓库对应文件为 `tools/rk3588_performance.sh`。

---

## 任务17：RKNN 2.5.0 完整扫描与 25 FPS 实时化（2026-10-25）

### 17.1 实时化最佳折中

新增候选：

```text
rtdetr_i25_c088_640_q150_l2.rknn
```

配置：RKNN-Toolkit2 2.5.0、640×640、auto-hybrid c0.88、150 decoder queries、2 层 decoder。

| 模型 | AP | AP50 | AP75 | FPS（n10） | 体积 |
|------|----|------|------|------------|------|
| 最初纯 INT8 部署模型 | 0.0947 | 0.2358 | 0.0598 | 15.36 @n8 | 32.30 MB |
| 2.3.2 c0.90 精度基线 | **0.2076** | **0.3651** | **0.2055** | 13.24 | 36.66 MB |
| **2.5.0 c0.88 q150-l2** | 0.2049 | 0.3556 | 0.2024 | **25.038±0.088** | **28.43 MB** |

相对 c0.90 精度基线：

- FPS **+89.2%**
- 体积 **−22.4%**
- AP **−0.27 点**、AP50 **−0.95 点**
- `-c -0.53` 合并 F1 `0.6072 → 0.6069`，实际工作点基本持平
- CPU `361.6% → 338.4%`、峰值 RSS `1 479 408 → 1 242 608 KiB`

运行时无需覆盖主模型：

```bash
-m /home/neardi/Workspace_Codex/models/RT-DETR-RK3588-Models/.rknn/experiments_20261025/rtdetr_i25_c088_640_q150_l2.rknn \
-p 2 -n 10 -P 3 -c -0.53f
```

### 17.2 Decoder / 输入尺寸结论

| 640 FP32 模型 | Query | Decoder 层 | mAP50-95 | mAP50 |
|---------------|------:|-----------:|---------:|------:|
| 原始 | 300 | 3 | 0.2916 | 0.4715 |
| q150-l3 | 150 | 3 | **0.3039** | **0.4774** |
| q150-l2 | 150 | 2 | 0.2965 | 0.4687 |
| q150-l1 | 150 | 1 | 0.2785 | 0.4462 |
| q175-l2 | 175 | 2 | 0.2938 | 0.4683 |
| q200-l2 | 200 | 2 | 0.2917 | 0.4688 |

- q150-l2 与 q150-l3 的板端检测逐字节一致；l2 严格更小、更快。
- 704 输入 FP32 mAP50-95 为 0.2959，不优于 640，未继续量化。
- 512 输入 FP32 正常，但所有普通 PTQ 配置均崩塌，不可部署。
- 全量微调和极低学习率微调均损害原模型精度，未采用。

### 17.3 运行时兼容与工具

- `RTDETR_INPUT_SIZE` 编译选项默认仍为 640，可用 `-DRTDETR_INPUT_SIZE=512/704` 构建实验二进制。
- RKNN 输出解析支持自动推导 150/300 query，不再把 query 数硬编码为 300。
- 新增 `tools/export_uavdetr_onnx.py`、`tools/finetune_uavdetr.py`、`tools/visdrone_to_yolo.py` 及标签转换单测。
- C++ 单测 **32/32 通过**。
- 完整实验数据见 `模型实时化优化_[2026-10-25-01].md`。

---

## 任务18：模型精选备份与主部署切换（2026-10-25）

### 18.1 当前主模型

主部署路径继续使用：

```text
/home/neardi/Workspace_Codex/models/RT-DETR-RK3588-Models/.rknn/rtdetr_i8.rknn
```

文件内容已切换为综合最佳的 `rtdetr_i25_c088_640_q150_l2.rknn`：

- SHA-256：`3b74ce22219c3c9baabcc280d8ed80f88722dfa641118fbaa0c5cb85dcc01431`
- 大小：28 427 092 B
- 输入：640×640、150 queries、2 层 decoder
- Val AP/AP50/AP75：0.2049 / 0.3556 / 0.2024
- 1080p：25.04±0.09 FPS @n10

原始模型另存为：

```text
.rknn/rtdetr_i8_baseline.rknn
```

SHA-256：`39dee60d1ea3322d3d60382c9a874c500aa57c38d6ad9667643037f81295d878`

### 18.2 板端精选模型

保留目录：

```text
.rknn/curated_20261025/
```

该目录只保留原始 baseline、Q300 Top1/2/4、Q150 Top1/2，共 6 个精选模型。其余板端 RKNN 已清理。

### 18.3 备份与教程

- VM 全量备份：`/home/zktd/Workspace_Codex/backups/rk3588_rtdetr_models_20261025/`
- 33 个语义有效模型：`all_valid/`
- SHA-256 清单：`manifest_all_valid.sha256`
- 6 个精选模型：`curated/`
- 无效/未验证产物：分别隔离在 `excluded_invalid/` 和 `experimental_unverified/`
- 完整环境、命令、阈值、测试和回滚说明见 `RK3588模型备份与使用教程_[2026-10-25-01].md`

主模型运行命令：

```bash
./cpp_DMSJ/build/rtdetr_pipeline \
  -m /home/neardi/Workspace_Codex/models/RT-DETR-RK3588-Models/.rknn/rtdetr_i8.rknn \
  -p 2 -n 10 -P 3 -c -0.53f
```

验证结果：替换后 C++ 单测 **32/32 通过**，主路径实际视频 **25.07 FPS**。旧任务6 Gherkin 中的
`39dee60…` 哈希属于历史 baseline，当前对应文件为 `rtdetr_i8_baseline.rknn`。

---

## 任务20：原始 `.pt` 与 FP32 ONNX 精度一致性（2026-10-29）

### 20.1 结论

原始 `best-original.pt` 的 20 094 584 个参数全部为 `torch.float32`。在
VisDrone val 上使用同一套 640×640 直接拉伸、argmax 解码和忽略区规则时，
原始 `.pt` 与 FP32 ONNX 的精度差没有统计显著性：

| 指标 | 原始 `.pt` | FP32 ONNX | 差值（PT−FP32） |
|------|-----------:|----------:|----------------:|
| AP | **0.2834** | **0.2803** | **+0.0031** |
| AP50 | **0.4737** | **0.4718** | **+0.0019** |
| AP75 | **0.2766** | **0.2732** | **+0.0034** |

该表为 69 张系统抽样（文件名排序后每 8 张取 1 张）。图像级配对
bootstrap 1000 次的 AP 差值 95% CI 为 `[-0.0013, +0.0088]`，跨 0，
因此 **FP32 ONNX 导出没有可辨识的精度损失**。

项目已有全量 548 张 FP32 ONNX 检测重新复算仍为：

```text
AP    = 0.2180
AP50  = 0.3725
AP75  = 0.2208
```

69 张子集与全量值的差异来自抽样图像的难度分布，不能用子集点估计替代
全量 AP。纯 CPU 运行原始 `.pt` 约 0.25 img/s，全量 548 张约需 36 分钟，
因此 `.pt` 使用 69 张直接测量，并以数值等价的 FP32 ONNX 提供全量参考。

### 20.2 新增工具与用法

`tools/rtdetr_pt_dump.py` 支持原始 `.pt` 与 FP32 ONNX 两种后端，输出统一
检测格式 `<stem> <class_id> <score> <x1> <y1> <x2> <y2>`：

```bash
# 原始 PyTorch checkpoint
python tools/rtdetr_pt_dump.py \
  --pt /home/zktd/Workspace_Codex/models/RT-DETR-RK3588-Models/.pt/best-original.pt \
  --images /path/to/VisDrone2019-DET-val/images \
  --out outputs/pt_detections.txt \
  --batch 8 --threads 8

# 同语义 FP32 ONNX 组合输出
python tools/rtdetr_pt_dump.py \
  --onnx /home/zktd/Workspace_Codex/models/RT-DETR-RK3588-Models/.onnx/best_rtdetr_npu.onnx \
  --images /path/to/VisDrone2019-DET-val/images \
  --out outputs/fp32_detections.txt \
  --threads 8

# 统一 mAP 评测
python tools/visdrone_map.py \
  --det outputs/pt_detections.txt \
  --ann /path/to/VisDrone2019-DET-val/annotations \
  --img-dir /path/to/VisDrone2019-DET-val/images \
  --conf 0.468 --ignore-regions
```

ONNX 必须使用图内 `pred_logits [1,300,14]` 组合输出，其中前 4 项为归一化
框，后 10 项为类别分数；不要把未 sigmoid 的其他辅助输出与原始 `.pt`
组合输出混用。

### 20.3 归档与验证

- `tools/pt_sample69_detections.txt.gz`：原始 `.pt` 69 张检测导出。
- `tools/fp32_sample69_detections.txt.gz`：FP32 ONNX 同批检测导出。
- `tools/pt_sample69_map.txt` / `tools/fp32_sample69_map.txt`：两侧完整评测日志。
- `tools/tests/test_rtdetr_pt_dump.py`：预处理、框反变换、无效框过滤和
  ONNX 组合输出选择测试；工具测试 **6/6 通过**。
- 完整指标、模型 SHA-256、环境和 bootstrap 口径见 `METRICS.md` 的
  “2026-10-29 原始 `.pt` 与 FP32 ONNX 精度一致性”章节。
