# CSS 混合树的保留布局基准

2026-09-30，在 Windows 11／MSVC Release 上测量新 `css::LayoutTree`。
输入为 96 或 384 个测量叶子，每八个叶子组成一个 Block、Flex 或 Grid
面板，面板由外层 Grid 排列。合成文字回调区分最小内容、最大内容和受宽度
约束的换行高度。它用于定位布局和失效热点；真实 DirectWrite 测量与
GeeyoouUI 页面仍需作为单独的 P5 验收。

## 方法

`targeted` 只标记变化叶子或面板；`full_invalidation` 每帧标记根。
两条路径从相同树、内容和可用空间出发。每个场景的前四帧逐节点比较
边框盒、内容盒、局部可滚动溢出、父子关系、Grid 轨道和项目占用线。
测量场景包括首次布局、输入不变、窗口宽度变化、单叶编辑、面板编辑及
交替编辑与缩放。首次布局的两种配置实际执行相同操作，用于检查配对噪声。

计时从内容变化或空间变化、失效通知之前开始，到 `compute` 返回且结果
基本检查完成时结束。建树、预热、完整几何验证、输出采样及销毁不计时。
分配程序在同一边界统计 C++ `new/delete` 的次数、申请字节数及窗口内峰值
存活字节；预热时已经存活的对象不计入峰值。计时程序使用普通分配器，
分配程序单独运行，**不**使用后者的时间作性能结论。每个批次 20 帧，
两种配置交替运行 9 批；p95 是批次平均时间的 95 百分位，非单帧尾延迟。
没有绑定 CPU，微秒以下的值尤其受计时分辨率及桌面负载影响。

```powershell
cmd /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars64.bat"" >nul && cmake --preset benchmark-release && cmake --build --preset benchmark-release"
build/benchmark-release/axisly_css_retained_bench.exe --iterations 20 --samples 9 --output build/css-timing-summary.csv --samples-output build/css-timing-raw.csv
build/benchmark-release/axisly_css_retained_allocations.exe --iterations 20 --samples 9 --output build/css-allocations-summary.csv --samples-output build/css-allocations-raw.csv
ctest --test-dir build/benchmark-release -R axisly.css_retained_bench --output-on-failure
```

原始记录：[时间汇总](benchmarks/css-retained-msvc-2026-09-30/timing-summary.csv)、
[逐批时间](benchmarks/css-retained-msvc-2026-09-30/timing-raw.csv)、
[分配汇总](benchmarks/css-retained-msvc-2026-09-30/allocations-summary.csv)、
[逐批分配](benchmarks/css-retained-msvc-2026-09-30/allocations-raw.csv)、
[环境与二进制哈希](benchmarks/css-retained-msvc-2026-09-30/environment.json)。

## 结果与热点

下表是 9 批平均耗时的中位数，单位微秒；括号内是每帧测量次数。

| 叶子 | 场景 | 局部失效 | 整树失效 |
| ---: | --- | ---: | ---: |
| 96 | 首次 | 794.98（921） | 802.38（921） |
| 96 | 输入不变 | 0.025（0） | 740.28（921） |
| 96 | 缩放 | 502.51（588.5） | 731.50（921） |
| 96 | 单叶编辑 | 520.59（630.3） | 720.94（920.3） |
| 96 | 面板编辑 | 524.28（629.3） | 727.30（921.7） |
| 384 | 首次 | 2962.15（3686） | 2956.15（3686） |
| 384 | 输入不变 | 0.025（0） | 2944.42（3686） |
| 384 | 缩放 | 2016.06（2355） | 2951.07（3686） |
| 384 | 单叶编辑 | 2024.60（2403.4） | 2936.33（3686.4） |
| 384 | 面板编辑 | 2013.61（2395.9） | 2959.05（3686.7） |

输入不变时直接复用上次发布的结果向量，测量、布局和分配均为零。
0.025 微秒只表示本机该批短调用的观测值，不能作为稳定的单次延迟承诺。
单叶或单面板编辑时，384 叶树仍平均探测约 2400 次；外层 Grid 对
各面板的固有尺寸查询是后续优化对象。384 叶单叶编辑的申请次数为
9873.65，整树失效为 14377.4；两者窗口内峰值同为 466764 字节，
因为每次重新发布整棵树的结果。当前尚不能据此声称已经完成轨道
工作区与结果构造优化，也不能将这些数字推广到真实宿主文字。

## 验证范围

本次改动的 Release、Debug 参考构建各通过 333/333 项；ASan 通过
332/332 项；benchmark-release 通过 302/302 项。基准自身的几何与
分配 smoke 纳入 CTest。
已从 Release 安装前缀单独编译、链接并运行消费程序，验证混合
Grid→Flex→Block 树、宿主测量、结果复用和局部失效。P5 仍需
真实字体及 GeeyoouUI 的新 CSS 树页面和再次比较。
