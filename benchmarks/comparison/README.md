# 三方 CSS 布局基准

`css_compare.py` 比较本库的 `css::LayoutTree`、Taffy 0.14.0 与 Yoga 3.2.1。
程序中的 `axisly_css` 是本库的引擎标识，所有布局基准统一使用 CSS 布局树。

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
python benchmarks/comparison/css_compare.py --native build/comparison-release/axisly_css_comparison_native.exe --taffy build/comparison-release/benchmarks/comparison/cargo/x86_64-pc-windows-msvc/release/css.exe --output build/css-comparison --cpu 4 --rounds 3 --samples 9 --frames-scale 3
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
