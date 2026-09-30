# CSS 混合树的保留布局基准

2026-09-30，在 Windows 11／MSVC Release 上测量新 `css::LayoutTree`。
输入为 96 或 384 个测量叶子，每八个叶子组成一个 Block、Flex 或 Grid
面板，面板由外层 Grid 排列。合成回调与 Windows DirectWrite 后端均区分
最小内容、最大内容和受宽度约束的换行高度。DirectWrite 使用系统安装的
Consolas 16 px，实际创建文字布局并读取尺寸与首末基线；最小内容宽度是
各单词的最大实测宽度，最大内容宽度是整行不换行的实测宽度。它用于定位
布局和失效热点；GeeyoouUI 的 Blend2D 文字路径仍需独立验收。

## 方法

`targeted` 只标记变化叶子或面板；`full_invalidation` 每帧标记根。
两条路径从相同树、内容和可用空间出发。每个场景的前四帧逐节点比较
边框盒、内容盒、局部可滚动溢出、父子关系、Grid 轨道和项目占用线。
测量场景包括首次布局、输入不变、窗口宽度变化、单叶编辑、面板编辑及
交替编辑与缩放。首次布局的两种配置实际执行相同操作，用于检查配对噪声。

计时从内容变化或空间变化、失效通知之前开始，到 `compute` 返回且结果
基本检查完成时结束。建树、预热、完整几何验证、输出采样及销毁不计时。
分配程序在同一边界统计 C++ `new/delete` 的次数、申请字节数及窗口内峰值
存活字节；预热时已经存活的对象不计入峰值。DirectWrite／COM 内部未经过
C++ `new/delete` 的分配不在此计数中。计时程序使用普通分配器，
分配程序单独运行，**不**使用后者的时间作性能结论。每个批次 20 帧，
两种配置交替运行 9 批；p95 是批次平均时间的 95 百分位，非单帧尾延迟。
没有绑定 CPU，微秒以下的值尤其受计时分辨率及桌面负载影响。

```powershell
cmd /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars64.bat"" >nul && cmake --preset benchmark-release && cmake --build --preset benchmark-release"
build/benchmark-release/axisly_css_retained_bench.exe --iterations 20 --samples 9 --output build/css-timing-summary.csv --samples-output build/css-timing-raw.csv
build/benchmark-release/axisly_css_retained_allocations.exe --iterations 20 --samples 9 --output build/css-allocations-summary.csv --samples-output build/css-allocations-raw.csv
build/benchmark-release/axisly_css_retained_bench.exe --backend directwrite --iterations 20 --samples 9 --output build/css-directwrite-summary.csv --samples-output build/css-directwrite-raw.csv
build/benchmark-release/axisly_css_retained_allocations.exe --backend directwrite --iterations 20 --samples 9 --output build/css-directwrite-allocations.csv --samples-output build/css-directwrite-allocations-raw.csv
ctest --test-dir build/benchmark-release -R axisly.css_retained_bench --output-on-failure
```

原始记录：[时间汇总](benchmarks/css-retained-msvc-2026-09-30/timing-summary.csv)、
[逐批时间](benchmarks/css-retained-msvc-2026-09-30/timing-raw.csv)、
[分配汇总](benchmarks/css-retained-msvc-2026-09-30/allocations-summary.csv)、
[逐批分配](benchmarks/css-retained-msvc-2026-09-30/allocations-raw.csv)、
[DirectWrite 时间汇总](benchmarks/css-retained-msvc-2026-09-30/directwrite-timing-summary.csv)、
[DirectWrite 逐批时间](benchmarks/css-retained-msvc-2026-09-30/directwrite-timing-raw.csv)、
[DirectWrite 分配汇总](benchmarks/css-retained-msvc-2026-09-30/directwrite-allocations-summary.csv)、
[DirectWrite 逐批分配](benchmarks/css-retained-msvc-2026-09-30/directwrite-allocations-raw.csv)、
[环境与二进制哈希](benchmarks/css-retained-msvc-2026-09-30/environment.json)。

## 初始基线的结果与热点

下表是 9 批平均耗时的中位数，单位微秒；括号内是每帧测量次数。

| 叶子 | 场景 | 局部失效 | 整树失效 |
| ---: | --- | ---: | ---: |
| 96 | 首次 | 799.21（921） | 796.31（921） |
| 96 | 输入不变 | 0.030（0） | 734.61（921） |
| 96 | 缩放 | 500.03（588.5） | 735.59（921） |
| 96 | 单叶编辑 | 534.55（630.3） | 734.73（920.3） |
| 96 | 面板编辑 | 530.26（629.3） | 733.54（921.7） |
| 384 | 首次 | 3033.70（3686） | 3041.13（3686） |
| 384 | 输入不变 | 0.030（0） | 2967.04（3686） |
| 384 | 缩放 | 2016.44（2355） | 3000.43（3686） |
| 384 | 单叶编辑 | 2057.41（2403.4） | 2985.77（3686.4） |
| 384 | 面板编辑 | 2034.58（2395.9） | 2979.54（3686.7） |

真实 DirectWrite，96 叶，同样是 9 批平均耗时的中位数：

| 场景 | 局部失效 | 整树失效 |
| --- | ---: | ---: |
| 首次 | 2555.79（921） | 2564.94（921） |
| 输入不变 | 0.030（0） | 2446.69（921） |
| 缩放 | 1678.13（588.5） | 2495.31（921） |
| 单叶编辑 | 1706.21（622.0） | 2447.45（920.3） |
| 面板编辑 | 1728.43（631.3） | 2447.27（921.7） |

输入不变时直接复用上次发布的结果向量，测量、布局和分配均为零。
0.030 微秒只表示本机该批短调用的观测值，不能作为稳定的单次延迟承诺。
在这份初始基线中，单叶或单面板编辑时，384 叶树仍平均探测约 2400 次；
外层 Grid 对各面板的固有尺寸查询是后续优化对象。384 叶单叶编辑的申请次数为
9873.65，整树失效为 14377.4；两者窗口内峰值同为 466764 字节，
因为每次重新发布整棵树的结果。这份基线不能证明已经完成轨道
工作区与结果构造优化，也不能将这些数字推广到真实宿主文字。

## 混合树约束复核后的基准

四种根尺寸查询、四种嵌套 Flex 约束、Grid／Flex 两种外层算法组成的
9,216 组差分揭示了 Grid 轨道贡献需要按已确定的另一轴重测。
修正之后，首次布局比上表慢：384 叶合成回调从 3,033.70 µs 增至
9,666.15 µs。布局会为相同的叶节点建立多种约束；现在按节点保留
最多八种布局片段，并独立保留最多十六种测量请求结果。两类缓存
均按节点失效，只在整次计算成功后发布；全树容量由 `TreeLimits` 限制。

下表使用与初始基线相同的输入、计时边界和 20 帧 × 9 批方法。
时间是局部失效配置的批次均值中位数，括号中为每帧宿主测量次数：

| 后端／叶子 | 首次 | 尺寸变化 | 单叶编辑 | 面板编辑 |
| --- | ---: | ---: | ---: | ---: |
| 合成／96 | 2496.69（1017） | 49.29（0） | 205.59（22.8） | 237.72（84.25） |
| 合成／384 | 9666.15（4070） | 175.97（0） | 344.58（23.45） | 383.94（84.25） |
| DirectWrite／96 | 4978.93（1017） | 48.40（0） | 239.03（12.1） | 431.81（84.25） |

384 叶单叶编辑的整树失效对照是 9678.99 µs、4096 次测量；
局部编辑的 C++ 申请次数为 2446.9，整树失效为 47188.6。
但 384 叶首次布局需要 10065 次节点布局和 47164 次 C++ 申请，
相对初始基线仍有明显回退。DirectWrite 首次布局也从旧基线的
2555.79 µs 增至 4978.93 µs；测量结果复用降低了宿主调用，
尚未消除重复的布局片段构造。后续须在保持 9,216 组几何差分的条件下
优化 Grid 贡献查询及输出构造。

新原始记录：[时间汇总](benchmarks/css-retained-parity-2026-09-30/timing-summary.csv)、
[逐批时间](benchmarks/css-retained-parity-2026-09-30/timing-raw.csv)、
[分配汇总](benchmarks/css-retained-parity-2026-09-30/allocations-summary.csv)、
[逐批分配](benchmarks/css-retained-parity-2026-09-30/allocations-raw.csv)、
[DirectWrite 时间汇总](benchmarks/css-retained-parity-2026-09-30/directwrite-timing-summary.csv)、
[DirectWrite 逐批时间](benchmarks/css-retained-parity-2026-09-30/directwrite-timing-raw.csv)、
[DirectWrite 分配汇总](benchmarks/css-retained-parity-2026-09-30/directwrite-allocations-summary.csv)、
[DirectWrite 逐批分配](benchmarks/css-retained-parity-2026-09-30/directwrite-allocations-raw.csv)、
[环境与二进制哈希](benchmarks/css-retained-parity-2026-09-30/environment.json)。
申请次数和字节数仍要求各批完全一致；峰值存活字节可能因不同节点标识
导致容器释放顺序微变，汇总使用批次均值的中位数，逐批值保存在 CSV。

## 固有尺寸查询的输出分层

Grid 轨道贡献与 Flex 基础尺寸只消费子项尺寸。此前这些查询仍构造整棵
子树的位置、Grid 轨道报告和发布用的节点数组。现将内部查询分为尺寸、
基线和完整输出三种片段详细度：尺寸查询在结果确定后返回，Flex 行基线
仍用带基线的子片段，只有最终布局汇集所有后代和轨道。详细度参与缓存键；
完整片段可回答较小的查询，反向复用不允许。与前一份“混合树约束复核后”
基准相比，输入、后端、计时边界、20 帧 × 9 批和机器相同。

| 后端／叶子 | 场景 | 前一版 µs | 本版 µs | 前一版 C++ 申请 | 本版 C++ 申请 |
| --- | --- | ---: | ---: | ---: | ---: |
| 合成／384 | 首次 | 9666.15 | 8154.49 | 47164 | 40316 |
| 合成／384 | 单叶编辑 | 344.58 | 281.82 | 2446.9 | 1731.9 |
| DirectWrite／96 | 首次 | 4978.93 | 3680.68 | 12444 | 10604 |

DirectWrite 行的申请次数是该后端对应的记录；它不包含 DirectWrite／COM
内部未经过 C++ `new/delete` 的申请。合成／384 首次仍比更早的 3033.70 µs
基线慢；新算法执行更多交叉轴贡献查询，不能把本轮改进视为性能收尾。
缓存命中的尺寸变化与输入不变路径分别约 154.77 µs 和 0.035 µs；
其中尺寸变化仍需构造根输出。Release／Debug 参考测试各通过 344/344，
MSVC ASan 在 `vcvars64` 环境下通过 343/343；GeeyoouUI
Release／Debug／MSVC ASan 各通过 296/296，包含 9,216 组混合树差分。

新原始记录：[时间汇总](benchmarks/css-retained-detail-2026-09-30/timing-summary.csv)、
[逐批时间](benchmarks/css-retained-detail-2026-09-30/timing-raw.csv)、
[分配汇总](benchmarks/css-retained-detail-2026-09-30/allocations-summary.csv)、
[逐批分配](benchmarks/css-retained-detail-2026-09-30/allocations-raw.csv)、
[DirectWrite 时间汇总](benchmarks/css-retained-detail-2026-09-30/directwrite-timing-summary.csv)、
[DirectWrite 逐批时间](benchmarks/css-retained-detail-2026-09-30/directwrite-timing-raw.csv)、
[DirectWrite 分配汇总](benchmarks/css-retained-detail-2026-09-30/directwrite-allocations-summary.csv)、
[DirectWrite 逐批分配](benchmarks/css-retained-detail-2026-09-30/directwrite-allocations-raw.csv)、
[环境与可执行文件哈希](benchmarks/css-retained-detail-2026-09-30/environment.json)。

## 四层纵横比与百分比尺寸对照后

本轮修正 Block 子项的已知尺寸与百分比包含块传递、根 Block 的已知宽度，
以及 Grid 自动轨道对确定行高的纵横比传递。输入、后端、计时边界与上节
相同，按 20 帧 × 9 批重新采样；下面的时间均为批次中位数，分配次数来自
独立运行的 C++ 分配程序。

| 后端／叶子 | 场景 | 上版 µs | 本版 µs | 上版 C++ 申请 | 本版 C++ 申请 |
| --- | --- | ---: | ---: | ---: | ---: |
| 合成／384 | 首次 | 8154.49 | 8222.41 | 40316 | 40188 |
| 合成／384 | 单叶编辑 | 281.82 | 292.12 | 1731.9 | 1731.6 |
| DirectWrite／96 | 首次 | 3680.68 | 4442.64 | 10604 | 10572 |

合成／384 首次时间与上版接近，DirectWrite／96 本次首次时间上升约 20.7%；
这组数据未固定 CPU 亲和性，不能单凭一次采样归因于算法。尺寸变化仍为
0 次测量、根节点 1 次布局；输入不变仍复用已发布结果。Grid 的行确定性
检查只在存在纵横比候选项时执行，避免增加普通项目的热路径工作。
需要继续定位真实文字路径的耗时，并降低固有尺寸查询中的重复工作。

本轮原始记录：[时间汇总](benchmarks/css-ratio-deep-2026-09-30/timing-summary.csv)、
[逐批时间](benchmarks/css-ratio-deep-2026-09-30/timing-raw.csv)、
[分配汇总](benchmarks/css-ratio-deep-2026-09-30/allocations-summary.csv)、
[逐批分配](benchmarks/css-ratio-deep-2026-09-30/allocations-raw.csv)、
[DirectWrite 时间汇总](benchmarks/css-ratio-deep-2026-09-30/directwrite-timing-summary.csv)、
[DirectWrite 逐批时间](benchmarks/css-ratio-deep-2026-09-30/directwrite-timing-raw.csv)、
[DirectWrite 分配汇总](benchmarks/css-ratio-deep-2026-09-30/directwrite-allocations-summary.csv)、
[DirectWrite 逐批分配](benchmarks/css-ratio-deep-2026-09-30/directwrite-allocations-raw.csv)、
[环境与可执行文件哈希](benchmarks/css-ratio-deep-2026-09-30/environment.json)。
最终代码的参考 Release／Debug 各通过 345/345 项，MSVC ASan 通过
344/344 项；GeeyoouUI Release／Debug／MSVC ASan 各通过 296/296 项，
安装消费者通过 1/1 项，基准 smoke 通过 4/4 项。

## 根 Block 已知尺寸阶段复核

根 Block 现在先选取过约束 min/max、样式尺寸或确定的可用宽度，并把
已最终确定的尺寸轴传入子算法；普通 Flex 子项仍按自身 min/max 处理
父级传入的暂定尺寸。720 组根纵横比差分及原有差分均通过。基准输入、
计时边界与上节相同，仍为 20 帧 × 9 批；同一最终二进制另重复计时一次。

| 后端／叶子 | 场景 | 首轮 µs | 复轮 µs | C++ 申请／帧 |
| --- | --- | ---: | ---: | ---: |
| 合成／384 | 首次 | 11277.64 | 8592.32 | 40188 |
| 合成／384 | 单叶编辑 | 366.65 | 318.28 | 1731.6 |
| DirectWrite／96 | 首次 | 4391.42 | 6275.79 | 10572 |
| DirectWrite／96 | 单叶编辑 | 200.80 | 278.87 | 957.6 |

两轮相同输入和二进制的计时差异很大，不能将它归因于这次布局修改。
分配次数、测量次数及节点布局次数保持一致；真实文字首次布局的
性能问题仍未收尾。参考 Release／Debug 各通过 346/346，MSVC ASan
通过 345/345；GeeyoouUI 三种配置各通过 296/296，独立安装消费
通过 1/1，基准 smoke 通过 4/4。

原始记录：[合成首轮时间汇总](benchmarks/css-root-ratio-2026-09-30/timing-summary.csv)、
[逐批时间](benchmarks/css-root-ratio-2026-09-30/timing-raw.csv)、
[复轮时间汇总](benchmarks/css-root-ratio-2026-09-30/timing-repeat-summary.csv)、
[复轮逐批时间](benchmarks/css-root-ratio-2026-09-30/timing-repeat-raw.csv)、
[C++ 分配汇总](benchmarks/css-root-ratio-2026-09-30/allocations-summary.csv)、
[逐批分配](benchmarks/css-root-ratio-2026-09-30/allocations-raw.csv)、
[DirectWrite 首轮时间汇总](benchmarks/css-root-ratio-2026-09-30/directwrite-timing-summary.csv)、
[逐批时间](benchmarks/css-root-ratio-2026-09-30/directwrite-timing-raw.csv)、
[复轮时间汇总](benchmarks/css-root-ratio-2026-09-30/directwrite-timing-repeat-summary.csv)、
[复轮逐批时间](benchmarks/css-root-ratio-2026-09-30/directwrite-timing-repeat-raw.csv)、
[DirectWrite 分配汇总](benchmarks/css-root-ratio-2026-09-30/directwrite-allocations-summary.csv)、
[逐批分配](benchmarks/css-root-ratio-2026-09-30/directwrite-allocations-raw.csv)、
[环境与可执行文件哈希](benchmarks/css-root-ratio-2026-09-30/environment.json)。

## 验证范围

初始基线的 Release、Debug 参考构建各通过 333/333 项；ASan 通过
332/332 项；benchmark-release 通过 303/303 项。基准自身的几何与
分配 smoke 纳入 CTest。
混合树约束复核后的参考 Release／Debug 各通过 338/338，MSVC ASan
通过 337/337；GeeyoouUI Release／Debug／MSVC ASan 各通过 294/294。
基准的四项 smoke、独立安装消费程序的 1/1 项和真实 Showcase 窗口
的 CSS 混合页重排均通过。Taffy 的 9,216 组混合树差分属于参考
338 项中的四项 CTest；以上结果对应最后一次 Grid 贡献变化判断修改。
已从 Release 安装前缀单独编译、链接并运行消费程序，验证混合
Grid→Flex→Block 树、宿主测量、结果复用和局部失效。
GeeyoouUI 的真实控件与窗口验收见
[CSS 宿主记录](host-validation/geeyoou-css-2026-09-30/README.md)；
更多页面、真实多显示器 DPI 和已识别热点的后续优化仍未完成。
