# 18 — M10 性能基准与第一轮优化

本文记录 `0.0.11` 的 M10 第一阶段：可复现的场景、独立的计时／分配工具，以及基于数据的 ID 去重优化。生产核心保持无状态，只依赖 C++20／STL。后续增加的三库同场景实测见 [对比报告](19-comparative-benchmarks.md)；测量缓存、保留树和 dirty 传播仍未完成。

## 构建和运行

默认关闭 `GRIDLY_BUILD_BENCHMARKS`，不下载第三方库，不安装／导出基准目标。Windows：

```powershell
./scripts/build-msvc.ps1 -Preset benchmark-release
./build/benchmark-release/gridly_bench.exe --output build/benchmark-release/timing.csv
./build/benchmark-release/gridly_bench_allocations.exe --output build/benchmark-release/allocations.csv
```

已有编译器环境的终端以及 Linux/macOS：

```text
cmake --preset benchmark-release
cmake --build --preset benchmark-release
ctest --preset benchmark-release
```

其他平台运行 `build/benchmark-release/gridly_bench` 和 `gridly_bench_allocations`，不带 `.exe`。`--output` 的父目录须已存在，默认向标准输出写 CSV。`benchmark-debug` 只检查功能，不能与 Release 的速度混比。

两个程序共用参数：

```text
--list
--case weighted_100x100
--iterations 100 --samples 11 --warmup 3
--output report.csv
--smoke
```

`--smoke` 为每批 2 次、3 批、预热 1 次；它不会覆盖连续缩放的全部计时状态，但计时前的完整校验会检查全部 17 种尺寸。参数按顺序处理，后面的数值覆盖先前值。非法参数、未知场景、输出失败和布局失败返回非零退出码。分配程序额外支持 `--self-test`。

## 场景

| 场景 | 条目数 | 目的 |
| --- | ---: | --- |
| `fixed_3x3` | 9 | 小型固定布局的调用成本 |
| `weighted_20x20` | 400 | 中型权重网格 |
| `weighted_31x31` | 961 | 对齐 Taffy wide 的第一档规模 |
| `weighted_50x50` | 2,500 | 常规压力场景 |
| `weighted_100x100` | 10,000 | 对齐 Taffy wide 的第二档规模 |
| `weighted_316x316` | 99,856 | 对齐 Taffy wide 的第三档规模 |
| `weighted_shuffled_100x100` | 10,000 | 全宽 64 位 ID、含 0 和最大值、乱序输入 |
| `resize_50x50` | 2,500 | 17 种容器尺寸循环重算 |
| `content_20x20` | 400 | 两轮内容测量 |
| `content_resize_20x20` | 400 | 宽度约束引起的模拟文字换行 |
| `range_20x20` | 400 | Content／Range／有上限的权重组合 |
| `spans_50x50` / `spans_100x100` | 2,500 / 10,000 | 密集重叠跨度、内容和范围轨道 |
| `nested_resize_3levels` | 5 个展开条目 | M7 宿主适配器的递归探测和最终重排 |

平面场景采用显式放置，默认容器 1280×900，gap 为 4，padding 为 8，margin 为 1。316×316 使用 12000×12000，避免轨道全部被间隙挤空。缩放容器的宽度为 320 到 1920、步长 100；高度在 400 到 720 之间循环。内容度量是确定性的算术回调，不包含真实字体排版。

嵌套单独标记 `scope=host_adapter`；其中 `columns`／`rows` 是根网格的 2／1，不能误读为总轨道数。该用例每次宿主调用触发 39 次核心布局、94 次叶节点测量，结果中的 5 个条目不意味着只做了 5 份工作。

## 计时口径和 CSV

场景构造、每种尺寸的完整几何校验、重复结果校验、预热、CSV 格式化均在计时区间外。每次计时包含 `compute_grid` 的输入校验、内部临时分配、布局、常数规模的结果校验值计算以及结果析构。输入规格和条目数组重复使用；核心没有跨调用缓存。

`gridly_bench` 使用正常分配器。`steady_clock` 测量整批调用再除以次数，报告各批均值的中位数 `median_us` 和最近秩法的 `p95_batch_us`。后者不是单次调用延迟的 P95；默认 11 批时它等于最大的批均值。默认每场景 3 次预热，然后 11×100 次正式调用。17 个尺寸循环会在每批从头开始。

`gridly_bench_allocations` 单独链接替换的全局 `new`／`delete`，覆盖数组、sized、aligned 和 nothrow 形式；其 CSV 的耗时列留空，避免分配计数成本污染性能结论：

- `allocations_per_layout`：测量区间内 C++ 堆分配次数除以宿主调用次数。
- `requested_bytes_per_layout`：请求字节累计值除以调用次数，不是峰值内存。
- `peak_live_bytes`：整个批次内，被计数分配的同时存活请求字节峰值，不除以次数。
- `measurements_per_layout`：平面布局的回调次数；嵌套场景统计叶节点实际测量次数。
- `core_layouts_per_run`：每次宿主调用包含的核心计算次数，平面布局为 1。
- `checksum`：整批结果的确定性校验值；采样首、中、末条目及占用范围，用于消费输出和发现基准漂移，不能代替完整几何测试。

所有报告均携带场景、版本、构建配置、编译器、规模、循环和模式。每批检查计数／校验值一致，分配统计检查区间结束时被计数的对象已释放。正式计时程序的内存列留空。

分配探针仅针对当前单线程 C++ 场景，不拦截 `malloc`、OS 分配或其他线程，也不统计输入构造、分配器元数据、碎片和常驻内存（RSS）。峰值相同、分配次数降低是可能的。本阶段没有测得“进程内存减少多少”。

## 竞品确实有 bench

核对版本为 [Taffy 0.14.0 benchmark 清单](https://github.com/DioxusLabs/taffy/blob/v0.14.0/benches/Cargo.toml)，其中有 tree creation、Flexbox、Grid 和混合布局，使用 Criterion。

[Taffy 原生 Grid bench](https://github.com/DioxusLabs/taffy/blob/v0.14.0/benches/benches/grid.rs) 包含 31／100／316 阶宽网格、多分支深树和 100／1000 层单链。随机轨道还包含百分比、auto、min/max-content，条目使用自动放置。`iter_batched` 的 setup 创建新树，计时闭包拥有这棵树并计算布局，闭包结束还会释放树；它不是重复命中同一棵已布局树缓存的测试。建树另有独立 bench。

[Yoga 的 capture benchmark](https://github.com/facebook/yoga/blob/main/benchmark/Benchmark.cpp) 从捕获数据构建树，分别记录建树、布局和两者总和；其测量回放还可能按捕获时长等待。它与本项目的显式 Grid 和算术测量回调有不同工作范围。

本轮复用 Taffy wide 的**规模档位**，没有声称原场景等价，也没有运行上游性能程序。已有 M8 的 780 个 Taffy 用例验证正确性，Python 启动进程／序列化／探针的耗时不能拿来代表布局性能。

后续跨库对比应固定版本、编译配置与共同子集输入，先逐条验证几何一致，再明确分开：首次布局、强制重算、连续缩放、缓存命中、建树／销毁、结果提取。计时应在各自进程内部完成，并注明是否清除叶节点缓存、是否取整和测量函数成本。不能将 Taffy 缓存命中与 gridly 完整计算直接作为算法快慢结论。百分比、自动放置和超过 M7 递归预算的深树先标为不支持。

## 2026-09-28 本机数据

机器：Windows 11 x64（10.0.26200），Intel i7-11700K，8 核／16 线程；MSVC 19.51.36257，Release `/O2 /Ob2 /DNDEBUG`，单线程、未固定 CPU 亲和性。测试时未并行构建或运行其他基准；普通桌面后台活动仍可能影响数据。

相同基准程序先后链接优化前后核心，各做两轮计时和一轮分配统计。两轮都用 100 次／批、11 批、预热 3 次。原始 CSV 和程序 SHA-256 存在 [本机记录目录](benchmarks/msvc-2026-09-28/environment.json)。下表使用第二轮的批均值中位数，单位为 µs／次，分配为次／次：

| 场景 | 优化前 µs | 优化后 µs | 前／后分配 |
| --- | ---: | ---: | ---: |
| fixed_3x3 | 1.295 | 0.773 | 19 / 8 |
| weighted_20x20 | 31.279 | 16.653 | 430 / 28 |
| weighted_31x31 | 69.119 | 36.491 | 993 / 30 |
| weighted_50x50 | 212.687 | 97.322 | 2,534 / 32 |
| weighted_100x100 | 944.783 | 519.890 | 10,038 / 36 |
| weighted_316x316 | 10,743.160 | 5,414.086 | 99,898 / 40 |
| weighted_shuffled_100x100 | 741.620 | 763.025 | 10,038 / 36 |
| resize_50x50 | 180.884 | 96.162 | 2,534 / 32 |
| content_20x20 | 37.389 | 22.902 | 412 / 10 |
| content_resize_20x20 | 36.519 | 22.798 | 412 / 10 |
| range_20x20 | 37.665 | 22.711 | 419 / 17 |
| spans_50x50 | 449.713 | 381.221 | 6,596 / 4,094 |
| spans_100x100 | 1,873.437 | 1,655.088 | 26,507 / 16,505 |
| nested_resize_3levels | 25.402 | 21.354 | 533 / 416 |

第二轮常规 100×100 耗时降低约 45%，316×316 约 50%。乱序 ID 第二轮慢约 2.9%，首轮快约 4.4%，没有稳定的加速证据；不能据常规递增 ID 场景宣称所有输入都快一倍。这些是本机观察，不是延迟保证，也不是与竞品的排名。

六份 CSV 的 14 个场景、规模、回调次数、核心调用数和校验值全部一致。100×100 累计请求字节从 1,277,886 降到 935,590，但被计数的存活峰值仍为 732,995 字节；临时 ID 存储与最终结果的存活阶段不同。

- [第一轮优化前计时](benchmarks/msvc-2026-09-28/baseline-timing.csv) / [优化后计时](benchmarks/msvc-2026-09-28/optimized-timing.csv)
- [第二轮优化前计时](benchmarks/msvc-2026-09-28/baseline-timing-repeat.csv) / [优化后计时](benchmarks/msvc-2026-09-28/optimized-timing-repeat.csv)
- [优化前分配](benchmarks/msvc-2026-09-28/baseline-allocations.csv) / [优化后分配](benchmarks/msvc-2026-09-28/optimized-allocations.csv)

## 优化和未完成项

`validate_input` 原先用 `unordered_set` 去重，MSVC 实现每个条目需要一个节点分配。现在只分配一个连续的 `(ItemId, input_index)` 数组，排序后找到输入中最早出现的重复位置，再按原有顺序校验每个条目。0 和最大 64 位 ID 仍然合法；输出和测量顺序不变，重复条目自身的非法尺寸／放置仍先报错，非法输入不触发测量。

这是从哈希表平均 O(n) 查找改为 O(n log n) 排序的空间／时间权衡，减少节点分配但不保证所有 ID 排列更快。新增三个回归用例覆盖最早重复 ID、错误优先级、测量前校验，以及 400 组与独立线性扫描比较的随机输入。

跨格场景仍有较多临时分配，下一轮可优化候选数组的复用；嵌套仍有重复探测，可据真实宿主语义设计缓存。`std::pmr`、测量缓存、保留树、dirty 传播和真实字体成本均未实现。本次没有改变公共 API、布局规则或生产库依赖。

本机 MSVC Debug／Release 各 213/213 通过（208 项核心测试、5 项基准检查）；Reference Release 210/210 通过，包含 780 个 Taffy 共同子集用例和对照器契约。六份 CSV 的字段、场景和统计一致性另行检查通过。

从启用基准的构建目录安装后，安装消费工程与默认子目录消费工程各 1/1 通过；安装内容只有 7 个公共头文件、核心静态库和 CMake 包文件，没有基准程序或它的分配探针。

CI 已加入基准冒烟和分配探针契约，不设置共享机器上的耗时阈值。远端 CI 配置不代表已在 Linux/macOS 上运行。
