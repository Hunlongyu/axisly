# GeeyoouUI 宿主接入验收计划

按用户选择，后续真实宿主验收使用 [sj7suren/GeeyoouUI](https://github.com/sj7suren/GeeyoouUI)。
现已完成可选构建、布局适配器、完整 Showcase 页面接入及本机窗口回归。
实现范围和验证边界见 [GeeyoouUI Showcase 接入记录](26-geeyoou-showcase.md)。
以下保留接入设计；跨显示器 DPI 切换和全面人工交互验收仍待完成。
核对版本为 `master` 的 `4cadaf6479b663ce6633e59bfbac28332de495fc`，许可证 MIT。
核对用源码放在忽略的 `build/GeeyoouUI-inspect`，不作为本项目的生产依赖。

## 演示范围：复用完整 Showcase

采用上游 `examples/showcase` 作为真实宿主演示，保留控件、绘制、交互与示例数据，
由 axisly 接管页面和容器的布局计算。这样可以直接使用复杂的真实控件树，
把开发工作集中在测量、排列、失效通知和生命周期适配上。

在上述提交中，[main.cpp](https://github.com/sj7suren/GeeyoouUI/blob/4cadaf6479b663ce6633e59bfbac28332de495fc/examples/showcase/main.cpp)
实际注册 **19 个页面**：概览、窗口外壳、主题与皮肤、图标库、HMI 监控、运维控制台、
布局引擎、基础控件、输入与按钮、下拉选择、对话框与新控件、七类表格、设备三维视图。
README 中的 18 页介绍和概览页中“尚无布局引擎”的文字均落后于这份源码，
不能据此判断当前布局能力。

| 范围 | 复用与迁移方式 |
| --- | --- |
| 控件、主题、图标、输入事件、弹窗、国际化、示例数据 | 保留上游实现，复用原有交互 |
| 导航栏、标题条、页面内容区域 | 用 axisly 表达主界面的容器关系；保留原生窗口壳行为 |
| 已用 BoxLayout／GridLayout 的页面 | 经示例层适配改由 axisly 计算；单行／单列可用网格表达，需明确 stretch、min／max 和间距语义 |
| 概览、窗口、主题、下拉选择、对话框等固定坐标页面 | 保留控件和事件，改写页面级容器、行列和尺寸约束；概览页作为首个可视验收页面 |
| HMI 工艺图、图表和 3D 视图 | 外层面板参与网格排列；图内工艺坐标、曲线和三维场景坐标保留 |
| 表格内部列宽、冻结列、虚拟滚动及控件内部绘制 | 保留控件自身算法，axisly 负责分配控件所在的矩形 |
| 布局引擎页面 | 调整说明与演示，补上 axisly 的百分比、自动放置、隐式行、基线和缓存失效场景 |

`PageOverview.cpp` 仍使用固定坐标摆放卡片和面板；截图中的概览页因此适合直接比较
接入前后缩放、文本换行和空间利用的变化。已有自动布局的表单及表格页则适合检验嵌套测量。
复用全部页面不等于已把全部页面迁移完成，验收报告需逐页记录迁移范围和状态。

依赖固定到上述上游提交，在本项目中维护适配器、页面覆盖文件及必要的最小补丁，
保留上游 MIT 许可证和版权声明。忽略目录中的调研副本不作为交付源码，
也不通过全局替换类名掩盖两套布局算法的语义差异。

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

1. 新增默认关闭的宿主演示构建入口，锁定上游提交，复用完整 Showcase 的源码与资源，
   关闭上游无关示例及测试的默认构建。先构建上游基线，确认 19 个页面可访问，
   再构建接入版本。核心 axisly 的安装导出和依赖保持 C++20/STL。
2. 在示例层实现派生 `geeyoou::Layout` 的适配器。维护稳定 ID 与宿主控件映射，
   从 `sizeHint` 和 `heightForWidth` 提供回调，成功后统一应用矩形。
3. 接通文本、字体、主题、DPI、显示状态和父子关系变化的失效通知。
   单叶子变化只失效必要路径；需核对宿主通知完整性，不能只比较控件地址。
4. 先迁移主界面和概览页，再迁移已有自动布局的表单、表格和嵌套面板，
   然后处理其余固定坐标页面的外层布局。保留完整导航和交互；在布局页面补充
   axisly 专有场景、动态增删、显示状态和文本更新演示。
5. 完成逐页自动回归和真实窗口验收，保存输入、布局矩形、测量次数、截图与构建信息。
   逐项说明与上游基线的预期差异，不把原有 BoxLayout 的像素分配规则当作 axisly 的通用规范。

## 必须明确的适配边界

- **最大尺寸**：`SizeHint` 有逐控件 max，axisly 的 `MeasureResult` 只有 min／preferred。
  必须明确宿主 Stretch／裁剪政策并验收；不能静默丢弃 max，也不能把某个控件的 max
  错误地当作整列的硬上限。
- **坐标与边距**：`arrange` 收到的 content 已处理宿主 margins；避免重复减边距、
  重复乘 DPI 或先取整后测量。文字测量保持逻辑尺寸，最终矩形再按设备像素处理。
- **重入和存活期**：上游明确允许 `sizeHint`／`setGeometry` 进入应用代码，可能删除控件、
  宿主或替换布局。每次回调后核对上游的存活保护及子项身份；计算失败不应用部分结果。
  `GridTree` 管理的是布局数据，不取得宿主控件所有权。
- **溢出政策**：上游布局倾向保持控件最小尺寸并报告 `LayoutOverflow`；与 axisly 的
  硬轨道／裁剪行为逐项对齐，给出可验证规则。

## 验收矩阵

- 全部 19 个页面可进入；保留按钮、输入、弹窗、表格编辑／滚动、实时数据和 3D 交互。
- 每个页面记录由 axisly 管理的容器和保留的内部坐标区域；实际计算路径确实经过 axisly。
- 主题和中英文切换后重建页面，布局、事件连接和对象生命周期正确。
- 100%、125%、150%、200% DPI，以及跨 DPI 窗口变化。
- 短／长／多行文本、窄宽度换行、字体和主题变化后的重新测量。
- 连续缩放、单标签修改、面板修改；核对缓存命中及无关分支的回调数量。
- 动态增删、重排、隐藏／显示、销毁或替换宿主；核对无悬空引用及过期几何。
- 固定、权重、Content、百分比、跨格、自动放置、隐式行、首基线和像素边缘。
- 正常尺寸、最小尺寸不足、最大尺寸限制及溢出报告。

达到完整验收矩阵后再评估正式 1.0；本次窗口回归仅覆盖接入记录中明确列出的配置，
先前的接口调研和 DirectWrite benchmark 不作为宿主窗口验收证据。
