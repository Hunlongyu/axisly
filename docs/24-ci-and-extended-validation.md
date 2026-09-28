# CI 修复与扩展验证

本轮落实发布前的第 1、3 项任务：修复真实远端 CI 故障，扩大新功能对照，并测量
保留树局部更新及真实文本测量成本。开发版本仍为 0.1.0；没有发布 1.0。

## CI 修复

首次 Windows MSVC Debug CI 编译成功，272 项测试中只有
`gridly.cache.no_allocations` 失败：已预热、容量不变的缓存未命中路径仍有分配。
本机 `benchmark-debug` 重现了同样的问题。

原因是 `result.error = {}` 创建临时 `GridError`，其 `std::string` 在 MSVC Debug
下会分配迭代器代理。现在原位重置错误码、消息和条目 ID，复用字符串存储。
没有关闭迭代器检查，也没有放宽零分配断言；失败诊断增加场景、帧和分配字节数。

修复提交 `9219a41` 的 [7 项远端任务全部通过](https://github.com/Hunlongyu/gridly/actions/runs/36389354343)。
后续工作流另外增加 Windows MSVC Release，使 Debug、Release 都执行分配契约，
并保存普通构建及新增参考对照的失败日志。当前提交的完整状态见
[GitHub Actions](https://github.com/Hunlongyu/gridly/actions)。

## 新增 Taffy 对照

原 `GRIDLY_REFERENCE_V1`、30 个固定用例、750 个随机用例及旧失败重放协议保持有效。
新增独立 `GRIDLY_REFERENCE_V2` 与 `advanced.py`，默认执行 **913 个用例**：

- 13 个固定回归，包括稀疏／Dense、部分锁定、隐式增长、百分比、基线及跨行游标。
- 4 种自动流向各 150 个随机用例。
- 百分比、首基线各 150 个随机用例。

共同子集：固定／百分比显式轨道、固定隐式模板、正向索引、单轴／双轴自动放置、
行／列稀疏和 Dense、跨格，以及容纳得下的显式首基线组。
自动放置用例的条目使用零固有尺寸、零 margin 和 Stretch，隔离放置算法；
基线用例只跨一行。自定义基线直接交给双方的测量回调；底边回退要求条目的
minimum height 与 preferred height 相等，因为 Taffy 用 MinContent 探测基线，
gridly 在最终宽度下使用 preferred height 回退。不能把这两种不同输入语义当成同一场景。
所有准入条件在执行引擎前检查；失败或不兼容输入不会被计为通过。

Taffy 固定 0.14.0，仅为参考工具启用 `detailed_layout_info`。比较其自身输出的
轨道位置和条目放置，无需插入会扰动自动放置的探针。逐条比较最终 row／column／span、
单元格矩形、条目矩形和占用尺寸；关闭取整，误差仍为 `1e-4 + 1e-6 * max(abs(a), abs(b))`。
该诊断特性只用于正确性参考工具，不改变既有性能比较程序的 Taffy 配置。

扩展对照发现并修复了实际算法问题：稀疏流中，一个锁定起始行、跨多行的自动条目，
必须让它覆盖的每一行都记住已放置条目的末端；旧实现只记录起始行，后续行可能回填空洞。
现在传播到全部覆盖行，列流作相同转置处理，并保留 Dense 回填行为。
独立 C++ 回归和原始失败输入均已保存；原有全自动位图对照继续通过。

比较工具新增 12 项检查，覆盖协议拒绝、输入边界、独立 golden、放置差异检测、
失败落盘和通过两个真实执行程序重放。另用种子 42 执行 **3,613** 个扩展用例，全部通过。

```powershell
./scripts/build-msvc.ps1 -Preset reference-release
python tests/reference/advanced.py `
  --gridly build/reference-release/tests/reference/gridly_reference_native.exe `
  --taffy build/reference-release/tests/reference/cargo/x86_64-pc-windows-msvc/release/gridly-taffy-reference.exe `
  --replay build/reference-release/tests/reference/advanced-failures/failure.json `
  --failure-dir build/replay-advanced
```

## 保留树与真实文本 benchmark

`gridly_retained_bench` 随 `GRIDLY_BUILD_BENCHMARKS` 启用。合成换行场景跨平台运行；
Windows 额外使用 DirectWrite 和实际安装的 Consolas 16 px，创建文本布局、实际排版并读取
测量结果。验证窄宽度确实增加文字高度，没有窗口、渲染或虚构的字体测量数值。
字体文件哈希、DirectWrite 版本和程序哈希随结果保存。开发工具的 `dwrite` 链接不进入核心库。

比较同一 `GridTree` 的两种宿主失效策略：`targeted` 只标记受影响叶子或分支，
`full_invalidation` 每帧标记整棵树。两者拥有相同节点、回调、输入和内部测量缓存。
这是局部失效收益的测量，不是与 Taffy／Yoga 的速度对比。

场景包含 100／1,000 个合成标签及 100 个真实字体标签，每 25 个标签一个嵌套面板。
分别测试内容不变、单标签修改、单面板修改、连续缩放。所有帧在计时前逐节点对照
整树失效结果及独立的无状态布局结果，同时核对占用尺寸和测量次数；计时内消费三个矩形。

- 计时包含：内容修改、脏标记传播、计算、旧输出替换、计数和少量结果消费。
- 计时不包含：建树、字体初始化、首次预热、完整几何校验、最终销毁及写文件。
- 每个批次重建相同初始状态，两个策略交替先后顺序；输出所有批次数据。
- 本机 Release 正式测量运行两轮，每轮 9 批、每批 40 帧，期间无并行构建或测试。
- 未绑定 CPU，仍可能受桌面后台负载影响。p95 是批均值的 p95，不是单帧尾延迟。

```powershell
./scripts/build-msvc.ps1 -Preset benchmark-release
./build/benchmark-release/gridly_retained_bench.exe `
  --samples 9 --iterations 40 `
  --output build/retained-summary.csv --samples-output build/retained-samples.csv
```

两轮全部 18 个批均值合并后的中位数，单位为微秒。这里的参照是整树失效策略。

| 场景 | 局部失效 | 整树失效 | 耗时降低 | 回调次数（局部／整树） |
| --- | ---: | ---: | ---: | ---: |
| 1,000 个合成标签，单叶子修改 | 57.42 | 1166.93 | 95.1% | 252／12000 |
| 1,000 个合成标签，单面板修改 | 56.80 | 1144.15 | 95.0% | 300／12000 |
| 1,000 个合成标签，连续缩放 | 1039.30 | 1156.17 | 10.1% | 10000／12000 |
| 100 个真实字体标签，单叶子修改 | 487.76 | 3977.81 | 87.7% | 154／1000 |
| 100 个真实字体标签，单面板修改 | 994.02 | 3987.59 | 75.1% | 250／1000 |
| 100 个真实字体标签，连续缩放 | 2583.80 | 4038.01 | 36.0% | 800／1000 |

输入不变时，局部策略保持 0 次测量和 0 次布局重算。嵌套布局包含多个有限约束探测，
因此单叶子修改仍可能重测受影响面板中的兄弟条目；没有承诺每次编辑只测两次。
局部编辑受益明显，连续缩放仍需访问整个树。上述比例只适用于本机和本组输入。

原始数据：[汇总](benchmarks/retained-msvc-2026-09-28/summary.csv)、
[第一轮全部批次](benchmarks/retained-msvc-2026-09-28/samples-1.csv)、
[第二轮全部批次](benchmarks/retained-msvc-2026-09-28/samples-2.csv)、
[环境与字体](benchmarks/retained-msvc-2026-09-28/environment.json)、
[验证及源码哈希](benchmarks/retained-msvc-2026-09-28/validation.json)。

## 本机验证

| 配置 | 结果 |
| --- | --- |
| MSVC benchmark-debug | 275/275，含严格分配契约及真实字体场景 |
| MSVC benchmark-release | 275/275 |
| reference-release | 271/271：原 780＋新 913 个对照，22＋12 项比较工具检查 |
| 扩展对照第二种子 | 3,613/3,613，seed=42 |
| Clang／MSVC ASan | 各 267/267 |

GeeyoouUI 已按用户选择列为后续真实宿主目标，见 [接入验收计划](25-geeyoou-integration-plan.md)。
本轮 DirectWrite 数据是文本测量 benchmark；GeeyoouUI 使用自己的 Blend2D 文本路径，
需要在实际接入时另行验收。
