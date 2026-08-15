Feature: 任务六 NPU 运行时优化与 25-30 FPS 目标

  Background:
    Given 板端已设置 performance governor
    And 模型 rtdetr_i8.rknn SHA-256 为 39dee60d1ea3322d3d60382c9a874c500aa57c38d6ad9667643037f81295d878
    And 已保存任务六开始前的基线与逐帧检测结果

  Scenario: 输出张量识别与缓存
    Given 已加载 rtdetr_i8.rknn
    When RKNNDetector 查询输入输出属性
    Then n_output 为 5
    And boxes 索引为 n_elems=1200 的最后一个匹配项
    And logits 索引为 n_elems=3000 的最后一个匹配项
    And 输出属性只在初始化阶段查询并缓存一次

  Scenario: 每帧只转移必要输出
    Given 输出优化已启用
    When 处理任一帧
    Then 仅 boxes 1200 与 logits 3000 被反量化进入后处理
    And 冗余输出 pred_logits 4200 与中间输出不被拷贝

  Scenario: 零拷贝失败自动回退
    Given 零拷贝已启用
    When rknn_set_io_mem 或内存创建失败
    Then 回退到 rknn_inputs_set 与 rknn_outputs_get
    And 推理成功且结果与基线等价

  Scenario: 128 字节对齐仅在有实测收益时启用
    Given 已分别以 64 和 128 字节对齐运行
    When 比较 FPS、Avg NPU、CPU、RSS 与检测结果
    Then 只有 128 字节对齐在结果等价且性能更优时被保留
    And 若无收益则回退 64 字节默认值

  Scenario: 专用每核拓扑优于统一多核掩码
    Given 已完成 auto、one-per-core、round-robin 对照
    When 使用默认配置
    Then 选用实测最优拓扑
    And 检测结果与 FPS/CPU/RSS 不回退

  Scenario: 图片检测结果严格等价
    Given 基准与优化版本均使用 uav.jpg、-c -0.13
    When 两版本分别运行
    Then 过滤后的 class_id、box 完全一致
    And score 绝对误差不超过 1e-6

  Scenario: 视频检测结果严格等价
    Given 素材为 cars.mp4、1080p 18s、480x332 VFR
    When 基准与优化版本分别运行
    Then 每帧过滤后的检测结果集合完全一致

  Scenario: 性能达到 25-30 FPS
    Given 优化版本在完整视频管线上运行 cars.mp4
    When 使用 /usr/bin/time -v 采集指标
    Then Overall FPS 位于 25 到 30 之间
    And CPU 与 Max RSS 不高于对应基线加测量容差
    And 检测结果严格等价

  Scenario: 动态批处理仅作为兜底且需批准
    Given Stage 1-3 未达到 25-30 FPS
    And 用户已明确批准重新导出 batch 维度模型
    When 验证 batch=3 及 4/6/8
    Then 仅当 FPS、CPU、RSS、结果等价全部通过才集成
    And 权值与网络结构保持不变

  Scenario: 默认模型文件未被修改
    Then 主模型 rtdetr_i8.rknn 的 SHA-256 保持为 39dee60d1ea3322d3d60382c9a874c500aa57c38d6ad9667643037f81295d878
