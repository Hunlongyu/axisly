# Taffy 0.14.0 布局字段与 Axisly 能力矩阵

本矩阵以锁定的 `taffy = 0.14.0` 源码 `src/style/mod.rs` 的 `Style` 为清单。
开发参考工具启用 `block_layout`、`float_layout`、`flexbox`、`flexbox_balance`、
`grid`、`calc`、`content_size` 和布局诊断。开发树形工具还启用 Taffy 的
`parse`，仅把单个 CSS 样式值转成 Taffy 类型；Axisly 生产库不依赖它。
绘制、文本塑形、DOM 与样式表级联不计入布局能力。独立树形工具
`tests/reference/taffy/src/bin/tree/`
可从实际编译的
`Style` 导出字段名；`test_tree.py` 会检查本表没有漏项或重复项。

**状态**：`部分`表示新 CSS 树或旧 Grid 入口已覆盖部分取值和组合，
绝不表示 CSS 语义等效；`未支持`表示缺少对应行为；`内部字段`不构成布局能力。
本表记录 2026-09-29 的工作树现状。测试列中 `已建` 是 Taffy 参考输入；
其中列于末尾“原生树差分”的用例同时通过 Axisly 矩形对照。

| Style 字段 | Taffy 0.14 输入与关键取值 | Axisly 现状 | 阶段 | 参考用例 |
| --- | --- | --- | --- | --- |
| `display` | Block／FlowRoot／Flex／Grid／None，决定子树算法 | 部分：新树均可调度；各算法尚未完整 | P2–P4 | 已建：mixed-block-flex-grid、hidden-and-positioned |
| `item_is_table` | Block 子项表格标记；Taffy 未实现 Table 布局 | 部分：标记使 Block 子项不拉伸自动宽度并阻断父子边距折叠；复杂内在尺寸未完整 | P4 | 已建：block-table-intrinsic、block-table-margin-boundary |
| `item_is_replaced` | 替换元素的固有尺寸规则 | 部分：Block 测量叶子的自动宽度采用固有尺寸；纵横比与跨算法组合未完整 | P1 | 已建：block-replaced-intrinsic |
| `box_sizing` | BorderBox／ContentBox | 部分：盒尺寸与 Flex 差分已覆盖 | P1 | 已建：content-box-and-negative-margin |
| `direction` | Ltr／Rtl 的内联方向与 Grid 列方向 | 部分：Block/Flex/Grid 已接入基本方向规则，混合树未验收 | P1/P4 | 已建：rtl-block-child；待建：rtl-mixed |
| `overflow` | x/y：Visible／Clip／Hidden／Scroll；0.14.0 无 Auto 变体 | 部分：Flex 与 Grid 单轨道自动最小尺寸依赖 overflow；新树输出节点局部可滚动溢出矩形，含滚动容器边界、RTL 与宿主报告的内容范围；复杂轴向与嵌套组合未完整 | P4 | 已建：overflow-scroll-padding、overflow-rtl-origin、overflow-hidden-boundary、overflow-unreachable、overflow-absolute-padding、grid-scroll-automatic-minimum |
| `scrollbar_width` | Scroll／Auto 所预留的数值宽度 | 部分：Scroll 轴预留空间并报告宽度；溢出范围与子项组合仍需差分 | P4 | 已建：flex-scrollbar-reservation、overflow-scroll-padding |
| `contain` | None／Layout／Paint／Content；0.14.0 的尺寸包含尚未实现 | 部分：Block 独立格式化上下文包含浮动；溢出与基线规则未完整 | P4 | 已建：block-contain-float |
| `float` | None／Left／Right，Block 子项 | 部分：独立浮动区域模块计算左右浮动、clear、FlowRoot／Flex／Grid／滚动容器旁的剩余宽度及宽度不足时的下移；自动宽度浮动 Block 查询子树固有宽度并累计内部浮动贡献；复杂纵向环绕未验收 | P4 | 已建：block-float-clear、block-float-flow-root、block-float-fixed-wrap、block-float-both-sides、block-float-intrinsic-child、block-float-intrinsic-pair、block-float-intrinsic-flex、block-float-intrinsic-grid |
| `clear` | None／Left／Right／Both | 部分：普通块按左右浮动底部清除；浮动盒自身清除按 Taffy 0.14 的已有浮动底部下移；复杂折叠未验收 | P4 | 已建：block-float-clear、block-float-self-clear |
| `position` | Relative／Absolute | 部分：相对偏移、绝对定位、Block 正常流静态纵坐标、Flex/Grid 基本静态对齐及 Grid 指定区域；绝对定位自动宽度 Block 查询内容并包含内部浮动；复杂静态位置和祖先包含块未完整 | P4 | 已建：hidden-and-positioned、grid-absolute-area、absolute-block-static-flow、absolute-block-auto-static、absolute-block-auto-floats、absolute-flex-static-center、absolute-grid-static-center |
| `inset` | 四边 Length／Percent／Auto／Calc | 部分：四边偏移、双边拉伸、绝对定位自动 margin 与 RTL 冲突求解；百分比偏移已覆盖 Block／Flex／Grid、边框内包含块和 Grid 指定区域；复杂百分比循环未完整 | P4 | 已建：absolute-auto-margins、absolute-rtl-overconstraint、absolute-percent-basic、absolute-percent-border、absolute-percent-grid-area |
| `size` | 双轴 Dimension：Length／Percent／Auto／固有关键字／Calc | 部分：叶子固有尺寸与约束、Block 自动宽度的 min-content／max-content 子树查询、Flex/Grid 根容器的自动内容宽度已接入；复杂容器固有尺寸与循环百分比未完整 | P1 | 已建：flex-row-grow、block-min-content-width、block-max-content-width、flex-auto-content-width、grid-auto-content-width；待建：intrinsic-size |
| `min_size` | 双轴 Length／Percent／Auto／Calc | 部分：Block/Flex/Grid 的已知尺寸先约束再排子项，Flex/Grid 自动高度受限后重排；跨轴与百分比循环未完整 | P1 | 已建：block-min-width-before-children、flex-min-width-before-children、grid-min-width-before-tracks、flex-auto-min-height、grid-auto-min-height |
| `max_size` | 双轴 Length／Percent／Auto／Calc | 部分：已知尺寸在三算法排子项前约束；Flex 自动主轴尺寸受上限压缩后重算普通与平衡换行；其他自动尺寸及复杂重排未完整 | P1/P2 | 已建：flex-auto-max-width-wrap、flex-auto-max-height-wrap、flex-auto-max-width-balance；待建：max-size-reflow |
| `aspect_ratio` | 可选宽高比 | 部分：共用盒层传递首选及最小／最大尺寸；Block／Flex／Grid 的单轴指定值和自动内容宽度、border／content box 已差分；替换元素和复杂 min/max 组合未完整 | P1 | 已建：block-ratio-width、block-ratio-auto-width、flex-ratio-height、grid-ratio-content-box；待建：ratio-replaced |
| `margin` | 四边 Length／Percent／Auto／Calc，允许负值 | 部分：Flex/Grid/Block 自动 margin 与 Block 父子、相邻及空盒连锁折叠；clear／浮动等复杂组合未完整 | P1/P4 | 已建：content-box-and-negative-margin、block-adjacent-margins、block-parent-child-margins、block-empty-margin-chain、grid-auto-margins |
| `padding` | 四边 Length／Percent／Calc | 部分：共享盒模型解析；百分比未定组合未完整 | P1 | 已建：flex-row-grow；待建：percent-padding |
| `border` | 四边 Length／Percent／Calc | 部分：共享盒模型解析，差分覆盖固定数值 | P1 | 已建：content-box-and-negative-margin |
| `align_items` | 交叉／块轴项目默认对齐，含基线与安全值 | 部分：Flex/Grid 常用位置、stretch 与同一行基线；复杂跨行未验收 | P2/P3 | 已建：flex-baseline、grid-baseline |
| `align_self` | 单项目交叉／块轴覆盖值 | 部分：Flex/Grid 常用位置、stretch 与行基线；复杂组合未验收 | P2/P3 | 待建：align-self-full |
| `justify_items` | Grid 子项内联轴默认对齐 | 部分：Grid 常用位置与 stretch；安全值未完整 | P3 | 待建：justify-items |
| `justify_self` | Grid 单项目内联轴覆盖值 | 部分：Grid 常用位置与 stretch；基线未完整 | P3 | 待建：justify-self-full |
| `align_content` | 行／轨道整体交叉轴分布，含 space-* 与安全值 | 部分：Flex 多行与 Grid 固定轨道分布；复杂组合未验收 | P2/P3 | 已建：grid-content-alignment |
| `justify_content` | 主／内联轴整体分布，含 space-* 与安全值 | 部分：Flex 与 Grid 固定轨道分布；复杂组合未验收 | P2/P3 | 已建：grid-content-alignment |
| `gap` | 双轴 Length／Percent／Calc | 部分：Flex/Grid 双轴间隙；循环百分比未完整 | P2/P3 | 已建：flex-row-grow；待建：percent-gap |
| `text_align` | Auto／LegacyLeft／LegacyRight／LegacyCenter，Block 的传统内联对齐 | 部分：Block 子项 Legacy 对齐已接入；RTL 组合未验收 | P4 | 已建：block-legacy-text-align |
| `flex_direction` | Row／RowReverse／Column／ColumnReverse | 部分：四种轴映射已接入；复杂尺寸交互未验收 | P2 | 已建：flex-row-grow；待建：flex-reverse-column |
| `flex_wrap` | NoWrap／Wrap／WrapReverse／Balance／BalanceReverse | 部分：五种换行策略已接入；Flex 自动尺寸被 max 限制时按最终主轴尺寸重新分行；复杂组合未验收 | P2 | 已建：flex-balance-lines、flex-auto-max-width-wrap、flex-auto-max-width-wrap-reverse、flex-auto-max-width-balance-reverse |
| `flex_line_count` | balance 等多行算法的最少行数 | 部分：平衡分行及自动尺寸受限后的重新分行已接入；边界组合未验收 | P2 | 已建：flex-balance-lines、flex-auto-max-width-balance |
| `flex_basis` | Dimension 的 flex 初始主轴尺寸 | 部分：数值、百分比及固有关键字可探测；嵌套与循环未完整 | P2 | 已建：flex-intrinsic-basis |
| `flex_grow` | 非负弹性增长系数 | 部分：增长、系数总和小于 1 与 min/max 冻结；复杂组合覆盖不足 | P2 | 已建：flex-row-grow、flex-small-grow-factors、flex-grow-max-freeze |
| `flex_shrink` | 非负收缩系数，按基准尺寸缩放 | 部分：缩放、系数总和小于 1 与 min/max 冻结；复杂组合覆盖不足 | P2 | 已建：flex-small-shrink-factor、flex-shrink-min-freeze |
| `grid_template_rows` | 固有／minmax／fit-content／fr／repeat 轨道 | 部分：新入口支持主要轨道函数与自动重复；尺寸阶段仍需差分 | P3 | 已建：css-grid-tracks-placement、grid-auto-fill／fit |
| `grid_template_columns` | 同上，含 auto-fill／auto-fit 与具名线 | 部分：自动重复与具名线已接入；跨格项目按跨度分批并分阶段计算最小贡献与固有内容尺寸，固有最大尺寸轨道共同分配可用空间并冻结增长上限；`fr` 的固有查询和低系数跨格贡献已有差分，复杂尺寸阶段仍未完整 | P3 | 已建：css-grid-tracks-placement、named-area-placement、grid-span-order-short-first／long-first、grid-equal-intrinsic-growth、grid-capped-intrinsic-growth、grid-scroll-automatic-minimum、grid-spanning-flex-minimum、grid-fr-small-min-query／max-query |
| `grid_auto_rows` | 隐式行轨道尺寸序列 | 部分：新入口支持循环序列；复杂起点未验收 | P3 | 待建：implicit-track-sequence |
| `grid_auto_columns` | 隐式列轨道尺寸序列 | 部分：新入口支持正向循环与显式网格前的逆向序列；复杂起点未验收 | P3 | 已建：grid-implicit-negative-lines、grid-negative-implicit-sequence |
| `grid_auto_flow` | Row／Column／RowDense／ColumnDense | 部分：四种放置方向已实现；RowDense 补洞通过差分 | P3 | 已建：grid-row-dense-holes |
| `grid_template_areas` | 命名矩形区域 | 部分：矩形区域及起止线生成，通过固定差分 | P3 | 已建：named-area-placement |
| `grid_template_column_names` | 显式列网格线名 | 部分：显式、重复和多次出现的线名可解析；复杂隐式线未完整 | P3 | 已建：css-grid-tracks-placement、grid-named-line-occurrences |
| `grid_template_row_names` | 显式行网格线名 | 部分：显式与区域生成的线名可解析；多次出现未完整 | P3 | 已建：named-area-placement |
| `grid_row` | 起止线、负索引、span、名称 | 部分：各输入可表达；区域与绝对定位通过固定差分 | P3 | 已建：named-area-placement、grid-absolute-area |
| `grid_column` | 起止线、负索引、span、名称 | 部分：负索引、具名 span、隐式线与绝对区域通过差分 | P3 | 已建：grid-implicit-negative-lines、grid-named-line-occurrences |
| `dummy` | Rust 泛型编译占位字段 | 内部字段 | — | 无需用例 |

## 跨字段验收轴

单字段用例只能证明输入可表达。每个阶段还需覆盖有限／未定空间、零尺寸、
百分比循环、固有尺寸与已知尺寸重测、深层混合树、绝对定位、方向切换、
基线、内容溢出、缓存失效及错误路径。新树形参考协议将这些条件保存为
`tests/reference/fixtures/tree.json` 中可重放的 Taffy 输入。旧 V1/V2 差分只对
声明过的 Grid 共同子集作相等断言，不会因启用更多 Taffy 特性而扩大准入。

| 已建用例 | 来源与断言范围 |
| --- | --- |
| `flex-row-grow` | 由 Taffy 0.14 Style 的 Flex/gap/size 语义构造；断言根、两个子项矩形 |
| `mixed-block-flex-grid` | 混合 Block→Flex→Grid→测量叶及 FlowRoot；断言每级几何、输入顺序和父关系 |
| `block-float-clear` | Block float/clear；断言浮动盒与清除盒的纵向位置 |
| `hidden-and-positioned` | Display::None 与 Absolute/inset；断言隐藏项、定位项和正常流项 |
| `content-box-and-negative-margin` | ContentBox、padding、border 和负 margin；断言外盒尺寸及位置 |
| `flex-balance-lines` | Balance 与 flex_line_count；断言分成两行后项目位置 |
| `css-grid-tracks-placement` | Taffy 的 CSS 值解析器构造 `minmax`、`fr`、具名线和起止线；断言 Grid 子项位置 |
| `named-area-placement` | 区域边界采用 Taffy 的一基线号；断言显式线和区域隐式线的坐标 |
| `grid-auto-fill`／`grid-auto-fit` | 自动重复和空轨道折叠；断言根与项目矩形，本地测试另检查轨道尺寸 |
| `block-adjacent-margins` | 相邻垂直 margin 的折叠；断言第二个块的位置 |
| `grid-content-alignment` | 固定轨道在剩余空间中的 space-between／center 分布 |
| `flex-intrinsic-basis`／`flex-baseline` | 固有 Flex 基准尺寸与两个项目的首基线对齐 |
| `grid-baseline`／`grid-auto-margins` | 网格项目首基线对齐与自动外边距的已使用值 |
| `rtl-block-child`／`flex-scrollbar-reservation` | RTL Block 放置与 Flex 滚动条预留宽度 |
| `block-legacy-text-align`／`block-contain-float` | 传统对齐与 layout containment 包含浮动 |
| `grid-absolute-area`／`grid-implicit-negative-lines` | Grid 绝对定位区域、隐式轨道序列与负线号 |
| `grid-row-dense-holes`／`grid-named-line-occurrences` | Dense 补洞与重复具名线起止／span |
| `block-parent-child-margins`／`block-empty-margin-chain` | 父子边距与空 Block 连锁折叠；断言折叠后的盒边和根相对位置 |
| `block-negative-margin-chain` | 正负外边距跨空 Block 累积；验证最大正值与最小负值分别保留 |
| `block-padding-stops-collapse`／`block-fixed-height-collapse` | 垂直 padding 阻断折叠、固定高度仅允许顶部折叠 |
| `flex-small-grow-factors`／`flex-small-shrink-factor` | 弹性系数总和小于 1 时保留未分配空间 |
| `flex-grow-max-freeze`／`flex-shrink-min-freeze` | 多轮冻结后按剩余项目重新分配主轴空间 |
| `absolute-auto-margins`／`absolute-one-auto-margin`／`absolute-negative-auto-margins` | 绝对定位双侧 inset 与自动外边距的正负剩余空间 |
| `absolute-rtl-overconstraint` | RTL 包含块中左右 inset 冲突时使用右侧定位 |
| `absolute-flex-static-center`／`absolute-flex-static-end` | Flex 绝对定位子项无 inset 时依据容器对齐计算静态位置 |
| `absolute-grid-static-center`／`absolute-grid-static-end` | Grid 绝对定位子项无 inset 时依据项目对齐计算静态位置 |
| `absolute-block-static-padding`／`absolute-flex-static-padding-rtl`／`absolute-grid-static-padding` | 含 padding 的 Block/Flex 静态位置与 Grid 定位区域边界 |
| `absolute-block-static-flow` | Block 绝对定位子项在正常流中的静态纵坐标，不占用后续子项空间 |
| `block-min-width-before-children`／`flex-min-width-before-children`／`grid-min-width-before-tracks` | 三算法在排子项与轨道前应用容器最小宽度 |
| `flex-auto-min-height`／`grid-auto-min-height` | 自动高度受最小高度约束后重新分配 Flex 交叉轴与 Grid `fr` 轨道 |
| `grid-negative-implicit-sequence` | 显式网格前的隐式列按 `grid-auto-columns` 序列逆向取值 |
| `block-table-intrinsic`／`block-replaced-intrinsic` | Table 标记及替换元素的 Block 测量叶子采用固有宽度 |
| `block-table-margin-boundary` | Table 标记建立独立格式化边界，不与内部子块折叠外边距 |
| `overflow-scroll-padding`／`overflow-rtl-origin` | 滚动容器的末端 padding 与 RTL 节点的溢出原点；断言节点局部溢出边界 |
| `overflow-hidden-boundary`／`overflow-unreachable` | 子项裁剪边界及滚动容器中完全不可达的负方向子项；断言溢出传播 |
| `overflow-absolute-padding` | 普通流与绝对定位内容分别汇总，滚动容器末端 padding 不延伸绝对定位溢出 |
| `overflow-border-origin` | 子项溢出相对于父盒 padding 边缘计量，剔除 border 坐标偏移 |
| `grid-span-order-short-first`／`grid-span-order-long-first` | 2 跨格与 3 跨格项目交换源码顺序；断言固有轨道分配和项目矩形不变 |
| `grid-equal-intrinsic-growth`／`grid-capped-intrinsic-growth` | 两条 auto 轨道在有限宽度内共同增长；相同上限均分，较小上限先冻结 |
| `grid-visible-automatic-minimum`／`grid-scroll-automatic-minimum`／`grid-scroll-explicit-minimum` | Grid 单轨道项目的自动最小尺寸随溢出模式归零，显式最小尺寸仍生效 |
| `grid-spanning-flex-minimum`／`grid-spanning-auto-minimum`／`grid-spanning-scroll-minimum` | 跨弹性、普通 auto 轨道与滚动项目的最小贡献；按最小贡献和 min-content 阶段分别分配 |
| `grid-fr-small-min-query`／`grid-fr-small-max-query`／`grid-fr-weighted-min-query`／`grid-fr-weighted-max-query` | 不同 `fr` 系数下跨格项目的 min-content／max-content 固有查询；系数和小于 1 时按比例缩放贡献 |
| `grid-auto-min-query`／`grid-auto-max-query`／`grid-max-track-min-query` | `auto` 轨道在两种固有查询下采用不同增长阶段，`max-content` 最小轨道在 min-content 查询下仍保持最大内容尺寸 |
| `block-float-ordinary-overlap`／`block-float-flow-root`／`block-float-fixed-wrap` | 普通 Block 与浮动盒重叠；FlowRoot 使用旁侧空间，固定宽度放不下时下移 |
| `block-float-both-sides`／`block-float-margin-context`／`block-float-overflow-context` | 双侧浮动、外边距与滚动容器建立独立格式化上下文后的旁侧排布 |
| `block-float-flex-context`／`block-float-grid-context` | Flex 与 Grid 作为 Block 流中的独立格式化上下文避让浮动盒 |
| `block-float-self-clear` | 浮动盒自身设置 `clear` 后移至已有浮动盒下方 |
| `block-float-intrinsic-child`／`block-float-intrinsic-overflow` | 自动宽度的浮动 Block 从子项测量得到固有宽度，包括宽于父容器的情形 |
| `block-float-intrinsic-pair` | 自动宽度的浮动 Block 累计并排的两个内部浮动盒，且包含内部浮动高度 |
| `block-max-content-width`／`block-min-content-width` | 自动宽度 Block 对子树执行最大内容／最小内容宽度查询 |
| `block-float-intrinsic-flex`／`block-float-intrinsic-grid` | 浮动 Block 的内容宽度查询穿过 Flex／Grid 子树 |
| `block-ratio-width`／`block-ratio-height`／`block-ratio-auto-width` | Block 指定单轴或由可用宽度决定自动宽度时的纵横比传递 |
| `flex-ratio-width`／`flex-ratio-height`／`flex-ratio-auto-width` | Flex 容器指定单轴及自动宽度的纵横比与子项交叉轴排布 |
| `grid-ratio-width`／`grid-ratio-height`／`grid-ratio-auto-width` | Grid 容器指定单轴及自动宽度的纵横比与项目拉伸 |
| `block-ratio-content-box`／`flex-ratio-content-box`／`grid-ratio-content-box` | 纵横比按 content box 转换并加回内边距，区分 border-box 尺寸 |
| `flex-auto-content-width`／`grid-auto-content-width` | 根 Flex／Grid 在可用空间确定但自身宽度为 auto 时按内容确定宽度 |
| `flex-auto-max-width-wrap`／`flex-auto-max-height-wrap` | 自动主轴尺寸受 max 限制后，在行向和列向按最终尺寸重新分行 |
| `flex-auto-max-width-wrap-reverse`／`flex-auto-max-width-balance`／`flex-auto-max-width-balance-reverse` | 反向换行和平衡分行在自动主轴尺寸受限后重新分行 |
| `absolute-percent-basic`／`absolute-percent-border`／`absolute-percent-stretch` | 绝对定位百分比前后偏移、边框内包含块，以及双边百分比拉伸 |
| `relative-percent-basic`／`absolute-percent-flex`／`absolute-percent-grid`／`absolute-percent-grid-area` | 相对偏移与 Flex/Grid 绝对定位的百分比基准，含指定 Grid 区域 |
| `absolute-block-auto-static`／`absolute-block-auto-percent`／`absolute-block-auto-trailing` | 绝对定位 Block 自动宽度由内容决定，使用静态或百分比前后偏移 |
| `absolute-block-auto-floats` | 绝对定位 Block 的内部浮动贡献宽度和高度 |

这些是参考工具的固定输入，预期来自 **Taffy 0.14.0 的锁定实现**。
后续若依据 CSS 规范、WPT 或浏览器补用例，必须在此注明规范版本／WPT 路径，
并将 Taffy 差异与 Axisly 错误分开记录。

## 树形参考协议

`axisly-taffy-reference` 保持旧 V1/V2 空白分隔协议；
`axisly-taffy-tree-reference` 单独读取 JSON `AXISLY_TREE_V1`。
每个用例提供根的双轴可用空间和按父先于子的节点序列。`parent` 是父节点
在序列中的索引，根节点为 `null`。节点 `style` 只接受已知的 Taffy 字段；
数值长度表示逻辑像素，字符串长度使用 Taffy 0.14 的单值解析，
如 `"50%"`、`"min-content"`、`"fit-content(20px)"`。Grid 模板使用
轨道列表字符串，`grid_row`／`grid_column` 用包含 `start`、`end` 的对象。
叶节点的 `measure` 提供固定 min-content／首选内容尺寸与可选首基线；
这不代替后续按已知宽度重新排版的宿主测量协议。

工具关闭取整，输出每个节点**相对父节点**的矩形、盒边、滚动条占用、
可滚动溢出和顺序。输入中的重复 JSON 键、未知字段、非法父关系、
重复节点 ID、非有限值以及超出限制的用例都报错。输出仅是固定版本
Taffy 的参考结果；Axisly 尚未支持的 Block／Flex／CSS Grid 功能不计作通过。

目前有 44 字段清单、一百二十三组可重放参考输入、独立树形工具及旧 Grid
差分回归；这一百二十三组的矩形、盒边、滚动条及节点局部可滚动溢出
同时经过新原生树差分。
差分脚本将 Taffy 的父相对矩形换算为 Axisly 公共 API 使用的根相对矩形。
轨道尺寸、放置范围、
复杂取值组合和规范／WPT 精确用例索引仍未完成，不能据此验收 P0–P5。

Taffy 0.14 的 `TaffyTree` 对自定义 `calc` 句柄的解析回调固定返回 `0`，
而其 `CompactLength` 的 serde 反序列化明确拒绝 `calc`。因此树形参考工具
当前拒绝 `calc` 输入；要验收 `calc`，需新增实现
`LayoutPartialTree::resolve_calc_value` 的专用参考树，不能把返回 `0` 当作规范答案。
