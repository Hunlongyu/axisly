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

## 结果与热点

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
单叶或单面板编辑时，384 叶树仍平均探测约 2400 次；外层 Grid 对
各面板的固有尺寸查询是后续优化对象。384 叶单叶编辑的申请次数为
9873.65，整树失效为 14377.4；两者窗口内峰值同为 466764 字节，
因为每次重新发布整棵树的结果。当前尚不能据此声称已经完成轨道
工作区与结果构造优化，也不能将这些数字推广到真实宿主文字。

## 验证范围

本次改动的 Release、Debug 参考构建各通过 333/333 项；ASan 通过
332/332 项；benchmark-release 通过 303/303 项。基准自身的几何与
分配 smoke 纳入 CTest。
已从 Release 安装前缀单独编译、链接并运行消费程序，验证混合
Grid→Flex→Block 树、宿主测量、结果复用和局部失效。P5 仍需
GeeyoouUI 的新 CSS 树页面和再次比较。
