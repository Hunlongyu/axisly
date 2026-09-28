# Axisly：原生 UI 通用布局能力与实施计划

## 已确定的方向

本项目面向原生 UI 的通用布局：宿主提供节点、可用空间和测量结果，C++20
核心计算尺寸与位置。核心不持有控件，不负责绘制、事件、文字排版或窗口
生命周期，默认构建继续只依赖 STL。

布局能力由现有 Grid 与新增 Flex 组合。Flex 采用 **CSS Flexbox 算法语义**，
通过类型化 C++ 接口输入，不做 CSS 文本解析、选择器、级联、DOM 或完整
浏览器渲染。目标涵盖 Flexbox 的全部规范布局边界，不仅是一套相似的
Row／Column 分配规则。现有 Grid 保持原有契约；增加 Flex 不等于现有
Grid 自动成为 CSS Grid 的完整实现。

项目名称确定为 **Axisly**。后续统一调整仓库内的命名空间、头文件、
CMake 包／目标、公开 API 和文档。验证通过也不发布。

## 当前实现与缺口

- Grid 的轨道、自动放置、跨度、测量、缓存和保留树已经可用；
  GeeyoouUI Showcase 的 19 页内容根已接入。
- GeeyoouUI 的 `GridlyBoxLayout` 为每个一维子项建立独占轨道。
  它可服务当前演示，但伸缩、换行和对齐不是 Flexbox。
- `GridTree` 的节点直接保存 `GridSpec`／`GridItem`，容器使用
  `GridCache`。混合 Grid／Flex 树需要抽取共同的树与缓存机制。
- `MeasureResult` 仅有最小／首选尺寸及可选基线。CSS 的 min-content、
  max-content、auto 最小尺寸、纵横书写模式及分片测量不能从现有字段
  推断，必须设计新的测量契约。

## 目标算法矩阵

| 能力 | 用途 | 决定 |
| --- | --- | --- |
| Grid | 两轴对齐、跨格、表单和仪表盘 | 保留现有实现与合法输入语义 |
| Flex | 一维排列、伸缩、多行换行和交叉轴对齐 | 按 Flexbox 规范实现全部布局边界 |
| 定位／分片辅助 | 绝对定位子项、分片容器所需的几何 | 作为 Flex 完整性依赖实现 |
| Overlay／Anchor | 非 CSS 的叠层和锚定 API | 后续从宿主案例决定 |

`flex-wrap` 本身负责 Flex 换行，不另设重复的 Flow 算法。普通纵向堆叠
可用 Column Flex 表达；CSS Block 格式化上下文并非通用原生 UI 的
必备布局类型。只有明确需要文档流、外边距折叠、浮动等行为时，才另行
设计 Block。表格内部列宽、文字排版与滚动条由宿主提供或处理。

## Flexbox 功能清单

以下清单涵盖 Flexbox Level 1 的布局相关章节；即使少见也纳入目标。
具体属性名通过 C++ 枚举／结构表达，不引入 CSS 字符串。

1. **盒与方向**：content／padding／border／margin 的尺寸关系，
   `row`／`row-reverse`／`column`／`column-reverse`，`nowrap`／
   `wrap`／`wrap-reverse`，LTR／RTL 与纵向 writing modes 的轴映射，
   `order` 及稳定的同序排列。
2. **尺寸与伸缩**：`flex-basis` 的 auto、content、长度、百分比及
   intrinsic 值；`flex-grow`／`flex-shrink`、flex base size、
   hypothetical main size、正负空间的冻结迭代、min／max、自动最小尺寸、
   aspect ratio 转移尺寸、确定与不确定尺寸、单行和多行 intrinsic 尺寸。
3. **对齐与行**：按规范收集 flex lines，主／交叉轴 gap、auto margin、
   `justify-content`、`align-items`、`align-self`、`align-content`、
   stretch、基线和多行交叉尺寸；子项最终宽度确定后重新测量高度。
4. **特殊子项**：绝对定位子项不参与 flex 分配，但计算规范要求的
   static-position rectangle；`visibility: collapse` 保留交叉轴 strut，
   重新执行换行；隐藏与不生成盒的输入由宿主明确区分。
5. **分片**：在宿主提供 fragmentainer、断点及子项可分片信息时，
   输出跨页／分栏的多个几何片段，并遵守 Flexbox 对行、项目与断点
   传播的要求。普通窗口布局不传分片上下文。
6. **异常与输出**：零尺寸、溢出、不可增长／收缩、百分比基准不确定、
   极端有限值、正交书写模式、空容器、重复计算与确定性。

完整 Flexbox 几何需要相邻 CSS 规范提供尺寸、书写模式、对齐、绝对定位、
分片和盒模型的规则。它们作为**布局数据和几何规则**实现；颜色、字体
样式、选择器、绘制层叠与浏览器 DOM 不进入核心。宿主为文本、表格或
替换元素提供测量和基线；布局核心不实现这些内容的排版。若规范要求
输出绘制顺序，结果可提供顺序元数据，由宿主绘制。

Flexbox 规范的部分文字仍留有开放问题：例如 9.9.1.2 标注的
web-compatible intrinsic sizing 算法尚未写出；10.1 的分片算法是
*informative* 示例。对这类没有唯一规范答案的点，先记录测试用例、
主流引擎行为和所选规则，再声明可验证的兼容范围；不能声称存在尚未
定义的唯一像素结果。

## 测量与公开 API 原则

1. 保留 `compute_grid`、`GridSpec`、`GridItem`、`GridCache` 的合法输入
   语义。Flex 先有独立无状态计算内核，再接入通用保留树。
2. Flex 测量接口支持 min-content、max-content、指定尺寸、可用空间、
   基线、aspect ratio、分片断点与正交书写模式。明确返回值属于
   content box 还是 border box，区分未知、已知零和无限可用空间。
3. 通用树共享节点身份、约束、结果、失效和取整机制，支持 Grid 中 Flex、
   Flex 中 Grid 及多层嵌套；保留 `GridTree` 的迁移入口。
4. 结果区分分配框、实际子项框、溢出范围、分片矩形和必要的顺序元数据。
   出错时不发布部分几何；回调异常按现有约定传播。
5. 数值校验拒绝 NaN／无限尺寸及无效约束。逻辑坐标计算完成后统一按
   设备像素取整。任何已支持输入都不得静默换成自定义 BoxLayout 规则。
6. `FlexSpec`／`FlexItem`／`compute_flex` 先作为设计名称；公开时统一
   使用 `axisly` 命名空间与 CMake 包名。

## 实施与验收里程碑

1. **规范矩阵**：逐章列出 Flexbox 的规范要求、相邻几何规则、类型化
   输入、宿主测量职责和可自动验证的结果；标注规范自身未定之处。
2. **测量契约**：加入 intrinsic、纵横书写模式、aspect ratio、
   baseline、fragmentation、绝对定位静态位置所需的探针和数据模型。
3. **无状态 Flex**：按规范顺序完成尺寸决定、伸缩冻结、换行与对齐；
   再覆盖绝对定位、collapse、纵向书写和分片。内部可分阶段开发，
   但首版验收不以「只支持常规流」作为完成条件。
4. **混合保留树**：验证 Grid／Flex 双向嵌套、局部失效、缓存命中、
   重入保护、失败原子性、测量次数及布局结果顺序。
5. **宿主验证**：在 GeeyoouUI 将代表性的工具栏、导航和竖向页面改用
   Flex 适配器，记录与当前单轴 Grid 的预期差异和不同 DPI 的表现。
6. **命名迁移**：统一仓库内名称、CMake 包／目标、命名空间、头文件及
   文档；明确现有 `gridly` 使用者的迁移方式。

验收以规范章节及 WPT 几何用例为主，抽取适合类型化输入的测试；再在
相同功能交集上与 Taffy、浏览器结果比对。Taffy 不是规范，也不成为
默认构建依赖。运行 MSVC Debug／Release、ASan、安装后
`find_package` 消费测试、真实 GeeyoouUI 窗口和 DPI 检查。
发布不属于这轮工作。

参考：

- [CSS Flexible Box Layout Module Level 1](https://www.w3.org/TR/css-flexbox-1/)
- [WPT Flexbox 测试](https://wpt.fyi/results/css/css-flexbox/)
- [Taffy 0.14 文档](https://docs.rs/taffy/0.14.0/taffy/)

## 命名决定

选择 `Axisly`，因为它不再把产品限定为 Grid，且延续短名称风格。
本决定仅覆盖仓库内代码、包名和文档；远端仓库地址是否更名在发布前
另行处理。当前无发布动作。
