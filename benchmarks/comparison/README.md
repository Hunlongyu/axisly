# 三方 CSS 布局基准

`css_compare.py` 比较本库的 `css::LayoutTree`、Taffy 0.14.0 与 Yoga 3.2.1。
程序中的 `axisly_css` 是本库的引擎标识，所有布局基准统一使用 CSS 布局树。

## 当前实测结果

2026-10-09 重跑，源码版本 [`6354815`](https://github.com/Hunlongyu/axisly/commit/6354815a98260862002f30445e4b5989f7dc28aa)。
覆盖 20 个场景、317 个引擎／输出模式／操作组合。构建后 CTest **95/95** 通过；
计时前全部 317 组逐帧几何对照通过，无资源限制失败。测试前后 84 个源码／配置文件
与计时程序的哈希一致。

| 测试条件 | 本次配置 |
| --- | --- |
| 系统与处理器 | Windows 11 Pro 10.0.26200，Intel Core i7-11700K，约 64 GiB 内存 |
| 编译 | Axisly / Yoga：MSVC 19.51.36257.0，Release `/O2 /Ob2 /MD`；Taffy：Rust 1.98.0，`opt-level=3` |
| 依赖版本 | Taffy 0.14.0、Yoga 3.2.1；无 PGO、LTO 或 Sanitizer |
| 运行 | 平衡电源方案，驱动及子进程固定逻辑 CPU 4；构建和测试结束后串行计时 |
| 采样 | 3 轮 × 9 批，轮间交换引擎顺序；每批 102 或 510 次，循环同一组 17 帧输入 |
| 度量 | 相同的合成文本测量，不含字体加载、窗口和绘制；关闭像素取整 |

以下数值是 **27 批平均耗时的中位数，单位 µs，越小越好**。场景名称中的项数／叶数不含根和中间容器，
精确节点数见 CSV。**完整**输出包含根相对几何数组和 Grid 轨道诊断；**按需**输出关闭这两项，
仅查询同样三个根相对矩形。Taffy／Yoga 保留节点局部几何，不要求额外导出全数组。
“—”表示该引擎未参与该场景；Yoga 只参加共同的 Flex 场景。

完整数值、逐组 95% 探索性区间和相对 Taffy 的比值见 [summary.csv](results/2026-10-09-msvc/summary.csv)，
机器、编译参数、源码／程序／输入哈希见 [environment.json](results/2026-10-09-msvc/environment.json)。
区间跨 1 表示本轮无法区分快慢；**低于 1 µs 的结果不用于速度排名**。

本轮首次布局（`cold`）的主要观察：

- Axisly 完整输出相对 Taffy：20 个场景中，5 个更快、10 个更慢、5 个区间跨 1，无法区分快慢。
- 弹性 Grid 20×20、100×100 和跨格 Grid 50×50 的耗时分别为 Taffy 的 **0.48×、0.49×、0.40×**；
  两个混合布局文本场景分别为 **0.86×、0.83×**，本轮区间均支持更快。
- 100 项固定 Flex、100 项内容测量 Flex、384 叶嵌套 Flex 文本分别为 Taffy 的
  **1.67×、2.09×、1.92×**，本轮 Taffy 更快。其他操作模式需分别查看，首次布局结论不代表增量更新。

### 首次布局（cold）

| 场景 | Axisly 完整 | Axisly 按需 | Taffy | Yoga |
| --- | ---: | ---: | ---: | ---: |
| Flex 固定尺寸 100 项 | 25.63 | 26.15 | 15.32 | 42.50 |
| Flex 伸展 100 项 | 26.79 | 27.65 | 15.40 | 47.88 |
| Flex 固定尺寸 1,000 项 | 248.90 | 261.48 | 241.87 | 410.83 |
| Flex 伸展 1,000 项 | 257.87 | 266.12 | 253.28 | 453.26 |
| Flex 纵向伸展 1,000 项 | 279.96 | 265.74 | 263.13 | 416.23 |
| Flex 收缩 100 项 | 25.93 | 26.85 | 15.59 | 45.84 |
| Flex 换行 100 项 | 27.48 | 28.31 | 17.94 | 58.29 |
| Flex 内容测量 100 项 | 48.22 | 47.12 | 23.10 | 57.90 |
| 嵌套 Flex 文本 96 叶 | 103.02 | 97.91 | 56.14 | 148.84 |
| 嵌套 Flex 文本 384 叶 | 459.60 | 431.91 | 238.98 | 604.39 |
| Flex 嵌套 12 层 | 48.68 | 50.01 | 35.87 | 42.74 |
| Block 文本 96 叶 | 33.25 | 36.15 | 17.89 | — |
| Grid 固定轨道 20×20 | 188.20 | 177.97 | 165.34 | — |
| Grid 弹性轨道 20×20 | 188.63 | 172.48 | 390.53 | — |
| Grid 弹性轨道 100×100 | 7,257.71 | 6,826.28 | 14,750.46 | — |
| Grid 跨格 50×50 | 1,369.27 | 1,365.02 | 3,446.90 | — |
| 混合布局文本 96 叶 | 165.65 | 157.56 | 193.37 | — |
| 混合布局文本 384 叶 | 632.75 | 594.64 | 763.59 | — |
| Flex 不同样式 100 项 | 29.79 | 30.67 | 13.49 | 43.87 |
| Flex 不同样式 1,000 项 | 292.84 | 312.49 | 282.43 | 427.93 |

<details>
<summary>容器尺寸变化（resize）</summary>

| 场景 | Axisly 完整 | Axisly 按需 | Taffy | Yoga |
| --- | ---: | ---: | ---: | ---: |
| Flex 固定尺寸 100 项 | 20.12 | 20.26 | 13.30 | 28.33 |
| Flex 伸展 100 项 | 21.18 | 21.27 | 13.96 | 46.64 |
| Flex 固定尺寸 1,000 项 | 192.65 | 195.51 | 126.89 | 274.57 |
| Flex 伸展 1,000 项 | 195.53 | 204.29 | 214.24 | 424.66 |
| Flex 纵向伸展 1,000 项 | 193.92 | 199.77 | 213.06 | 406.25 |
| Flex 收缩 100 项 | 19.68 | 19.98 | 13.74 | 46.83 |
| Flex 换行 100 项 | 19.97 | 21.94 | 17.00 | 41.65 |
| Flex 内容测量 100 项 | 25.01 | 25.96 | 14.55 | 40.52 |
| 嵌套 Flex 文本 96 叶 | 4.19 | 4.33 | 37.17 | 109.97 |
| 嵌套 Flex 文本 384 叶 | 15.01 | 15.50 | 151.23 | 485.16 |
| Flex 嵌套 12 层 | 35.76 | 35.86 | 36.13 | 44.79 |
| Block 文本 96 叶 | 28.83 | 28.44 | 14.10 | — |
| Grid 固定轨道 20×20 | 77.05 | 68.48 | 131.57 | — |
| Grid 弹性轨道 20×20 | 98.40 | 93.66 | 361.01 | — |
| Grid 弹性轨道 100×100 | 5,366.56 | 5,046.93 | 14,492.56 | — |
| Grid 跨格 50×50 | 804.39 | 686.44 | 2,753.47 | — |
| 混合布局文本 96 叶 | 126.79 | 118.83 | 130.30 | — |
| 混合布局文本 384 叶 | 464.23 | 467.53 | 452.70 | — |
| Flex 不同样式 100 项 | 19.23 | 19.38 | 12.41 | 28.74 |
| Flex 不同样式 1,000 项 | 195.39 | 191.72 | 182.26 | 305.07 |

</details>

<details>
<summary>单叶内容修改（leaf_edit）</summary>

| 场景 | Axisly 完整 | Axisly 按需 | Taffy | Yoga |
| --- | ---: | ---: | ---: | ---: |
| Flex 内容测量 100 项 | 25.68 | 25.30 | 11.87 | 40.27 |
| 嵌套 Flex 文本 96 叶 | 9.32 | 9.73 | 6.10 | 112.67 |
| 嵌套 Flex 文本 384 叶 | 20.80 | 21.46 | 12.00 | 501.76 |
| Flex 嵌套 12 层 | 32.63 | 31.03 | 32.50 | 41.58 |
| Block 文本 96 叶 | 13.71 | 13.78 | 8.84 | — |
| 混合布局文本 96 叶 | 15.02 | 12.21 | 29.64 | — |
| 混合布局文本 384 叶 | 30.34 | 25.24 | 72.57 | — |

</details>

<details>
<summary>新建布局树并首次计算（build）</summary>

| 场景 | Axisly 完整 | Axisly 按需 | Taffy | Yoga |
| --- | ---: | ---: | ---: | ---: |
| Flex 固定尺寸 100 项 | 43.62 | 44.61 | 28.73 | 61.30 |
| Flex 伸展 100 项 | 41.99 | 43.00 | 29.37 | 66.74 |
| Flex 固定尺寸 1,000 项 | 387.15 | 412.96 | 463.21 | 651.18 |
| Flex 伸展 1,000 项 | 399.51 | 416.10 | 509.49 | 707.29 |
| Flex 纵向伸展 1,000 项 | 399.07 | 416.19 | 516.36 | 676.68 |
| Flex 收缩 100 项 | 42.22 | 43.07 | 29.18 | 64.20 |
| Flex 换行 100 项 | 44.12 | 42.85 | 29.35 | 72.03 |
| Flex 内容测量 100 项 | 62.61 | 65.51 | 38.39 | 74.19 |
| 嵌套 Flex 文本 96 叶 | 123.00 | 129.65 | 77.04 | 188.87 |
| 嵌套 Flex 文本 384 叶 | 526.11 | 508.88 | 314.04 | 749.44 |
| Flex 嵌套 12 层 | 63.96 | 60.55 | 40.59 | 49.58 |
| Block 文本 96 叶 | 49.29 | 54.74 | 28.59 | — |
| Grid 固定轨道 20×20 | 220.05 | 198.78 | 193.87 | — |
| Grid 弹性轨道 20×20 | 244.94 | 256.60 | 425.95 | — |
| Grid 弹性轨道 100×100 | 10,702.66 | 11,543.89 | 19,122.36 | — |
| Grid 跨格 50×50 | 1,731.51 | 2,387.21 | 4,095.06 | — |
| 混合布局文本 96 叶 | 186.76 | 179.44 | 212.62 | — |
| 混合布局文本 384 叶 | 776.33 | 701.15 | 855.97 | — |
| Flex 不同样式 100 项 | 45.58 | 48.65 | 25.97 | 61.83 |
| Flex 不同样式 1,000 项 | 863.48 | 711.78 | 492.38 | 692.28 |

</details>

<details>
<summary>输入不变（same）</summary>

| 场景 | Axisly 完整 | Axisly 按需 | Taffy | Yoga |
| --- | ---: | ---: | ---: | ---: |
| Flex 固定尺寸 100 项 | 0.040 | 0.165 | 0.060 | 0.085 |
| Flex 伸展 100 项 | 0.039 | 0.164 | 0.061 | 0.081 |
| Flex 固定尺寸 1,000 项 | 0.041 | 0.209 | 0.058 | 0.085 |
| Flex 伸展 1,000 项 | 0.040 | 0.210 | 0.058 | 0.082 |
| Flex 纵向伸展 1,000 项 | 0.040 | 0.202 | 0.064 | 0.084 |
| Flex 收缩 100 项 | 0.037 | 0.163 | 0.057 | 0.078 |
| Flex 换行 100 项 | 0.040 | 0.163 | 0.057 | 0.078 |
| Flex 内容测量 100 项 | 0.039 | 0.161 | 0.059 | 0.084 |
| 嵌套 Flex 文本 96 叶 | 0.038 | 0.182 | 0.061 | 0.072 |
| 嵌套 Flex 文本 384 叶 | 0.040 | 0.211 | 0.061 | 0.077 |
| Flex 嵌套 12 层 | 0.038 | 1.21 | 0.092 | 0.109 |
| Block 文本 96 叶 | 0.038 | 0.166 | 0.072 | — |
| Grid 固定轨道 20×20 | 0.042 | 0.177 | 0.060 | — |
| Grid 弹性轨道 20×20 | 0.040 | 0.181 | 0.060 | — |
| Grid 弹性轨道 100×100 | 0.045 | 0.612 | 0.068 | — |
| Grid 跨格 50×50 | 0.042 | 0.250 | 0.070 | — |
| 混合布局文本 96 叶 | 0.039 | 0.195 | 0.059 | — |
| 混合布局文本 384 叶 | 0.040 | 0.203 | 0.060 | — |
| Flex 不同样式 100 项 | 0.040 | 0.161 | 0.057 | 0.080 |
| Flex 不同样式 1,000 项 | 0.046 | 0.213 | 0.060 | 0.080 |

</details>

这是一台 Windows / MSVC 机器上的合成布局结果，不能推广为所有平台、编译器或真实应用的总排名。
按需模式的收益也不能当作相同输出契约下的算法提速；两种模式应按宿主实际需要分别选择。

下方[运行方法](#运行)中的 MSVC 命令可复现相同场景与采样配置；其中 `--include-retained`
会同时输出完整和按需两种模式。计时前需完成构建和测试。

本次逐批样本、逐帧几何、程序快照和构建日志保留在本机 `build/comparison-docs-20261009/`。
仓库保存上面的汇总与环境快照，后续更新结果时须同时更新二者。

## 相同输入与测量

- 三方读取同一份 fixture，使用相同节点、样式、17 帧尺寸序列和内容更新。
- 使用同一套合成文本度量公式，没有 DirectWrite、字体加载、绘制或窗口。
- Yoga 参加共同的 Flex 场景；Block、Grid 和混合场景比较本库与 Taffy。
- 关闭像素取整。计时前逐帧、逐节点对照 Taffy 的位置和尺寸，阈值为
  `0.01px + 1e-6 × max(abs(a), abs(b))`，计时中继续检查结果消费值。
- 默认 `axisly_css` 生成完整根相对几何和 Grid 诊断；Taffy、Yoga 保留节点局部几何。
  计时不额外要求对方导出全数组。这些公开 API 的固有成本包含在结果中。
- `--include-retained` 增加 `axisly_css_retained`：关闭全数组和 Grid 诊断，
  使用 `layout(node)` 读取同样三个根相对矩形。它与默认输出分别列示，几何校验一致。

本库另有 `axisly_css_retained_bench` 的 DirectWrite 宿主测试；其结果不能
与这里的合成度量三方表混合。

`axisly_css_style_bench` 单独测量 `set_style` 的相同样式提交、当前样式引用提交和
实际样式变更，分别覆盖 Flex 与包含具名线／区域的 Grid。每项报告 7 批的中位数、
最小值和最大值；`axisly_css_style_allocations` 使用相同输入单独报告每次提交的分配
次数及申请字节数。运行 `--smoke` 只检查接口与缓存行为。此微基准不包含布局计算，
不能将其中的加速比当作首次布局或完整页面加速比。

Grid 热路径的同版本优化前后对照使用 `axisly_css_grid_bench`、
`axisly_css_grid_allocations` 和独立计数构建；场景、重放方法与实测记录见
[Grid 优化验证](../grid-optimization.md)。它不参与三方引擎排名。

## 计时模式

| 模式 | 计时内容 |
| --- | --- |
| `cold` | 已建好的新树首次布局，加首／中／末三个矩形读取 |
| `resize` | 复用树，循环变化容器尺寸，再布局和读取 |
| `same` | 输入不变，重复布局和读取 |
| `leaf_edit` | 修改一个测量叶的内容、标脏，再布局和读取 |
| `build` | 新建树、安装样式和测量回调、首次布局、读取 |

所有模式都排除 fixture 解析、API 样式值转换、进程启动、结果序列化和树销毁。
本库与 Taffy 都在计时外准备 Style，build 内复制到节点；Yoga 使用公开
setter 安装对应属性。创建节点和安装样式的 API 成本仍然包含在 build 内。
`cold` 表示未计算过的布局树，并不表示清空 CPU 缓存。
`build` 专门用于检查初始化成本，默认与 `cold` 并列运行。
无模式参数时运行所有适用模式；`--mode` 可重复指定以缩小范围。

## 运行

在已初始化 MSVC 环境的 PowerShell 中：

```powershell
./scripts/build-msvc.ps1 -Preset comparison-release
python benchmarks/comparison/css_compare.py --native build/comparison-release/axisly_css_comparison_native.exe --taffy build/comparison-release/benchmarks/comparison/cargo/x86_64-pc-windows-msvc/release/css.exe --output build/css-comparison --include-retained --cpu 4 --rounds 3 --samples 9 --frames-scale 3
```

CPU 编号需在本机有效。性能计时应在构建、测试和其他基准全部结束后串行运行。
`--case` 可重复选择场景；`--baseline-native` 可以交替计时保存的旧版程序。
旧版程序必须支持所选模式：早期程序没有 `build` 模式。

### Windows Clang 对照

安装 LLVM 后，可使用相同 MSVC SDK／运行库，以 clang-cl 构建本库和 Yoga：

```powershell
./scripts/build-msvc.ps1 -Preset comparison-clang
python benchmarks/comparison/css_compare.py --native build/comparison-clang/axisly_css_comparison_native.exe --taffy build/comparison-clang/benchmarks/comparison/cargo/x86_64-pc-windows-msvc/release/css.exe --output build/css-comparison-clang --cpu 2 --rounds 3 --samples 9 --frames-scale 3 --mode cold --mode resize --mode leaf_edit --mode build --mode same
```

工具链从 PATH 或 `%ProgramFiles%/LLVM/bin` 发现 clang-cl 和 lld-link；不含基准的
库构建使用 `release-clang`。两种编译器使用独立构建目录。默认 Release 优化、
浮点语义与 Taffy 配置保持原有设置，不启用 fast-math 或链接时优化。
比较代码前后变化时，`--baseline-native` 应选用同一编译器构建的旧版本。
编译器切换带来的变化应另列，不能记成算法本身的加速。

结果目录保存输入、全帧几何检查、逐批耗时、中位数、P95、测量次数以及
源码／二进制哈希。低于 1 μs 的结果接近调用与计时器开销，不据此比较
几十纳秒的差异。分配探针会扰动耗时，其时间不能代替普通程序的计时。

## 结果管理

结果保存在 `--output` 指定的独立目录，建议使用 `build/` 下的路径。
报告直接使用 `summary.csv` 的 `median_us`（单位 µs），同时检查逐批样本和不利结果。
比较历史版本时保留旧二进制、对应源码和编译设置；不同输入、测量函数或计时边界的结果不能混为一组。

默认运行 3 轮、每轮 9 批，轮间反转引擎顺序。报告追加中位数和相对 Taffy 比值的
95% bootstrap 区间：固定随机种子重采样 2000 次，先采样轮次，再采样该轮的批均值，
比值两端使用相同轮次、各自独立采样批次。不把同批内的帧当成独立样本。
`comparison` 的 `faster`／`slower` 表示整个比值区间低于／高于 1，
跨过或接触 1 标为 `inconclusive`（当前数据无法区分快慢，并不证明性能相等）。
少于 3 轮不生成区间；冒烟测试标为 `smoke_only`，不作性能推断。

这些是当前机器与测试条件下的探索性统计，3 轮不能保证消除系统性偏差。
发布结论仍需固定并记录电源计划、避免其他负载，并分别归档 MSVC 与 clang-cl 结果。
跨编译器或跨机器的相对比值也可能漂移，不能假定比值自动抵消 CI 环境变化。
共享 CI 的冒烟测试只验正确性；稳定执行器和可比基线建立之前，不将 20% 阈值作为硬性性能门禁。

## 独立布局与分配基准

查询、Block、取整和 Flex 基线热点使用 `axisly_css_hotpaths_bench`，分配统计使用
`axisly_css_hotpaths_allocations`。`--queries 0` 读取全部节点，其余值指定查询次数。
两者支持 `--case lookup_source|lookup_owned|block|rounded|rounded_sparse|baseline`
和 `--mode cold|same|edit|resize`。优化前后使用相同的基准源码构建并保存程序：

还支持 `hidden`、`tracks_mixed`、`tracks_flex`、`tracks_source`、`batch_move`、
`deep_reorder`。`tracks_*` 每次交替报告开关，不修改样式；
`batch_move` 与 `deep_reorder` 单独计时拓扑 API，`same` 为原列表重复提交，
其余模式为批量移动或深层重排，不包含布局时间。

```powershell
python benchmarks/hotpaths_compare.py --before build/before.exe --after build/benchmark-release/axisly_css_hotpaths_bench.exe --output build/hotpaths-comparison --cpu 4
```

该驱动串行交替运行两版程序，记录校验和、逐批时间、源码/程序哈希和探索性区间。
小规模短批次若出现回退或不确定结果，用较大的 `--iterations` 复核；
分配程序独立运行，其时间不用于速度结论。场景限制及实测见
[热点优化验证](../hotpath-optimization.md)。
剩余建议的实现与对照见 [后续优化验证](../hotpath-remaining-optimization.md)。

```powershell
./scripts/build-msvc.ps1 -Preset benchmark-release
./build/benchmark-release/axisly_css_retained_bench.exe --output build/benchmark-release/timing.csv
./build/benchmark-release/axisly_css_retained_allocations.exe --output build/benchmark-release/allocations.csv
```

其他平台使用相应 CMake 预设和不带 `.exe` 后缀的程序。
计时与分配统计分别运行；Debug、Sanitizer 和分配探针的时间不作为普通 Release 性能结论。
构建选项见 [构建指南](../../docs/building.md)，正确性验证见 [测试指南](../../docs/testing.md)。
