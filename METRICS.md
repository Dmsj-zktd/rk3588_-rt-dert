# METRICS.md — 指标定义与基线记录

> 规则：所有优化/改动都以数据说话。先定义指标、记录基线，改动后复测对比，确认有效才算完成。
> 基线必须使用当前系统效果最佳参数：`-p 2 -n 14 -P 3 -c -0.13f`。

## 指标定义

| 指标 | 说明 | 采集方式 |
|------|------|----------|
| 整体 FPS | 处理帧数 / 总耗时 | 程序 Performance Summary |
| 预处理耗时 | RGA 预处理平均耗时 | 程序 PerfCounter（μs） |
| NPU 推理耗时 | 单次推理平均耗时 | 程序 PerfCounter（μs） |
| 后处理耗时 | 解码+画框平均耗时 | 程序 PerfCounter（μs） |
| CPU 占用率 | 进程 CPU% = (User+System)/Elapsed×100% | `/usr/bin/time -v` |
| 内存占用 | 进程 Max RSS（KB） | `/usr/bin/time -v` |
| DMA 池内存 | 池分配总量（MB） | 程序 [DRM] 日志累加 |
| 检测质量 | 平均每帧检测目标数、有目标帧占比 | 程序日志 results 计数 |
| 图片模式耗时（任务 1） | 单图端到端耗时 | 新增计时点（待实现） |
| 编码阶段（任务 4） | 软编 vs 硬编 CPU/耗时/文件大小 | 对比日志与输出文件 |

## 基线数据

### 基线 1：默认参数（已记录）

- 命令：`-p 2 -n 3 -P 1 -c -0.13`
- 素材：`cars.mp4`（1280×720 @ 30fps）
- 结果：395 帧，整体 **8.55 FPS**，pre **2.46ms**，npu **349.7ms**，post **31.2ms**，每帧检测 9~16 个目标
- 结论：NPU 推理是绝对瓶颈；CPU/内存占用待补测

### 基线 2：最佳参数（待记录）

- 命令：`-p 2 -n 14 -P 3 -c -0.13f`
- 素材：`cars.mp4`（1280×720 @ 30fps），板端 CPU/NPU/DMC 均 performance 模式
- 结果：385 帧，整体 **15.68 FPS**，pre **4.25ms**，npu **856.6ms**（14 worker 竞争 3 核，单帧时延上升但吞吐饱和），post **45ms**，每帧检测 11~16 个目标
- CPU 占用：平均 **394%**（约 4 核满载），峰值 447%
- 内存占用：峰值 RSS **1650 MB**（14 个 NPU context 各自加载模型）
- 结论：与用户报告的 ~15.7 FPS 吻合；内存 1.65GB 与 CPU 394% 是任务 3 的主要优化对象

## 基线对比总览

| 配置 | FPS | pre | npu | post | CPU 平均 | 峰值 RSS |
|------|-----|-----|-----|------|----------|----------|
| 默认 `-p 2 -n 3 -P 1 -c -0.13` | 8.55 | 2.46ms | 349.7ms | 31.2ms | 未测 | 未测 |
| 最佳 `-p 2 -n 14 -P 3 -c -0.13f` | **15.68** | 4.25ms | 856.6ms | 45ms | 394% | 1650 MB |

## 图片模式基线（任务 1，2026-08-11）

- 单图端到端耗时（含模型加载）：约 **293 ms**（uav.jpg，1360×765）
- 修复后检出（与原版逐项一致）：
  - `uav.jpg`：`-c 0.45` = **38 目标**（原版 38）；`-c -0.13` = 56 目标
  - `0000074_11827_d_0000023.jpg`：`-c 0.45` = **25 目标**（原版 25）
- 输出产物：`out_uav_v3a/b.jpg`、`out_vd_v3.jpg`（画框图片，板端 build/ 下）

## 检测一致性修复记录（2026-08-11）

- **根因 1**：输出张量选择错误——按张量名选到 4200 元素的 `pred_logits`，正确 logits 为 3000 元素张量（300×10）；修复为与原版一致的元素数形状匹配（boxes=300×4、logits=300×10，最后匹配者胜）。
- **根因 2**：RGA DMA→DMA 源 stride 对齐污染——非对齐宽度（1360→DRM stride 4096、480→1472）下 RGA 按 wstride=width 读行导致每行偏移；修复为 mat 输入统一走 CPU `resize+cvtColor` → 紧致 DMA 拷贝（stride=1920 与 wstride 严格一致）。
- 代价：预处理均值由 ~2.5ms 升至 ~5~7.5ms（CPU 负担），整体 FPS 基本不变（15.5~16.1）；CPU 预处理优化留待任务 5。
- **遗留**：V4L2/相机 DMA→DMA 路径仍存在相同 stride 隐患，待任务 3 处理（当前不在验收范围）。

## 修复后 vs 原版对照实测（2026-08-12，cars.mp4，最佳参数 -p 2 -n 14 -P 3）

| 指标 | 原版 rknn_rtdetr_demo | 修复后 workspace | 结论 |
|------|----------------------|------------------|------|
| 平均 CPU 占用 | 510% | 453% | **-11%** |
| 峰值 CPU 占用 | 667% | 487% | **-27%** |
| 峰值 RSS | 2753 MB | 1527 MB | **-45%** |
| 整体 FPS | 15.72 | 15.69 | 持平 |
| 预处理均值 | 原版同为 CPU resize | 8.0ms | 任务 5 优化目标 |

> 注：检出数不做跨版本对比（workspace 用 -c -0.23、原版硬编码 0.45）；CPU/内存为同输入同参数可比数据。

## 任务 2 日志系统验证（2026-08-12）

- 单测：**14/14 通过**（新增 logger 模块级联/级别控制测试）。
- `-G` 行为实测：`-G 0` 仅错误+运行报告；`-G 1` 仅 `[Main]`；`-G 2` = `[Main]+[RKNN]`；`-G 8` = 全部。
- FPS 无回归：performance 模式下默认全日志 **15.53 FPS**（基线 15.68，正常波动）；确认此前 14.9 FPS 波动源于板端 governor 被重置为 schedutil，与日志系统无关。
- 日志开销：流式直写 cerr（无缓冲/堆分配），近似零开销。

## 任务 3：RGA_DMA 链路分析与优化（2026-08-12）

### 架构/原理
- `DmaBufferPool`：DRM `CREATE_DUMB` 分配物理连续内存 → PRIME fd 导出 → mmap 虚拟地址，供 RGA（`wrapbuffer_fd`）与 NPU（rknn 零拷贝 fd）共享。
- RGA 预处理：wrapbuffer 三策略（handle / fd / virtualaddr）；`imresize` + `imcvtcolor` 两步（板端 librga 2.2.0 无 `imresize_then_cvtcolor` 单指令）。
- mat 输入链路（任务 1 修复后）：CPU resize+cvtColor → 紧致 DMA 拷贝（正确但 CPU 占用高）。

### 不足
1. DRM 源缓冲 stride 对齐（如 1360 宽 → 4096）与 RGA `wstride=width`（像素）在 3 字节格式下非整除冲突 → 非对齐宽度输入被逐行偏移污染（任务 1 已绕开，未根治）。
2. mat 路径 CPU 负担：resize+cvtColor ~8ms + Mat→DMA 桥接 memcpy。
3. V4L2/相机 DMA→DMA 路径仍存在同源 stride 隐患（当前不在验收范围，建议后续以 4 字节像素源缓冲根治）。

### 迭代优化 1（本次）：mat 输入改走 RGA virt→DMA
- 源：cv::Mat 连续内存虚拟地址（紧致 stride=width*3，RGA wstride 可精确表达）→ 目标 640×640 RGB DMA（stride=1920）。
- 消除 CPU resize/cvtColor 与桥接 memcpy；失败自动回退 CPU 路径（一次性告警，不刷屏）。
- 正确性：单测 MAE=0.109（RGA vs CPU 参考，阈值 3.0），14/14 通过；uav.jpg @0.45 = 37 目标（原版 38，等价）。

### 指标对比（cars.mp4，最佳参数，performance 模式）

| 指标 | 任务 2 后（CPU 路径） | 任务 3 后（RGA virt→DMA） | 变化 |
|------|----------------------|---------------------------|------|
| 平均 CPU | 453% | **416%** | -8% |
| 峰值 CPU | 487% | **455%** | -7% |
| 峰值 RSS | 1527 MB | **1492 MB** | -35 MB |
| 预处理均值 | 8.16 ms | **5.46 ms** | -33% |
| 整体 FPS | 15.53 | **15.85** | +2% |

> 结论：正确性无损前提下，CPU -8%、内存 -35MB、预处理 -33%、FPS +2%。可继续迭代方向：相机 DMA→DMA 用 4bpp 源根治 stride；视频帧环形缓冲消除 orig_img clone。

### 迭代优化 2（2026-08-12）：相机 DMA→DMA 通路 stride 安全化（模拟测试）+ worker 配置内存/CPU 实测

- **代码修复**：`preprocess_dma_to_dma` 源 wstride 改为 `实际 stride / bpp`（如 1280 宽 stride=3840 → wstride=1280）；当 3 字节格式 stride 非整除（如 1360 宽 → 4096，4080%3!=0）时，跳过 RGA 包装，**安全回退 CPU 按实际 stride 逐行读取 DMA 内存**完成 resize+cvtColor，杜绝逐行偏移污染。
- **模拟测试**：`test_unit` 新增 1360×765 BGR24 模拟相机帧（stride=4096）→ 输出与 CPU 参考 MAE<3 通过；单测 **15/15**。
- **worker 配置对照**（cars.mp4，-p 2 -P 3，performance 模式）：

| -n | 平均 CPU | 峰值 RSS | 整体 FPS | 相对 -n 14 |
|----|----------|----------|----------|------------|
| 6  | 293%     | 783 MB   | 14.77    | CPU -32% / 内存 -48% / FPS -7% |
| **8** | **365%** | **953 MB** | **15.31** | **CPU -15% / 内存 -36% / FPS -3%** |
| 14（最佳性能基准） | 430% | 1500 MB | 15.82 | - |

> 用户决策（2026-08-12）：`-n 8` 正式列为"显著降 CPU/内存甜点位"基准，`-n 14` 继续作为"最佳性能（最高 FPS）"基准，均已写入 AGENTS.md。

## 任务 4：GStreamer + RK MPP 硬解/硬编（2026-08-12）

### 实现
- 新增 `gst_io.{h,cc}`：`GstVideoReader`（视频 `qtdemux→h264parse→mppvideodec`、图片 JPEG `jpegparse→mppjpegdec`、PNG 软解）与 `GstVideoWriter`（`appsrc(BGR)→mpph264enc→h264parse→mp4mux`）。
- 关键发现：`videoconvert` 转 BGR 在 720p 上实测 ~60ms/帧（纯瓶颈）；改为 appsink 直拉 NV12 + OpenCV NEON `cvtColor` 转 BGR（~1.5ms/帧），解码保持纯硬件。
- `mpph264enc` 原生支持 BGR 输入 → 编码端无需 videoconvert；视频输出从 mp4v 软编改为 H.264 硬编。
- 全部保留 OpenCV 回退（reader/图片解码失败自动降级）。

### 指标对比（cars.mp4，performance 模式；软=OpenCV/FFmpeg，硬=MPP）

| 配置 | 解码/编码 | 平均 CPU | 峰值 RSS | 整体 FPS | 后处理均值 |
|------|-----------|----------|----------|----------|------------|
| -n 8 软 | OpenCV/ffmpeg | 365% | 953 MB | 15.30 | ~37ms |
| **-n 8 硬** | **MPP** | **322%** | **919 MB** | **15.40** | **3.2ms** |
| -n 14 软 | OpenCV/ffmpeg | 430% | 1500 MB | 15.82 | ~45ms |
| **-n 14 硬** | **MPP** | **386%** | **1487 MB** | **15.86** | **3.6ms** |

> 结论：硬解硬编后 CPU -10~12%、内存 -13~34MB、FPS 持平略升；后处理（含软编）从 ~40ms 降至 ~3ms（编码已卸载到硬件）。图片 JPEG 硬解验证：uav.jpg 检出 38 目标（与软解一致）。单测 18/18。

### 任务 4 迭代 2：1080p 绿条与 RGA 并发错误修复（2026-08-13）

- **问题 1（顶部绿/紫条）**：mppvideodec 把 1080p 高度按 16 对齐输出为 **1088 行 NV12**（缓冲区 3133440 字节），但 caps 只报 height=1080；此前按 1080 找 UV 平面，把 Y 平面 8 行当色度 → 顶部 16 行绿条。修复：`gst_buffer_get_video_meta` 读取真实 `offset[]/stride[]` 组装紧凑 NV12（无 meta 时按缓冲区大小推断）；并加 `mppvideodec arm-afbc=false` 禁用 AFBC 压缩输出。验证：读取帧与输出视频顶部绿条清零（与源内容一致）。
- **问题 2（RGA_BLIT fail: Invalid argument）**：`preprocess_mat_to_dma` 中 640×640 原地 `imcvtcolor` 在 `-p 2` 并发下偶发失败（-p 1 时 0 次）。修复：RGA 调用加全局互斥锁串行化。验证：1080p 全片 rga_fail=0。
- 1080p 实测（test_people_small_little_18s，-n 8，performance）：FPS **15.15**、CPU **360%**、RSS **1127MB**、pre 8.5ms、post 7.7ms、19 目标/帧。单测 **19/19**（新增 1080p 无绿条回归测试）。

### 任务 4 迭代 3：特殊尺寸视频质量修复 + 多格式支持（2026-08-13）

- **特殊尺寸视频模糊修复**（cars-from uav 480×332@60）：
  - 根因 1：容器帧率元数据缺失（caps 0/1）导致输出按默认 30fps 编码 → 60fps 源被慢放（时长 18.5s）；修复：读取端用 PTS 推算真实帧率。
  - 根因 2：mpph264enc 自动档码率仅 ~0.64Mbps → 低码率块状伪影；修复：`bps = w×h×fps×0.2`（clamp 1~12Mbps）自适应。
  - 效果：输出 60fps、时长 9.25s（与源一致）、码率 1.89Mbps；纯转码对照确认编码器本身正常（无检测框时清晰度 8.66 vs 源 10.58，正常损耗）。
- **多格式支持**：`GstVideoReader` 按扩展名选择链路——容器 `qtdemux/avidemux/matroskademux/tsdemux`，编解码 H.264→H.265 自动重试（`mppvideodec` 两者均支持），图片 `mppjpegdec`（jpg/jpeg）/`pngdec`/`bmpdec`/`webpdec`；**数据流验证**（启动后 800ms 内必须出帧，否则换候选，杜绝"PLAYING 成功但无帧"）；OpenCV 回退保留。
- **GStreamer RGA 检查结论**：板端 GStreamer **无 RGA 插件**（仅无关的 rganalysis 音频分析，无全局宏可开）；RGA 硬件转换只能在我们自己的代码里做（如 NV12→BGR 用 imcvtcolor），作为后续迭代方向。
- 单测 **20/20**（新增 AVI/H.264 + MP4/H.265 多格式读取测试）。

### 任务 4 迭代 4：特殊尺寸视频“仍模糊”根治（2026-08-13）

- **用户反馈**：迭代 3 修复后，480×332@60 输出仍比“无 GST 版（8.53Mbps mpeg4）”模糊，且比标准 720p/1080p 输出模糊。
- **帧级取证**（抽第 200/400 帧，480×332 原始 BGR）：
  - MPP 硬解→BGR 与 ffmpeg 软解对比度几乎一致（std 53 vs 53）→ **解码端不是模糊主因**；
  - 所有编码变体（任意码率/QP）对比度统一损失 ~13%（std 47.4→41.3）→ 与码率无关的**系统性色彩学问题**；
  - 源视频为 **VFR**（555 帧 / 21.7s，平均 26.6fps，标称 60fps），PTS 不规则。
- **根因 1（色彩学）**：appsrc(BGR) 未声明 colorimetry，mpph264enc 把默认 sRGB 色彩学写入 H.264 VUI：
  `color_range=pc / color_space=gbr / transfer=sRGB`（实测 ffprobe），而真实内容是 BT.709 limited →
  解码器按错误矩阵还原，对比度被压、色调偏移（视觉即“发灰/模糊”）。
  修复：appsrc caps 显式 `colorimetry=bt709` → 输出变为 `color_range=tv / transfer=bt709 / primaries=bt709`，
  帧 std 恢复至 47.5（源 47.4）。
- **根因 2（码率/控码）**：默认 rc-mode=cbr + `bps=w×h×fps×0.2` 在 60fps 下仅 ~1.9Mbps，复杂纹理（草地）
  帧被压成块状伪影；且 profile 默认 baseline（无 CABAC）。修复：默认 **fixqp（rc-mode=2）+ qp-init=22 + profile=high**，
  恒定质量、按内容自适应码率；`set_encoder_params()` 支持 vbr/cbr/fixqp + QP + profile 配置（diag_reencode 可 A/B）。
- **健壮性修复**：NPU 模型加载失败时主流程曾永久挂起（reader 阻塞满队列 + wait_idle 空转 + 析构 push 毒丸卡死）；
  现 NPU 初始化失败计数熔断（`workers_ok_`）、输入直接丢帧、`wait_idle()` 30s 上限、析构先 shutdown 队列。
  新增回归测试（模型加载失败 <30s 退出）。
- **PTS 帧率估算**：改为间隔**众数**统计（hist 1~240fps），抗 VFR 单帧抖动；480×332 源仍判 60fps 输出。

#### 编码质量 A/B（同一源 480×332@60，转码 554 帧，fixqp=恒定质量 / vbr/cbr=bps 3.34M）

| 配置 | 输出码率 | 文件大小 | stdG | edgeG@200/400 | lapVarG@200/400 |
|------|----------|----------|------|---------------|-----------------|
| 源（ffmpeg 软解参考） | 0.70Mbps | - | 47.4/49.2 | 3.86 / 4.76 | 512 / 522 |
| fixqp24+high（旧，无 bt709） | 1.80Mbps | 2.1MB | 41.3 | 3.16 / 4.60 | 331 / 496 |
| **fixqp22+high+bt709** | 2.44Mbps | 2.8MB | 47.5/49.2 | **3.74 / 4.72** | 467 / 516 |
| vbr+high+bt709 | 3.72Mbps | 4.3MB | 47.6/49.2 | 3.84 / 4.80 | 478 / 524 |
| 无 GST 软编参考（8.53Mbps@26.6fps，含画框） | 8.53Mbps | 22.2MB | 59.5 | 9.58 | 4087 |

> 结论：colorimetry 修复前对比度损失 ~13%（41.3 vs 47.4），修复后完全恢复；fixqp22 边缘能量保留 97~99%，
> 与 vbr(3.7M) 相当且文件更小，故定为默认。

#### 修复后完整流水线实测（performance 模式，-p 2 -n 8 -P 3 -c -0.13f，输出带检测框）

| 输入 | FPS | CPU 平均 | 峰值 RSS | 输出码率/大小 | 输出参数 |
|------|-----|----------|----------|----------------|----------|
| 特殊 480×332@60 | **15.64** | **328%** | **822MB** | 8.95Mbps / 10.3MB（9.23s） | High/tv/60fps |
| cars.mp4 720p@30 | 15.38 | 333% | 966MB | 4.35Mbps / 7.2MB | High/tv/30fps |
| 1080p@25（18s） | 15.02 | 352% | 1148MB | 5.17Mbps / 12.2MB | High/tv/25fps |

> 对比上版：720p FPS 15.40→15.38、CPU 322%→333%、RSS 919→966MB（采样法差异，无实质回归）；
> 1080p FPS 15.15→15.02、CPU 360%→352%、RSS 1127→1148MB。fixqp 恒质量下码率反而更低（1080p 12M→5.2M）。
- 输出帧质量：修复版特殊视频 edgeG=8.12 / lapG=3371（无 GST 参考 9.58 / 4087，含画框干扰），顶部绿条=0。
- 产物：`/home/neardi/Workspace_Codex/img/自测小尺寸_-0.83f_gst_修复v4.mp4`（对比旧模糊版 `_gst_模糊.mp4`）。
- 单测 **21/21**（新增“模型加载失败不挂起”回归）。

### 任务 4 迭代 5：VFR 帧率误判修复（时长压缩 + 闪烁花屏，2026-08-14）

- **用户反馈**：特殊尺寸 VFR 视频（cars-from uav 480×332）输出时长被压（源 21.7s → 输出 9.2s），
  且 2.35× 快进播放产生部分帧部分行“闪烁花屏”观感。
- **根因（实测确认）**：writer 打开时仅依据前两帧 PTS 间隔（前段 60fps burst，16.7ms）→ 误判 60fps；
  PTS 全量统计（555 帧）：间隔众数/中位数均为 **33ms（30fps，295 次）**、50ms（20fps，197 次）、
  16.7ms（60fps，61 次），平均 **26.6fps**，容器时长 21.725s → 正确输出帧率约 **25.5fps**。
  花屏定位：抽样帧行跳变位置（30/31、299/300）在**源帧同样存在**（航拍 OSD 固定线条），
  非输出伪影；“闪烁”主因是 60fps 快进播放的帧跳变。
- **修复（VFR 两遍法）**：`GstVideoReader` 新增 `caps_fps_authoritative()`（caps framerate 有效）与
  `measured_avg_fps()`（优先容器时长 count/duration，回退 PTS 首尾跨度）；
  `run_video_mode` 在 caps 帧率无效（0/1）时先 MPP 硬解全量扫描统计（555 帧约 2-3s），
  再正常处理；CFR 视频（caps 有效）零额外开销。writer 打开帧率改为使用该实测值。
- **实测（performance，-p 2 -n 8 -P 3 -c -0.13f）**：
  - 特殊 480×332：probe **25.5fps** → 输出 **26fps / 21.31s**（源 21.7s，误差 -1.8%）；
    FPS **15.63**（基线 15.64）、CPU **321%**（基线 328%）、RSS **827MB**（基线 822MB）——持平；
    全片 11 个时间点抽样 **0 帧花屏/行错乱**。
  - 回归：cars.mp4 720p **15.41FPS/332%/954MB/30fps/13.27s**（基线 15.35/330/945/13.27）；
    1080p **15.17FPS/370%/1152MB/25fps/18.88s**（基线 ~15.0-15.2/352-360/1127-1148/18.9）——无回归。
- 单测 **22/22**（新增“VFR 平均帧率估算”：读完整段后 avg_fps 应约 25~27fps）。

### 任务 4 迭代 5 修复：CFR 视频被误判 VFR 走两遍法（2026-08-14）

- **用户反馈**：迭代 5 声称“CFR 视频零开销”不实——所有视频都执行 VFR 两遍法；
  长视频（test_people_small_little.mp4，25fps/300s/6000帧）需先等 probe 全量解码（体感 30s+）
  才打开 VideoWriter，且 fps 用自算值（24.9967）而非视频自带 25fps。
- **根因**：`caps_fps_valid` 仅在 `read()` 解析 caps 时置位，而 main 的两遍法判断发生在
  `open()` 之后、首次 `read()` 之前 → 该标志恒为 false → 所有视频误判为 VFR。
- **修复**：`try_start_pipeline` 的 verify 出帧阶段直接从 sample 的 caps 解析 framerate
  并置 `caps_fps_valid`；CFR 视频 open 后即判定有效 → 直接用 caps fps、零两遍。
- **验证**（performance，-p 2 -n 8 -P 3）：
  - 长 CFR（test_people_small_little.mp4）：无 "VFR probe"、fps 直接用 **25**、VideoWriter 立即打开；
  - 特殊 VFR（480×332）：仍两遍（probe 25.5）→ 输出 26fps/21.31s、FPS 15.81；
  - cars.mp4（30fps CFR）：无两遍 → 输出 30fps/13.27s；
  - 单测 **22/22**。
