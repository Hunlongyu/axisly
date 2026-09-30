# 全部 Taffy 0.14 语义通过后的同输入性能对照

本轮以已通过全部 6020 个官方输入的代码为基线。合成换行测量 96／384 叶，
DirectWrite 测量 96 叶；六种场景及两种失效方式均保持相同。
建树、预热、完整几何比较和销毁不计时；计时从编辑／失效或空间变化开始，
到 compute 返回及基本检查结束。每批 20 帧，9 批交替运行 targeted 与
full_invalidation。p95 是批次平均值的百分位。

每组先比较前四帧的全部节点、溢出、轨道、放置及线名；最终还校验
所有汇总行的 checksum、测量和布局次数在优化前后完全相同。
时间由普通分配器程序测量；另一程序单独统计 C++ new/delete。
峰值是测量窗口内新申请的存活字节，不含预热已存活对象、malloc 或
DirectWrite／COM 的其他分配，不等于进程总峰值内存。
最终四次运行顺序执行，期间没有本任务的编译或测试。
未绑定 CPU，时间是本机观察值，不把微小差值当成稳定性能承诺。

## 存储重构

1. 布局片段内联根节点，Size／Baseline 探测不申请节点数组；Full 才收集后代。
2. Size、Baseline、Full 保持独立缓存键；不从 Full 结果推导早期 Size 查询。
3. 查询组采用分段存储，避免空余容量保留完整根节点；冷缓存直接转移整组。
4. 完整输出复用后代缓冲区；节点顺序、预算、替换和失效语义保持原规则。

首轮只有内联根节点时，申请减少但峰值增加约 34%；分段存储和整组转移
将合成首次峰值增幅缩小至约 3.7%。保留该首轮数据，避免只展示有利结果。

## 最终结果

下表均为 targeted，每帧中位时间；分配和峰值取自单独的分配程序。

| 后端／叶子 | 场景 | 时间 µs：前→后 | 申请次数：前→后 | 申请字节：前→后 | 窗口峰值字节：前→后 |
| --- | --- | ---: | ---: | ---: | ---: |
| 合成／384 | 首次 | 7862.03→7176.93 | 35859.00→21904.00 | 9091511.00→5477342.00 | 2130237.00→2208227.00 |
| 合成／384 | 单叶编辑 | 288.49→271.70 | 1631.75→1130.30 | 639334.20→551355.20 | 253254.40→255157.00 |
| DirectWrite／96 | 首次 | 3824.86→3436.84 | 9458.00→5972.00 | 2338698.00→1436754.00 | 556651.00→575979.00 |
| DirectWrite／96 | 单叶编辑 | 185.19→168.62 | 871.70→586.15 | 252656.80→200604.00 | 82778.20→84320.20 |

合成首次时间下降 8.71%，申请次数下降 38.92%，申请字节下降 39.75%，
窗口峰值增加 3.66%。DirectWrite 首次相应为 -10.14%、-36.86%、-38.57%、+3.47%。
分支编辑的 DirectWrite 窗口峰值增加 8.90%；这是保留缓存存储的取舍，
不宣称所有场景的峰值均下降。完整 36 行对照见 [comparison-rtl-final.json](comparison-rtl-final.json)。
输入不变时仍为零测量、零布局、零申请；仅尺寸变化仍为零测量、一次根布局。

## 原始数据与版本

| 阶段 | 内容 | 版本与哈希 |
| --- | --- | --- |
| before | 完整语义、优化前 | [manifest](before-manifest.json)；核心为 `4784b10` |
| after | 内联根节点首轮，峰值回退 | [manifest](after-manifest.json)；核心为 `b9e0292` |
| final | 紧凑查询组及输出复用，RTL 修复前 | [manifest](final-manifest.json)；核心为 `48c84c7`，宿主为 `075950b` |
| rtl-final | 最终 RTL 坐标修复后复测 | [manifest](rtl-final-manifest.json)；核心为 `3a060ca` |

before 的 benchmark 验证层已经包含之后提交的线名比较，计时区域和输入未变。
每个阶段保存 `*-synthetic-timing.csv`、`*-synthetic-allocations.csv`、
`*-directwrite-timing.csv`、`*-directwrite-allocations.csv` 及相应
`*-samples.csv` 逐批数据。旧共同子集、较早算法版本的数据具有不同语义，
不与本轮当作等价性能基线。

初始化 MSVC 环境后可用 `cmake --preset benchmark-release` 和对应 build
预设构建两个程序，按以下参数顺序执行：

```powershell
build/benchmark-release/axisly_css_retained_bench.exe --iterations 20 --samples 9 --output timing.csv --samples-output timing-samples.csv
build/benchmark-release/axisly_css_retained_allocations.exe --iterations 20 --samples 9 --output allocations.csv --samples-output allocation-samples.csv
build/benchmark-release/axisly_css_retained_bench.exe --backend directwrite --iterations 20 --samples 9 --output directwrite.csv --samples-output directwrite-samples.csv
build/benchmark-release/axisly_css_retained_allocations.exe --backend directwrite --iterations 20 --samples 9 --output directwrite-allocations.csv --samples-output directwrite-allocation-samples.csv
```
