# GeeyoouUI 宿主接入验收计划

按用户选择，后续真实宿主验收使用 [sj7suren/GeeyoouUI](https://github.com/sj7suren/GeeyoouUI)。
本轮完成源码接口核对；宿主适配器、实际控件应用和窗口验收尚未实施。
核对版本为 `master` 的 `4cadaf6479b663ce6633e59bfbac28332de495fc`，许可证 MIT。
核对用源码放在忽略的 `build/GeeyoouUI-inspect`，不作为本项目的生产依赖。

## 已核对的接口

| GeeyoouUI 接口 | 接入用途 |
| --- | --- |
| `Layout::measure(const Widget&)` | 纯测量，提供宿主尺寸提示 |
| `Layout::arrange(Widget&, const Rect&)` | 在宿主给定内容矩形内安排直接子控件 |
| `Widget::sizeHint()` | 获取 min／preferred／max |
| `Label::heightForWidth(float)` | 使用宿主真实文本路径进行宽度约束下的换行测量 |
| `Widget::setGeometry(Rect)` | 应用计算完毕的矩形 |
| `Layout::invalidate()`、`Widget::performLayout()` | 与宿主失效及重排生命周期对接 |
| `Window::scaleFactor()` | 获取窗口 DPI 比例，验证逻辑坐标与物理像素关系 |

源码依据：[Layout](https://github.com/sj7suren/GeeyoouUI/blob/4cadaf6479b663ce6633e59bfbac28332de495fc/include/geeyoou/widget/Layout.hpp)、
[Widget](https://github.com/sj7suren/GeeyoouUI/blob/4cadaf6479b663ce6633e59bfbac28332de495fc/include/geeyoou/widget/Widget.hpp)、
[Label](https://github.com/sj7suren/GeeyoouUI/blob/4cadaf6479b663ce6633e59bfbac28332de495fc/include/geeyoou/widget/Label.hpp)、
[Window](https://github.com/sj7suren/GeeyoouUI/blob/4cadaf6479b663ce6633e59bfbac28332de495fc/include/geeyoou/widget/Window.hpp)。

## 接入顺序与交付

1. 新增默认关闭的宿主演示构建入口，锁定上游提交，关闭上游独立示例及测试的默认构建。
   核心 gridly 的安装导出和依赖保持 C++20/STL。
2. 在示例层实现派生 `geeyoou::Layout` 的适配器。维护稳定 ID 与宿主控件映射，
   从 `sizeHint` 和 `heightForWidth` 提供回调，成功后统一应用矩形。
3. 接通文本、字体、主题、DPI、显示状态和父子关系变化的失效通知。
   单叶子变化只失效必要路径；需核对宿主通知完整性，不能只比较控件地址。
4. 提供含标签、输入控件、按钮及嵌套面板的演示，支持动态增删和容器缩放。
5. 完成自动回归和真实窗口验收，保存输入、布局矩形、测量次数、截图与构建信息。

## 必须明确的适配边界

- **最大尺寸**：`SizeHint` 有逐控件 max，gridly 的 `MeasureResult` 只有 min／preferred。
  必须明确宿主 Stretch／裁剪政策并验收；不能静默丢弃 max，也不能把某个控件的 max
  错误地当作整列的硬上限。
- **坐标与边距**：`arrange` 收到的 content 已处理宿主 margins；避免重复减边距、
  重复乘 DPI 或先取整后测量。文字测量保持逻辑尺寸，最终矩形再按设备像素处理。
- **重入和存活期**：上游明确允许 `sizeHint`／`setGeometry` 进入应用代码，可能删除控件、
  宿主或替换布局。每次回调后核对上游的存活保护及子项身份；计算失败不应用部分结果。
  `GridTree` 管理的是布局数据，不取得宿主控件所有权。
- **溢出政策**：上游布局倾向保持控件最小尺寸并报告 `LayoutOverflow`；与 gridly 的
  硬轨道／裁剪行为逐项对齐，给出可验证规则。

## 验收矩阵

- 100%、125%、150%、200% DPI，以及跨 DPI 窗口变化。
- 短／长／多行文本、窄宽度换行、字体和主题变化后的重新测量。
- 连续缩放、单标签修改、面板修改；核对缓存命中及无关分支的回调数量。
- 动态增删、重排、隐藏／显示、销毁或替换宿主；核对无悬空引用及过期几何。
- 固定、权重、Content、百分比、跨格、自动放置、隐式行、首基线和像素边缘。
- 正常尺寸、最小尺寸不足、最大尺寸限制及溢出报告。

达到以上结果后再评估正式 1.0；当前接口调研和文本 benchmark 不作为宿主窗口验收通过的证明。
