# TODOlist.md — 主任务列表与进度追踪

> 规则：按编号顺序执行，**前置任务完成并验证通过后才能开始下一个任务**。
> 每次取得进展必须动态更新本文件（状态、完成时间、指标对比、备注）。

## 任务清单

| # | 任务 | 状态 | 完成时间 | 备注 |
|---|------|------|----------|------|
| 1 | 图片输入检测：支持单张图片输入，输出绘制检测框与类别标识的图片 | **已完成并上传入库** | 2026-08-12 | 含"原版可检、workspace 不可检"一致性 bug 修复 |
| 2 | 调试日志等级化、模块化统一管理（`-G/--DEBUG`），替换散落打印 | **已完成** | 2026-08-12 | 待用户确认上传 |
| 3 | RGA_DMA 链路分析 + 迭代优化（指标：降低 CPU/内存占用） | **已完成（迭代 1+2）** | 2026-08-12 | 待用户确认上传 |
| 4 | GStreamer + RK MPP 硬解/硬编替代 OpenCV+FFmpeg 软解/软编 | **已完成（迭代1-5 已入库；迭代5修复 待上传）** | 2026-08-13 | 迭代5修复（2026-08-14）：CFR 视频被误判 VFR 走两遍法 |
| 5 | 预处理/后处理进一步提速（排除 NPU 时长） | **闲置（暂缓）** | - | 用户 2026-08-14 指令：暂不执行，等 NPU 瓶颈任务处理后再视情况恢复 |
| 6 | **NPU 推理耗时瓶颈（大任务，优先级最高）** | **已收尾（未达 25-30 FPS 目标）：仅保留 Stage1，Stage2/3 回退、Stage4 放弃** | 2026-08-15 | 保留：输出张量索引缓存 + 输出 buffer 预分配 + FrameBundle 预分配；Stage2 零拷贝输入/SRAM/topology/动态批处理均无收益或未执行；下一步方向待用户决策 |
| 7 | **摄像头通路验证 + draw_results 优化 + 实时播放（合集）** | **已完成并上传入库** | 2026-08-16 | 7.1 摄像头 YUYV 720p 通路 10.8 FPS；7.2 draw_results 3.4~3.8x 微基准提升、post -63%/-96%；7.3 --display 实时播放（视频/相机 FPS 差 <1%）；单测 29/29；commit 08f20e5 |

## 进度记录（动态更新）

### 2026-08-11

- 建立基线（默认参数 `-p 2 -n 3 -P 1 -c -0.13`）：cars.mp4 共 395 帧，整体 **8.55 FPS**，pre 2.46ms / npu 349.7ms / post 31.2ms。
- 建立基线（最佳参数 `-p 2 -n 14 -P 3 -c -0.13f`）：cars.mp4 共 385 帧，整体 **15.68 FPS**，CPU 平均 394%，峰值 RSS **1650 MB**（详见 METRICS.md）。
- 任务 1 实施完成：新增 `-i/--image` 图片模式 + 同步单帧检测接口 `detect_image()`；单元测试 **13/13 通过**。
- **检测一致性 bug 修复完成**（原版可检、workspace 不可检）：
  - 根因 1：输出张量按名称选错（4200 元素 pred_logits 当 logits，应为 3000 元素张量）→ 改为形状匹配（300×4 / 300×10，最后匹配者胜），与原版一致；
  - 根因 2：RGA DMA→DMA 源 stride 对齐污染（1360 宽 → DRM stride 4096 vs RGA wstride 1360px），非对齐宽度输入被逐行偏移 → mat 输入改走 CPU resize+cvtColor 紧致 DMA 路径。
  - 验证：uav.jpg @0.45 = 38 目标（原版 38）、VisDrone @0.45 = 25（原版 25）、录屏 mp4 每帧 46~52 目标（原版可检出）、cars.mp4 12 目标/帧。
- **待用户决策**：任务 1 已达成全部限制与要求，按规则询问是否上传入库/同步本地。
- 任务 2 完成（2026-08-12）：新增 `include/logger.h`（模块级联 + 级别 + LOG/LOGT/LOGR 宏），替换 6 个源文件 + 测试共 106 处散落打印；新增 `-G/--DEBUG`（0=仅错误+报告；1=Main；2=Main+RKNN；…；8=全部）；单测 **14/14**；FPS 无回归（performance 下 15.53 vs 基线 15.68）。
- 待用户决策：任务 2 是否上传入库/同步本地。
- 任务 3 完成（2026-08-12）：mat 输入改走 RGA virt→DMA 单链路（消除 CPU resize/cvtColor + 桥接 memcpy，失败回退 CPU）；CPU 453%→416%、RSS 1527→1492MB、pre 8.16→5.46ms、FPS 15.53→15.85；单测 14/14（MAE=0.109）。
- 任务 3 迭代 2（2026-08-12）：相机 DMA→DMA stride 安全化（wstride=实际 stride/bpp，非整除走 CPU 回退）+ 模拟测试（单测 15/15）；worker 配置实测：`-n 8` 甜点位（内存 953MB/-36%、CPU 365%/-15%、FPS 15.31/-3%）vs `-n 14`（1500MB/430%/15.82）。
- 待用户决策：任务 3 是否上传入库/同步本地。
- 任务 4~5 规划完成，待任务 3 收尾后按顺序启动。
- 任务 4 启动（2026-08-12）：板端 GStreamer 1.22.9 + rockchipmpp 插件就绪（mppvideodec 硬解 NV12 / mpph264enc 硬编支持 BGR）；方案：新增 gst_io（GstVideoReader/Writer）→ 视频读取/输出走 MPP 硬解硬编，保留 OpenCV 回退；编码端因 mpph264enc 原生支持 BGR 可省 videoconvert。
- 任务 4 完成（2026-08-12）：视频 H.264 硬解（NV12 直拉 + OpenCV cvtColor 转 BGR，规避 videoconvert 60ms/帧瓶颈）+ H.264 硬编（BGR 直送）+ JPEG 硬解（mppjpegdec）；CPU -10~12%、RSS -13~34MB、FPS 持平略升、后处理 ~40→3ms；单测 18/18。
- 任务 4 迭代 2（2026-08-13）：修复 1080p 顶部绿条（mpp 高度对齐 1080→1088 导致 UV 平面偏移，用 GstVideoMeta 真实 offset/stride 组装）+ 禁用 AFBC；修复 RGA 并发偶发 Invalid argument（互斥锁）；1080p 实测 FPS 15.15/CPU 360%/RSS 1127MB，rga_fail=0，单测 19/19。
- 任务 4 迭代 3（2026-08-13）：修复特殊尺寸视频（480×332@60）模糊/慢放（PTS 帧率估算 + 码率自适应 bps=w×h×fps×0.2）；多格式支持（avi/mkv/ts/hevc/h264/bmp/webp + H.264→H.265 自动重试 + 数据流验证）；GStreamer 无 RGA 插件（结论已记录）；单测 20/20。
- 任务 4 迭代 4（2026-08-13）：特殊尺寸“仍模糊”根治——根因① mpph264enc 色彩学元数据错误（sRGB/pc/gbr → 改为 appsrc `colorimetry=bt709`，输出 tv/bt709，对比度 std 41→47.5 恢复）；根因② 默认 cbr+baseline 低码率 → 默认 **fixqp qp22 + high**（恒质量，特殊视频 8.95Mbps、1080p 5.2Mbps、720p 4.3Mbps）；PTS 帧率估算改众数统计；NPU 初始化失败挂起修复（熔断+丢帧+wait_idle 30s+析构安全）；单测 **21/21**。实测：特殊 15.64FPS/328%/822MB，720p 15.38/333%/966MB，1080p 15.02/352%/1148MB。
- 任务 4 上传入库（2026-08-13）：用户同意上传，README 已更新（硬解硬编/多格式/迭代1-4记录/性能参考），git 提交并推送 gitee。
- 任务 4-5 已回退（2026-08-14）：动态尺寸 RGA 方案实测 FPS -3~6%、RSS +4~6.5%（大尺寸非16对齐源上
  RGA 硬件 resize 慢于 CPU NEON、编码器 BGRA 路径慢于原 CPU 回退），按用户规则放弃本次修改，
  代码与文档全部回到 `69e3034`（任务4 迭代1-4）；RGA_BLIT fail 噪音为上游 gst-mpp stride 缺陷与
  RGA 带宽问题，留待后续专项研究。
- 任务计划调整（2026-08-14）：任务5 改为**闲置（暂缓）**；新增**任务6：NPU 推理耗时瓶颈**（优先级最高，
  先于任务5），仅更新计划/记忆文档，待用户指令启动；板端 4-5 临时对照文件（约 800MB）已按用户确认清理。
- 任务 4 迭代 5（2026-08-14）：特殊尺寸 VFR 视频（480×332 录屏）输出时长被压（21.7s→9.2s）且
  快进播放产生闪烁花屏感。根因：writer 打开时仅取前两帧 PTS 间隔（前段 60fps burst）误判 60fps；
  PTS 全量统计显示间隔众数/中位数均为 33ms（30fps）、平均 26.6fps。修复：**VFR 两遍法**——
  caps 帧率无效（0/1）时先用 MPP 硬解全量扫描统计容器时长/平均帧率（probe 25.5fps），再正常处理；
  CFR 视频零开销。实测：输出 26fps/**21.31s**（源 21.7s）、FPS 15.63/CPU 321%/RSS 827MB（与基线持平）、
  全片抽样 0 花屏帧；720p/1080p 回归无变化；单测 **22/22**（新增 VFR 平均帧率估算测试）。
- 任务 4 迭代 5 修复（2026-08-14）：**CFR 视频被误判 VFR 走两遍法**——根因：caps_fps_valid 仅在
  read() 解析 caps 时置位，而两遍法判断发生在 open 后、首次 read 前，故所有视频都误判为 VFR；
  长视频（test_people_small_little.mp4，25fps/300s/6000帧）需先等 probe 全量解码（约 30s+）才开
  VideoWriter，且 fps 用自算值（24.9967）而非 caps 的 25。修复：try_start_pipeline 的 verify 出帧阶段
  直接从 sample caps 解析 framerate 并置 caps_fps_valid；CFR 视频 open 后即判定有效 → 直接用 caps fps、
  零两遍。验证：长 CFR 无 probe、fps=25、writer 立即打开；特殊 VFR 仍两遍 25.5→26fps/21.3s；
  cars 无两遍 30fps/13.27s；单测 22/22。
- 本地 git 快照 `cec2a05` 已提交（未推送），含任务 4 全部改动与 `对话上下文摘要.md` 的删除。

### 2026-08-15

- 任务6 Stage1 完成：RKNNDetector 初始化缓存输入/输出 attr 与 boxes/logits 索引；输出预分配并直接写入 FrameBundle；逐帧不再重复 query，不再反量化/搬运冗余输出。单测新增 resolve_rtdetr_output_indices 与 rknn_zero_copy_matches_infer_only，24/24 通过。
- 任务6 Stage2 完成验证：零拷贝输入 rknn_create_mem_from_fd + rknn_set_io_mem(pass_through=0) 与 infer_only 数值等价；但 cars_2s A/B 无收益（约 13.50 vs 13.83 FPS），故默认保持关闭，保留 --rknn-zero-copy 开关。
- 任务6 Stage3 完成验证：--rknn-sram、--rknn-topology round-robin/one-per-core 均无收益（约 13.72 / 13.36 / 11.80 FPS），默认保持 auto。
- 目标 25-30 FPS 未在 Stage1-3 达成；下一步需用户批准动态批处理参考验证。

### 2026-08-15 回退决策（Stage4 放弃，仅保留 Stage1）

- 放弃 Stage4 动态批处理执行；移除 batch3 模型/脚本/诊断产物。
- 回退 Stage2/Stage3 非收益改动：零拷贝输入、SRAM、topology、batch worker。
- 仅保留 Stage1：输出张量索引缓存、输出 buffer 预分配、FrameBundle 输出预分配；24/24 单测通过。
- 顺序复测 cars.mp4 n8：15.27 FPS（基线 15.31），无性能回退。

### 2026-08-15 任务7 启动（用户指令，合集）

- 任务6 正式收尾：未达 25-30 FPS 目标，保留 Stage1（提交 9fc657b），Stage2/3/4 全部回退/放弃；下一步方向待用户决策。
- 待上传项（均已本地提交、未推送，待用户确认）：任务4 迭代5 + CFR 修复（cfb4ee7）、任务6 Stage1（9fc657b）。
- 任务7 规划：7.1 摄像头 /dev/video41 通路验证与性能测量（设备已就绪：Web Camera，MJPG 720p/1080p@30、YUYV 720p@15）；7.2 draw_results 性能优化 + 分辨率自适应；7.3 实时播放 + 显示态 FPS。

### 2026-08-15 任务7.1 完成（摄像头通路）

- 通路打通：/dev/video41 协商 YUYV 1280×720（stride 2560）→ DMA→RGA→NPU→画框→MPP 硬编全链路；SIGTERM 优雅退出；无检测帧正常。
- 性能（50s 采样，performance）：n8 10.82 FPS / CPU 118.7% / RSS 780MB；n14 10.81 / 120.2% / 1308MB；FPS 受相机 YUYV 720p 源上限（~10.8fps）限制。
- 修复：V4L2 格式协商链（BGR3→RGB3→YUYV）、相机 buffer 生命周期（不提前 munmap/close）、预处理后提前归还 src_buf（7.6→10.8 FPS）、采集对象作用域（消除退出 core dump）、poll 优雅退出、默认不写视频。
- 回归：单测 26/26；cars.mp4 n8 15.22 FPS / 943MB 无回退。

### 2026-08-16 任务7.2/7.3 完成

- 7.2 draw_results：类别固定调色板 + 每帧每类文字度量缓存 + 去 putText 抗锯齿 + 小目标细框不画文字；
  线宽/字号随分辨率自适应（720p=2px/0.6 基准，1080p=3px/0.9，4K 封顶 4px/1.2）。
  微基准 3.4~3.8x；流水线 post：视频 1.34→0.49ms（-63%）、相机 0.35→0.013ms（-96%）。
- 7.3 实时播放：新增 `--display`；专用显示线程 + 丢旧保新队列（容量 4）；q/ESC 退出回调。
  视频 n8 显示 15.27 FPS（无显示 15.37，差 <1%）；相机 n8 显示 10.81 FPS（持平）；
  HDMI-2 显示器实测窗口 "RT-DETR" 正常显示（截图 display_shot.png）。
- 回归：单测 **29/29**；cars.mp4 n8 15.37 FPS/943MB、n14 15.67 FPS/1508MB（与基线持平）。
- 上传入库（2026-08-16）：用户同意，README 已更新（摄像头通路/自适应画框/--display 用法与指标），commit `08f20e5` 已推送 gitee。
