# Axisly／Taffy／Yoga Flex 与嵌套布局同场景复测

2026-09-28 在 Windows x64 上重新运行 `comparison-release` 全部场景。比较对象为
Axisly 当前 Flex 与 Grid、Taffy 0.14.0、Yoga 3.2.1。库本身只实现布局几何；
本轮不比较 CSS 解析、字体排版、渲染或控件创建。Axisly Flex 尚未通过全部
Flexbox 规范边界验收，性能对比不能替代规范符合性证明。

## 场景与公平性

- 6 个平面单轴场景同时比较 **Axisly Flex／Taffy Flex／Yoga Flex**。
  旧的 Axisly Grid／Taffy Grid 仍留在同一输入中，以展示使用不同算法
  完成同一几何任务的成本；Grid 与 Flex 的数字不混成算法排名。
- 5 个二维 Grid 场景比较 Axisly Grid／`GridCache`／Taffy Grid。
  Yoga 没有 Grid 算法，因此不参加。
- 3 个嵌套 Flex 场景比较 Axisly Flex／Taffy Flex／Yoga Flex：
  小型多面板仪表盘、大型多面板仪表盘、12 层容器深树。它们组合了
  横／竖容器、固定尺寸与弹性尺寸、padding 和 gap，并对全部节点的
  根坐标及尺寸做逐帧校验。Axisly 通过宿主适配器自顶向下对每个容器
  调用 `compute_flex`；适配器的规格和项目在建树阶段准备好。
  Taffy／Yoga 各用一棵原生保留树。

每个场景均有 17 个容器尺寸。正式计时前，对复用实例和新实例各验证
17 帧，逐节点与独立解析计算的矩形比较。阈值为
`1e-4 + 1e-6 × max(|实际|, |预期|)`；任何失败都中止计时。
各引擎关闭像素取整，使用 LTR、无边框、无 margin。嵌套场景只覆盖
确定的容器尺寸，不包含 intrinsic 回传、换行、绝对定位或 Grid／Flex 混合树。

`cold` 测新实例第一次布局，`resize` 测同一实例连续改变容器尺寸，
`same` 测尺寸不变的重复调用。三种模式均排除建树、解析和进程启动，
计入布局 API、旧结果退休和三个矩形的消费；Axisly 的输出分配与
嵌套容器调度在计时区内。Taffy／Yoga 保留节点内结果并正常使用缓存。
嵌套场景消费三个矩形时，Taffy／Yoga 沿父链换算根坐标；
Axisly 已在逐容器计算时形成根坐标，该部分读取成本也计时。
这是不同公开 API 的**实际调用成本**，并非去除数据所有权差异后的
纯算法内核比较。

每个组合串行执行两轮、轮间反转引擎顺序；每轮 9 批。
≤1,000 个项目的场景每批 340 次，其余 34 次。表中为 18 个批均值
的中位数，单位 µs／次，越低越好。小于 1 µs 的缓存命中不作细分排名。

## 结果

### 平面 Flex，同算法

| 场景 | 首次：Axisly／Taffy／Yoga | 缩放：Axisly／Taffy／Yoga |
| --- | ---: | ---: |
| 固定横排 100 项 | 22.65／14.21／39.78 | 21.86／11.13／26.30 |
| 固定竖排 100 项 | 22.35／12.99／37.77 | 21.87／11.06／24.74 |
| 权重横排 100 项 | 24.02／13.53／44.70 | 22.11／12.84／42.53 |
| 权重横排 1,000 项 | 207.66／173.99／437.09 | 209.77／176.12／419.62 |
| 权重竖排 1,000 项 | 208.07／160.24／407.25 | 211.36／138.18／394.70 |
| 内容测量横排 100 项 | 24.01／19.37／52.50 | 23.34／13.49／37.62 |

本轮平面 Flex 的首次和缩放布局，Axisly 均慢于 Taffy Flex，
快于 Yoga。输入不变时 Axisly Flex 仍重新计算，约 21–210 µs；
Taffy Flex 和 Yoga 的调用中位数均小于 1 µs。内容场景的测量
回调次数也不同：首次 Axisly／Taffy／Yoga 为 200／200／100，
缩放时为 200／100／0，不能把这一行当作纯算术算法速度。
1,000 项权重竖排的 Taffy Flex 缩放在两轮中位数为
119.87–176.05 µs，波动明显；单独再跑四轮时为
115.92–119.51 µs。两次都不改变三方排序，但不宜把
138.18 µs 当成稳定常数。四轮复核的样本保存在
[`column-stability`](benchmarks/comparison-axisly-2026-09-28/column-stability/)。

### 嵌套 Flex，同算法

| 场景（非根节点） | 首次：Axisly／Taffy／Yoga | 缩放：Axisly／Taffy／Yoga |
| --- | ---: | ---: |
| 多面板 116 节点 | 47.29／72.13／76.48 | 47.70／59.43／69.53 |
| 多面板 630 节点 | 171.57／346.51／379.28 | 175.69／274.83／369.59 |
| 深树 122 节点、12 层容器 | 36.17／71.37／80.00 | 38.76／54.77／79.34 |

这些固定／弹性尺寸的嵌套树中，Axisly 宿主适配器逐容器计算的首次和
缩放耗时低于 Taffy、Yoga。输入不变时 Axisly 为
47.58／176.25／38.47 µs，Taffy 和 Yoga 均小于 1 µs。
嵌套优势不能外推到内容驱动的父子固有尺寸传播；本轮没有测它。

### 二维 Grid，同算法

| 场景 | 首次：Axisly／GridCache／Taffy | 缩放：Axisly／GridCache／Taffy |
| --- | ---: | ---: |
| 固定 20×20 | 13.23／12.73／100.16 | 12.84／7.19／100.90 |
| 权重 20×20 | 15.86／15.30／287.79 | 15.79／9.95／287.82 |
| 权重 100×100 | 618.88／547.51／13,549.54 | 602.76／235.07／12,702.44 |
| 固定跨格 50×50 | 173.61／158.45／1,015.19 | 184.38／73.29／679.21 |
| 权重跨格 50×50 | 278.45／247.10／2,972.34 | 287.54／111.66／2,620.68 |

Grid 的无缓存 Axisly 在上述固定子集更快；输入不变时
`GridCache` 与 Taffy Grid 均小于 1 µs，无缓存 Axisly 仍重算。
这张表属于 Grid 算法，不能用来替代上面的 Flex 结论。

## 结果边界

本轮只证明上述共同子集的几何正确性与本机耗时。真实原生 UI 的工作负载
还应覆盖文本重排、局部节点改动、Grid／Flex 混合、Overlay、
更大深度和树创建／销毁成本。Axisly 目前没有统一的 Flex 保留树；
重复不变调用需要宿主跳过计算，否则不能获得 Taffy／Yoga 的树缓存收益。
平面场景中 Axisly Grid 的耗时不能代表 Axisly Flex 的耗时。

Taffy 官方 README 的历史基准已有 `yoga 'huge nested'`、
`big trees (deep)` 和 `super deep`，涵盖 3 至 1,000 层；
它使用 2021 年 M1 Pro 和当时的代码版本。本文 Windows／Taffy 0.14.0
的数值不能与该表直接作速度倍数比较：
[Taffy 官方 Benchmarks](https://github.com/DioxusLabs/taffy#benchmarks-vs-yoga)。

原始输入、逐批样本、全部几何校验、环境及程序 SHA-256、逐轮和汇总 CSV
保存在 [`benchmarks/comparison-axisly-2026-09-28`](benchmarks/comparison-axisly-2026-09-28/)。
复现命令（Windows PowerShell）：

```powershell
./scripts/build-msvc.ps1 -Preset comparison-release
python benchmarks/comparison/compare.py `
  --native build/comparison-release/axisly_comparison_native.exe `
  --taffy build/comparison-release/benchmarks/comparison/cargo/x86_64-pc-windows-msvc/release/axisly-taffy-bench.exe `
  --output build/comparison-results
```

机器为 Windows 11 x64、Intel Core i7-11700K；MSVC 19.51.36257、
Rust 1.98.0，Release 优化。构建和 `ctest` 的几何冒烟为
296/296 通过。正式轮次覆盖 14 个场景、60 个引擎／场景组合、
360 条逐轮／模式记录；全部 34 帧几何校验通过，最大绝对误差
0.00211（深树根坐标累积误差，在相对阈值内）。正式计时无速度阈值，
其完整记录见上述 CSV；不作为跨平台性能承诺。
