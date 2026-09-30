# Taffy 0.14 对齐后的真实宿主验收

GeeyoouUI 固定提交 `4cadaf6479b663ce6633e59bfbac28332de495fc`。
Release、Debug、MSVC ASan 三种配置均通过 **320/320** 项；
原始 [Release](release-tests.log)、[Debug](debug-tests.log)、
[ASan](asan-tests.log) 记录及[二进制／截图摘要](validation.json)可查。
ASan 配置为 RelWithDebInfo，核心、适配器与 GeeyoouUI C++ 源码启用插桩；
不将第三方汇编或绘图库内部的所有路径计作已插桩。

每种配置通过原有 19 页 × 3 个窗口／语言／主题组合，共 57 次访问，
包括原生键盘输入、动态增删、内容变化、隐藏／显示和滚动。
[Release 页面记录](release-pages.csv)保留每次窗口尺寸与布局数量。

新增三种 CSS 代表页，与原有 Grid→Flex→Block 混合页一起在真实窗口中
访问全部四台已连接显示器。移动后等待原生 DPI 事件，再检查
`GetDpiForWindow()/96 == window.scaleFactor()`，并使用真实 Label 字体测量与绘制。
每个 CSS 页都检验宽度／内容／可见性变化及物理像素边缘。

| 页 | 验收内容 |
| --- | --- |
| Mixed | Grid→Flex／Block、长文字重测及后续项目位置 |
| Flow | 左右 float、FlowRoot 避让、clear:both 与文字换行 |
| Flex | balance 最小两行、不同字号、内容变化与合成基线 |
| Grid | 具名／负线、跨列、嵌套 auto-fit、绝对定位 inset |

GeeyoouUI 没有暴露文字基线；本适配器报告无基线，由布局算法以内容盒底部
合成。这不证明文字字体基线已由宿主读取。核心的显式基线回调另有数值与差分验收。

## 实际 DPI 覆盖

[原生窗口摘要](release-summary.json)记录四台显示器：
DISPLAY2、DISPLAY4 为 120 DPI（125%），DISPLAY1、DISPLAY3 为 96 DPI（100%）。
实际跨显示器只宣称这两种比例。独立宿主测试还检验 100%、125%、150%、200%
的逻辑像素取整，不把后两者作为实际显示器结果。

保存全部 16 张 Release CSS 截图；相同 DPI 下的页面在两台显示器上均通过。
已视觉检查 100%／125% 代表页的文字、浮动边界、字号、跨列及定位标记。

| 页 | 125% 示例 | 100% 示例 |
| --- | --- | --- |
| Flow | [截图](monitor-0-css-flow.png) | [截图](monitor-1-css-flow.png) |
| Flex | [截图](monitor-0-css-flex.png) | [截图](monitor-1-css-flex.png) |
| Grid | [截图](monitor-0-css-grid.png) | [截图](monitor-1-css-grid.png) |
| Mixed | [截图](monitor-0-css-mixed.png) | [截图](monitor-1-css-mixed.png) |

运行 `axisly_geeyoou_showcase.exe --smoke <输出目录>` 可重放，它会枚举运行时
已连接的显示器并生成页面、DPI、截图和摘要；显示器数量不写死在程序中。
本次原始证据保存脚本要求四台，是对已观察结果的校验。
