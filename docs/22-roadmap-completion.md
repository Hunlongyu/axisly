# 计划功能完成记录 — 0.1.0

原路线图 M0–M10 及后来列入 1.1+ 的功能已全部实现，当前开发版本为 **0.1.0**。
核心仍为 C++20/STL，旧调用默认继续使用显式放置、浮点坐标和每条目两次测量。
新接口的使用方法、数值语义、所有权与失效规则见 [API 契约](23-api-contract.md)。
历史 benchmark 文件保留原始版本信息；本次另行记录结果。

## 完成清单

| 计划项目 | 交付接口／行为 | 主要验证 |
| --- | --- | --- |
| 像素取整 | `round_layout`／`PixelRounding`，支持 DPI 与根原点 | 6,931 组共享边界、嵌套边缘、非法 scale |
| 自动放置 | 行／列、稀疏／Dense、单轴或双轴自动 | 240 组独立位图对照、显式占用和跨格 |
| 隐式轨道 | 可选增长、行列模板和资源限制 | 空网格、显式越界增长、超限与缓存更新 |
| 百分比 | `Track::percentage`，支持 Range 上下界 | 缩放、padding／gap、混合范围、溢出 |
| 首基线 | 测量基线、底边回退、起始行分组、跨格 | 宽度重测、margin、硬上限、极端数值 |
| 条目测量缓存 | `MeasurementCache`，`GridCache::enable_measurement_cache` | 完整约束键、LRU、逐项失效、异常恢复 |
| 保留树 | `GridTree`、稳定 NodeId、父子管理 | 换父级、排序、删除、失效句柄、循环与深度限制 |
| 脏标记传播 | 叶子到祖先失效、无关分支复用 | 回调计数、子树复用、150 帧随机编辑对照 |
| API 与工程收尾 | 明确契约、独立头文件、示例、sanitizer 构建／CI | 安装包与子目录消费、MSVC／Clang ASan、既有回归 |

新示例 `examples/retained_grid.cpp` 同时展示自动放置、隐式行、百分比列、基线、
设备像素取整和树缓存。本机 4 个标签首次测量 **8 次**，不变帧 **0 次**，只改一个
标签后 **2 次**；窗口变化重新检查完整测量约束。

## Implementation and acceptance order

1. Optional pixel rounding: snap edges on a configurable device-pixel grid;
   validate scale/origin, preserve shared boundaries, and keep measurement in
   logical coordinates.
2. Placement and implicit tracks: explicit/one-axis/full automatic placement,
   row/column sparse/dense flow, spans and occupied-cell avoidance; bounded work
   and track growth, deterministic ordering, unchanged explicit overlap rules.
3. Percentage tracks and range bounds: resolve against available inner extent
   before subtracting gaps; handle zero space, resizing, overflow and mixed tracks.
4. Baseline alignment: first baseline within the starting row, measured baseline
   or bottom-edge fallback, margins, spans, intrinsic growth and constrained rows.
5. Optional measurement cache: bounded entries per item, full known/available
   constraints, callback binding and explicit item/content invalidation; invalid
   measurements/exceptions never become reusable entries.
6. Retained GridTree: owned nodes and topology, safe handles, attach/detach/remove,
   ancestor dirty propagation and independent subtree reuse, finite child-grid
   measurement, final-size rearrangement, depth/work/overflow protection and
   atomic publication of successful geometry.
7. Integration: public-header and consumer checks, runnable example, API contract
   documentation, sanitizer CI, existing regression/reference/allocation suites
   and a fresh common-workload benchmark report.

The `std::pmr` roadmap entry is conditional on measured allocator pressure; it is
not a required public feature. Flexbox, Block, CSS parsing/cascade, DOM ownership,
calc, subgrid, masonry and browser writing modes remain out of scope.

## 本机验收

Windows x64，Visual Studio Insiders MSVC 19.51，LLVM Clang 22.1.8。

| 配置 | 结果 |
| --- | --- |
| MSVC Debug／Release | 各 266/266（265 项核心回归＋新示例） |
| comparison-release | 272/272，包括分配检查和比较工具几何 smoke |
| reference-release | 268/268；其中 Taffy 0.14.0 共 780 个共同子集用例、22 个工具测试 |
| playground-release | 268/268；含 50 个场景尺寸组合和 30 次隐藏渲染／交互检查 |
| Clang AddressSanitizer | 266/266 |
| MSVC AddressSanitizer | 266/266 |
| 安装后 `find_package` 消费 | 1/1 |
| `add_subdirectory` 消费 | 1/1，开发依赖默认关闭 |
| 安装与头文件 | 11 个公开头文件独立编译；安装 11 个头文件、静态库和 4 个 CMake 文件 |

异常恢复使用作用域清理，回调的类型和内容原样传播；异常中断后的部分测量不会用于
下一次布局。Clang ASan 回归中发现的异常中转路径故障已通过该方式消除，两套 ASan
均跑完整测试。GCC／Clang 的 Linux ASan+UBSan 工作流已配置；尚未在远端运行 CI，
不将配置文件视为远端通过证明。

全部新功能有本库语义测试；既有 Taffy differential 仍只覆盖原先明确的共同子集，
不把新 API 的实现视为全部 CSS／Taffy 行为等价。完整浏览器基线、Flexbox、Block、
CSS 解析、subgrid 等继续按原计划的非目标处理。`std::pmr` 为有性能证据时才评估的选项，
既有缓存工作区的零分配检查通过，因此此次没有增加分配器接口。

本次未进行版本发布；0.1.0 表示开发功能及 API 契约的当前基线，不是跨版本 ABI 或
所有真实宿主场景均已验证的承诺。0.0.13 的源码和比较程序快照保存在忽略的 build 目录中。

## 同场景性能复测

同一台机器，固定 Taffy 0.14.0／Yoga 3.2.1，与保留的 0.0.13 程序同轮比较。
11 个场景、61 个引擎／场景组合均通过独立几何校验：2,074 组输出、3,175,600 个矩形，
最大绝对误差 **0**。两轮各 9 批，第二轮反转引擎顺序；共 366 条计时记录。
正式计时串行运行，期间没有并行构建／测试；未绑定 CPU，桌面背景活动仍可能影响数据。

单位为 **µs／次**，表中为 18 个批均值的中位数。构造和最终销毁不计时，复用路径的旧
输出释放计入时间；只消费首／中／末三个矩形，完整几何在计时前单独验证。

| 连续缩放场景 | 0.0.13 GridCache | 0.1.0 GridCache | Taffy | Yoga |
| --- | ---: | ---: | ---: | ---: |
| 100 项固定横排 | 2.17 | 2.24 | 37.51 | 25.12 |
| 100 项固定竖排 | 2.15 | 1.99 | 33.72 | 23.22 |
| 100 项权重横排 | 3.75 | 3.62 | 43.82 | 39.95 |
| 1,000 项权重横排 | 39.30 | 37.25 | 393.21 | 394.85 |
| 1,000 项权重竖排 | 39.23 | 38.44 | 393.14 | 379.44 |
| 100 项 Content 横排 | 5.38 | 5.05 | 53.39 | 37.23 |
| 固定 20×20 | 7.83 | 6.99 | 98.28 | — |
| 权重 20×20 | 10.93 | 10.12 | 324.37 | — |
| 权重 100×100 | 239.09 | 227.66 | 12084.06 | — |
| 固定跨格 50×50 | 80.79 | 72.00 | 669.43 | — |
| 权重跨格 50×50 | 110.66 | 107.40 | 2284.96 | — |

完整结果命中全部低于 **1 µs**，实际测量次数均为 **0**。本轮缓存缩放相对 0.0.13 的
变化范围为 **-10.9% 至 +3.1%**；并未因功能补齐出现明显整体回退。
这不是一次以降低所有耗时为目标的优化：无缓存连续缩放各场景增加约
**1.4%–12.1%**，Content 场景冷缓存也有约 8.2% 增加。
新版本增加了结果放置元数据和功能校验；本次数据只能确认耗时变化，不能单独证明其因果。

本表继续使用旧的显式共同场景，GridCache **没有启用条目测量缓存**，也没有把 GridTree
计时混入现有缓存口径。新树和条目缓存的正确性、失效范围及回调计数另有测试与示例。
Content 合成测量在缩放时 gridly 仍每帧 200 次调用；Taffy 在该固定测量模型下可以复用，
不能据此假设任意文字／图片测量都不依赖新约束。结果是这些引擎在指定场景下的比较，
不构成 C++ 与 Rust 的一般性结论，也不表示全功能 CSS 行为等价。

原始结果：[汇总 CSV](benchmarks/roadmap-msvc-2026-09-28/summary.csv)、
[各轮结果](benchmarks/roadmap-msvc-2026-09-28/results.csv)、
[全部样本](benchmarks/roadmap-msvc-2026-09-28/samples.json)、
[几何校验](benchmarks/roadmap-msvc-2026-09-28/geometry.json)、
[环境与程序哈希](benchmarks/roadmap-msvc-2026-09-28/environment.json)、
[验收与源码哈希](benchmarks/roadmap-msvc-2026-09-28/validation.json)。
