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
| `display` | Block／FlowRoot／Flex／Grid／None，决定子树算法 | 部分：新树均可调度；Block／Flex／Grid 向嵌套父容器传递首基线，各算法尚未完整 | P2–P4 | 已建：mixed-block-flex-grid、hidden-and-positioned、nested-block／flex／grid-baseline |
| `item_is_table` | Block 子项表格标记；Taffy 未实现 Table 布局 | 部分：标记使 Block 子项不拉伸自动宽度并阻断父子边距折叠；复杂内在尺寸未完整 | P4 | 已建：block-table-intrinsic、block-table-margin-boundary |
| `item_is_replaced` | 替换元素的固有尺寸规则 | 部分：Block 测量叶子的自动宽度采用固有尺寸；叶子测量后的纵横比与约束、Flex 拉伸交叉轴传递到主轴基准、Grid 行区域对自动列的比例贡献已差分；更多跨算法组合未完整 | P1/P2/P3 | 已建：block-replaced-intrinsic、block-replaced-ratio、flex-replaced-ratio、grid-replaced-ratio |
| `box_sizing` | BorderBox／ContentBox | 部分：盒尺寸、Flex 及带 `calc` 宽度／盒边的 ContentBox 叶子已差分，嵌套复杂尺寸未完整 | P1 | 已建：content-box-and-negative-margin、calc-leaf-content-box |
| `direction` | Ltr／Rtl 的内联方向与 Grid 列方向 | 部分：Block/Flex/Grid 已接入方向规则；Grid/Flex 的 self-start／self-end 按项目自身方向解析；三种混合容器方向切换与 Block RTL 自动边距溢出已差分；根节点宽度小于确定的可用宽度时，RTL 根盒及后代按该空间右边定位；更多定位和内在尺寸组合未完整 | P1/P4 | 已建：rtl-block-child、rtl-mixed-block-flex-grid／grid-flex-block／flex-reverse、rtl-block-auto-overflow-both／left／right、grid-self-ltr-rtl、flex-self-rtl-ltr、test_grid_triple_span.py |
| `overflow` | x/y：Visible／Clip／Hidden／Scroll；0.14.0 无 Auto 变体 | 部分：Flex 与 Grid 单轨道自动最小尺寸依赖 overflow；新树输出节点局部可滚动溢出矩形，含滚动容器边界、RTL 与宿主报告的内容范围；复杂轴向与嵌套组合未完整 | P4 | 已建：overflow-scroll-padding、overflow-rtl-origin、overflow-hidden-boundary、overflow-unreachable、overflow-absolute-padding、grid-scroll-automatic-minimum |
| `scrollbar_width` | Scroll／Auto 所预留的数值宽度 | 部分：Scroll 轴预留空间并报告宽度；溢出范围与子项组合仍需差分 | P4 | 已建：flex-scrollbar-reservation、overflow-scroll-padding |
| `contain` | None／Layout／Paint／Content；0.14.0 的尺寸包含尚未实现 | 部分：Block 独立格式化上下文包含浮动；Layout／Content 抑制基线，Paint 保留基线；溢出细节仍未完整 | P4 | 已建：block-contain-float、nested-block／flex／grid-layout／paint-contain-baseline |
| `float` | None／Left／Right，Block 子项 | 部分：独立浮动区域模块计算左右浮动、clear、不同高度的纵向空隙、FlowRoot／Flex／Grid／滚动容器旁的剩余宽度及宽度不足时的下移；普通 Block 后代共享祖先浮动上下文，已验证后代自身浮动及缓存失效；FlowRoot 负外边距在浮动盒旁受边缘约束，移到下方后可越过容器边缘；自动宽度浮动 Block 查询子树固有宽度并累计内部浮动贡献；更深层嵌套及完整 CSS 浮动规则未验收 | P4 | 已建：block-float-clear、block-float-flow-root、block-float-fixed-wrap、block-float-both-sides、block-float-vertical-hole、block-float-negative-below、block-float-intrinsic-child、block-float-intrinsic-pair、block-float-intrinsic-flex、block-float-intrinsic-grid、block-float-matrix、inherited-float-invalidation |
| `clear` | None／Left／Right／Both | 部分：普通块按左右浮动底部清除；浮动盒自身清除按 Taffy 0.14 的已有浮动底部下移；双侧浮动与跨层普通 Block 后代组合已差分，复杂折叠未验收 | P4 | 已建：block-float-clear、block-float-self-clear、block-float-matrix |
| `position` | Relative／Absolute | 部分：相对偏移、绝对定位、Block 正常流静态纵坐标、Flex/Grid 基本静态对齐及 Grid 指定区域；绝对定位自动宽度 Block 查询内容并包含内部浮动；复杂静态位置和祖先包含块未完整 | P4 | 已建：hidden-and-positioned、grid-absolute-area、absolute-block-static-flow、absolute-block-auto-static、absolute-block-auto-floats、absolute-flex-static-center、absolute-grid-static-center |
| `inset` | 四边 Length／Percent／Auto／Calc | 部分：四边偏移、双边拉伸、绝对定位自动 margin 与 RTL 冲突求解；百分比偏移已覆盖 Block／Flex／Grid、边框内包含块和 Grid 指定区域；复杂百分比循环未完整 | P4 | 已建：absolute-auto-margins、absolute-rtl-overconstraint、absolute-percent-basic、absolute-percent-border、absolute-percent-grid-area |
| `size` | 双轴 Dimension：Length／Percent／Auto／固有关键字／Calc | 部分：叶子固有尺寸与约束、Block 自动宽度的 min-content／max-content 子树查询、Flex/Grid 根容器的自动内容宽度已接入；嵌套 Block／Flex／Grid 容器的 min-content／max-content／fit-content 与有参 fit-content 已按双轴差分，含三层混合算法、同节点双轴固有关键字、根节点尺寸查询、Block 正常流高度及局部缓存失效；独立 calc 参考树验证叶子双轴及嵌套 Block／Flex／Grid 的确定基准计算；循环百分比与更多跨字段组合未完整 | P1 | 已建：flex-row-grow、block-min-content-width、block-max-content-width、flex-auto-content-width、grid-auto-content-width、test_intrinsic_size.py、intrinsic_container_invalidation、calc-leaf-size／indefinite／partial-basis、calc-block-child、calc-flex-gap-basis、calc-grid-gap-item |
| `min_size` | 双轴 Length／Percent／Auto／Calc | 部分：Block/Flex/Grid 的已知尺寸先约束再排子项，Flex/Grid 自动高度受限后重排；Flex 项目基础尺寸与受上下界约束的假设尺寸分离；Flex 容器同时存在最小与最大主轴约束时按最小值确定初次分行上限；叶子 calc 最小尺寸已对照；跨轴与百分比循环未完整 | P1 | 已建：block-min-width-before-children、flex-min-width-before-children、grid-min-width-before-tracks、flex-auto-min-height、grid-auto-min-height、calc-leaf-min-max、test_flex_bounds.py |
| `max_size` | 双轴 Length／Percent／Auto／Calc | 部分：已知尺寸在三算法排子项前约束；Flex 自动主轴尺寸受上限限制初次分行，`min-content`／`max-content` 查询保留该分行，不在最终尺寸确定后重排；Flex 项目基础尺寸与受上下界约束的假设尺寸分离；叶子 calc 最大尺寸已对照；其他自动尺寸及复杂重排未完整 | P1/P2 | 已建：flex-auto-max-width-wrap、flex-auto-max-height-wrap、flex-auto-max-width-balance、calc-leaf-min-max、test_flex_bounds.py；待建：max-size-reflow |
| `aspect_ratio` | 可选宽高比 | 部分：叶子与容器分开解析比例约束；Taffy 0.14 的叶子最大尺寸不沿比例传递，容器最大尺寸传递，Flex 项目的比例最小值仅用于假设尺寸阶段，Grid 项目先将最小尺寸抬到盒边再决定最终盒；Block／Flex／Grid 的单轴指定值、内容盒、min/max 约束、未定根可用轴及混合树已差分，含 Grid 内部轨道基准与 RTL 溢出；更多替换元素、百分比及循环组合未完整 | P1–P3 | 已建：block-ratio-width、block-ratio-auto-width、flex-ratio-height、grid-ratio-content-box、leaf-intrinsic-ratio-max-height、flex-replaced-ratio-max-width、grid-replaced-ratio-max-width、test_ratio_matrix.py |
| `margin` | 四边 Length／Percent／Auto／Calc，允许负值 | 部分：Flex/Grid/Block 自动 margin 与 Block 父子、相邻及空盒连锁折叠；Flex 主轴正剩余空间分配与交叉轴正负剩余空间分配均输出已使用边距；Block 浮动旁及下方的负外边距、根容器未定宽度的百分比边距已差分；RTL Block 自动边距在正剩余空间与溢出时按物理右边定位，叶子 calc 盒边按内联基准解析；复杂折叠仍未完整 | P1/P4 | 已建：flex-auto-main-pair／cross-pair／overflow、flex-column-reverse-auto-margin、content-box-and-negative-margin、block-adjacent-margins、block-parent-child-margins、block-empty-margin-chain、rtl-block-auto-overflow-both／left／right、rtl-block-auto-positive／left／right、grid-auto-margins、block-float-negative-below、root-block／flex／grid-margin-percent、calc-leaf-margins |
| `padding` | 四边 Length／Percent／Calc | 部分：未定包含块宽度时按零解析布局边值，calc 整体保持未定而归零，同时保留尺寸百分比的未定状态；Block 自身宽度确定后重新解析用于子项排布，带 calc 内边距的确定宽度 Block 嵌套树已差分；自动宽度 Block/Flex 子项的百分比内边距保持未定，Grid 按网格区域宽度解析；嵌套 Block 的后续子项排布与叶子局部可滚动范围分别验收；更多百分比循环未完整 | P1 | 已建：flex-row-grow、root-block／flex／grid-padding-percent、calc-leaf-content-box／indefinite／partial-basis、calc-block-child、nested-percent-block／flex／grid、nested-percent-depth-block／flex／grid |
| `border` | 四边 Length／Percent／Calc | 部分：固定数值、叶子 calc 及根容器未定宽度的百分比边框已差分；Block 用确定后的边框作为子项和溢出坐标原点；嵌套循环未完整 | P1 | 已建：content-box-and-negative-margin、root-block／flex／grid-border-percent、calc-leaf-content-box／partial-basis |
| `align_items` | 交叉／块轴项目默认对齐，含基线与安全值 | 部分：Flex/Grid 常用位置、stretch、同一行基线及嵌套容器首基线；跨行反向换行、Grid 首行选择、滚动项目基线钳制及 Grid 安全对齐在溢出时回退起始边已差分，其他复杂组合未验收 | P2/P3 | 已建：flex-baseline、grid-baseline、flex-wrap-reverse-baseline、grid-first-row-baseline、flex-scroll-baseline、grid-scroll-baseline、grid-align-safe／unsafe-overflow |
| `align_self` | 单项目交叉／块轴覆盖值 | 部分：Flex/Grid 常用位置、stretch 与行基线；Flex 的 start／end／self-start／self-end 区分容器方向、项目方向和反向换行；固定交叉尺寸项目在反向换行时按 flex-start 对齐；复杂组合未验收 | P2/P3 | 已建：flex-self-ltr-rtl、flex-self-rtl-ltr、flex-wrap-reverse-physical-row／column、flex-wrap-reverse-baseline；待建：align-self-full |
| `justify_items` | Grid 子项内联轴默认对齐 | 部分：Grid 默认 End 与单项 Start 覆盖、SafeCenter／Center 在轨道小于项目时的位置已差分；非拉伸的嵌套 Grid 在自动轨道与弹性轨道中按区域可用空间和固有贡献确定尺寸；其他安全值与复杂组合未完整 | P3 | 已建：grid-justify-items-end、grid-justify-safe／unsafe-overflow、test_mixed_nested.py |
| `justify_self` | Grid 单项目内联轴覆盖值 | 部分：Grid 常用位置与 stretch，self-start／self-end 按项目自身方向解析；基线未完整 | P3 | 已建：grid-self-ltr-rtl、grid-self-rtl-ltr；待建：justify-self-full |
| `align_content` | 行／轨道整体交叉轴分布，含 space-* 与安全值 | 部分：Flex 多行与 Grid 固定轨道分布；Block 将正常流子项作为单组垂直对齐，含安全回退及浮动，同时其子树建立独立格式化上下文；作为祖先浮动旁的 Block 项目时，Taffy 仍允许它的盒与浮动重叠；Flex 的 Start／End 在反向换行时按书写方向对齐，FlexStart／FlexEnd 按行堆叠方向对齐；复杂组合未验收 | P2/P3/P4 | 已建：grid-content-alignment、648 组 Flex 方向／换行／对齐组合差分、block-align-matrix |
| `justify_content` | 主／内联轴整体分布，含 space-* 与安全值 | 部分：Flex 与 Grid 固定轨道分布；复杂组合未验收 | P2/P3 | 已建：grid-content-alignment |
| `gap` | 双轴 Length／Percent／Calc | 部分：Flex/Grid 双轴间隙；Flex 自动容器固有尺寸阶段以零为 `calc` 间隙百分比基准，保留固定长度项，尺寸确定后重算主轴分配；Grid 自动容器先按零间隙确定轨道尺寸，再重算偏移；Flex 未定交叉轴百分比行距保持为零；Grid 自动重复计数使用确定／最大／最小尺寸作为百分比间隙基准，复杂嵌套循环未完整 | P2/P3 | 已建：flex-row-grow、flex-auto-percent-gap、grid-auto-percent-gap、flex-auto-cross-percent-gap、grid-auto-row-percent-gap、calc-flex-gap-basis／auto-gap、calc-grid-gap-item／auto-gap、repeat-gap-fixed／max／min-fill |
| `text_align` | Auto／LegacyLeft／LegacyRight／LegacyCenter，Block 的传统内联对齐 | 部分：Block 子项 Legacy 对齐及 RTL 下 Left／Right／Center 的物理位置已差分；自动边距与嵌套组合未完整 | P4 | 已建：block-legacy-text-align、block-legacy-rtl-left／right／center |
| `flex_direction` | Row／RowReverse／Column／ColumnReverse | 部分：四种轴映射已接入；ColumnReverse 的居中、换列、RTL 反向换列与自动外边距已差分，复杂尺寸交互未验收 | P2 | 已建：flex-row-grow、flex-column-reverse-center／wrap／rtl-wrap-reverse／auto-margin |
| `flex_wrap` | NoWrap／Wrap／WrapReverse／Balance／BalanceReverse | 部分：五种换行策略已接入；自动尺寸容器先按父级可用空间及最大尺寸收集行，最小尺寸大于最大尺寸时采用最小尺寸作为上限；`min-content` 查询无确定上限时每项单独成行，之后保持初次分组；分行采用受项目 min/max 约束的假设尺寸，反向换行溢出时应用行对齐回退；五种策略均与四种主轴方向、RTL、项目和内容对齐在四种尺寸查询下差分；项目主轴上下界及负外边距与盒边的组合已差分，平衡换行的更多边界组合未验收 | P2 | 已建：flex-balance-lines、flex-auto-max-width-wrap、flex-auto-max-width-wrap-reverse、test_flex_matrix.py、test_flex_bounds.py、test_mixed_nested.py |
| `flex_line_count` | balance 等多行算法的最少行数 | 部分：平衡分行及自动尺寸受限后的重新分行已接入；两种 Balance 策略在最少一、二、三行时完成固定及自动尺寸组合差分，更多项目和边界组合未验收 | P2 | 已建：flex-balance-lines、flex-auto-max-width-balance、test_flex_matrix.py |
| `flex_basis` | Dimension 的 flex 初始主轴尺寸 | 部分：数值、百分比、固有关键字及 Flex 确定宽度容器中的 calc 基准已差分，显式与百分比 basis 已加入四种尺寸查询下的方向／五种换行策略／RTL 组合；小于盒边的 basis 抬至盒边下限，收缩权重使用扣除 padding 和 border 后的内容盒基准；列向固有贡献先计入负外边距再用 basis 设下限；Flex／Grid 混合树的测量叶节点已覆盖四种尺寸情形，更多嵌套与循环未完整 | P2 | 已建：flex-intrinsic-basis、calc-flex-gap-basis、test_flex_factors.py、test_flex_bounds.py、test_mixed_nested.py |
| `flex_grow` | 非负弹性增长系数 | 部分：增长、系数总和小于 1 与 min/max 冻结；四向主轴、五种换行策略、LTR／RTL 与显式／百分比 basis 已组合差分；复杂嵌套仍不足。Taffy 0.14 在系数和小于 1 时对初始剩余空间缩放后再次扣除行间隙；该版本差分已固定，但与 W3C 步骤 9.7 的规则不同，见下文差异记录 | P2 | 已建：flex-row-grow、flex-small-grow-factors、flex-grow-max-freeze、test_flex_factors.py |
| `flex_shrink` | 非负收缩系数，按基准尺寸缩放 | 部分：缩放、系数总和小于 1 与 min/max 冻结；四向主轴、五种换行策略、LTR／RTL 已组合差分；嵌套 Flex 收缩中按真实约束违例冻结，避免浮点舍入把未违例项目一起冻结；其他复杂嵌套仍不足。低系数与行间隙的版本行为见上项 | P2 | 已建：flex-small-shrink-factor、flex-shrink-min-freeze、test_flex_factors.py、test_mixed_nested.py |
| `grid_template_rows` | 固有／minmax／fit-content／fr／repeat 轨道 | 部分：新入口支持主要轨道函数与自动重复；自动重复行的百分比间隙与最小／最大高度组合已差分；容器固有高度确定后重新解析循环百分比轨道；`min-content` 查询下弹性轨道保留基准尺寸，`max-content` 按项目跨越轨道求 `fr` 单位，双轴固有查询已加入组合差分；自动重复的双轴、尺寸约束和空轨道折叠，以及固定／固有／百分比／弹性轨道的重叠跨格贡献已加入组合差分；其他尺寸阶段仍需差分 | P3 | 已建：css-grid-tracks-placement、grid-auto-fill／fit、grid-percent-indefinite-rows、repeat-gap-row-max／min、test_grid_matrix.py、test_grid_span_matrix.py、test_grid_repeat_matrix.py、test_grid_triple_span.py |
| `grid_template_columns` | 同上，含 auto-fill／auto-fit 与具名线 | 部分：自动重复与具名线已接入，百分比间隙及轨道在自动重复计数中按尺寸约束解析；跨格项目按跨度分批并分阶段计算最小贡献与固有内容尺寸，跨弹性轨道的单格和跨格项目合批规划基础尺寸，仅向弹性轨道分配该批的固有贡献；固有最大尺寸轨道共同分配可用空间并冻结增长上限；`fr` 的固有查询和低系数跨格贡献已有差分，`min-content` 查询不放大弹性轨道；容器固有宽度确定后重新解析循环百分比轨道；固定增长上限也参与非弹性轨道增长，自动重复的双轴、尺寸约束和空轨道折叠，以及固定／固有／百分比／弹性轨道的重叠跨格贡献已加入组合差分，复杂尺寸阶段仍未完整 | P3 | 已建：css-grid-tracks-placement、named-area-placement、grid-span-order-short-first／long-first、grid-equal-intrinsic-growth、grid-capped-intrinsic-growth、grid-scroll-automatic-minimum、grid-spanning-flex-minimum、grid-fr-small-min-query／max-query、grid-percent-indefinite-tracks、grid-fixed-max-pair、repeat-gap-fixed-percent-track、test_grid_matrix.py、test_grid_span_matrix.py、test_grid_repeat_matrix.py、test_grid_triple_span.py |
| `grid_auto_rows` | 隐式行轨道尺寸序列 | 部分：新入口支持显式网格后的正向循环及前方的逆向序列，含双侧隐式行；复杂放置组合未验收 | P3 | 已建：grid-implicit-positive-rows、grid-implicit-negative-rows、grid-implicit-mixed-rows |
| `grid_auto_columns` | 隐式列轨道尺寸序列 | 部分：新入口支持正向循环与显式网格前的逆向序列；复杂起点未验收 | P3 | 已建：grid-implicit-negative-lines、grid-negative-implicit-sequence |
| `grid_auto_flow` | Row／Column／RowDense／ColumnDense | 部分：四种放置方向已实现；按“确定双轴、确定次轴、自动游标”阶段排布，原始网格线先估算初始范围，确定位置的项目再扩张占用范围；稀疏／Dense 的行列方向与具名线、负隐式线、第二项目的确定主轴／次轴位置已组合差分，复杂跨格组合未完整 | P3 | 已建：grid-row-dense-holes、placement-row／column-secondary-first、placement-row／column-dense-secondary、placement-negative-column／row-start、placement-row／column-primary-backtrack、test_grid_line_matrix.py |
| `grid_template_areas` | 命名矩形区域 | 部分：矩形区域及起止线生成，已组合横向／纵向／二维矩形区域、显式轨道不足和重复模板，并在三种尺寸查询下差分；更复杂区域与跨格尺寸组合未完整 | P3 | 已建：named-area-placement、test_grid_area_matrix.py |
| `grid_template_column_names` | 显式列网格线名 | 部分：显式、重复和多次出现的线名可解析；缺失名称、超出显式范围的正负序号及带锚点的具名 span 已差分；端点组合矩阵覆盖列轴、LTR／RTL、四种自动流和拉伸项目，区域生成线名与重复、自动重复模板已组合差分，其他复杂放置未完整 | P3 | 已建：css-grid-tracks-placement、grid-named-line-occurrences、named-line-missing-positive／negative／forward-span／backward-span、named-line-existing-negative-third／positive-third／backward-span-first、test_grid_line_matrix.py、test_grid_area_matrix.py |
| `grid_template_row_names` | 显式行网格线名 | 部分：显式与区域生成的线名可解析；端点组合矩阵覆盖行轴的重复线名及缺失名称，区域生成线名与重复、自动重复模板已组合差分，其他复杂放置未完整 | P3 | 已建：named-area-placement、test_grid_line_matrix.py、test_grid_area_matrix.py |
| `grid_row` | 起止线、负索引、span、名称 | 部分：各输入可表达；起止反向时交换、相同线时形成一条轨道，区域与绝对定位通过固定差分；行轴端点矩阵覆盖自动／数字／负线／具名线／具名 span 及第二项目的确定位置，更多区域和固有尺寸组合未完整 | P3 | 已建：named-area-placement、grid-absolute-area、grid-line-reverse-row、test_grid_line_matrix.py |
| `grid_column` | 起止线、负索引、span、名称 | 部分：负索引、具名 span、隐式线与绝对区域；双 span 采用起始 span，孤立具名 span 化为一条轨道，起止反向／相同线规则已差分；列轴端点矩阵覆盖四种自动流与固定／拉伸项目，更多固有尺寸组合未完整 | P3 | 已建：grid-implicit-negative-lines、grid-named-line-occurrences、grid-line-reverse-column／negative／named、grid-line-equal-column、grid-line-both-spans、grid-line-named-span-start-auto／auto-end、test_grid_line_matrix.py |
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
| `block-float-vertical-hole`／`block-float-staggered-left`／`block-float-staggered-right`／`block-float-opposite-hole` | 不同高度的同侧及双侧浮动盒释放横向空隙，后续浮动盒按纵向带选位 |
| `block-float-clear-ceiling`／`block-float-zero-height` | 后续浮动盒不得高于前置浮动盒的顶部；零高度浮动盒不占用纵向带 |
| `block-float-hole-flow-root`／`block-float-hole-wide-flow-root` | 独立格式化上下文在浮动盒下方可用的纵向带选位；宽盒覆盖 Taffy 0.14 超出现存分段时的参考行为 |
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
| `grid-fit-fr-tracks`／`grid-minmax-fr-tracks`／`grid-minmax-auto-tracks` | fit-content、minmax、fr 与 auto 轨道组合的有限宽度分配 |
| `grid-percent-indefinite-tracks` | 未定宽度时百分比轨道先参与固有尺寸计算，再按确定的容器宽度解析百分比 |
| `grid-percent-indefinite-rows` | 未定高度时百分比行轨道按确定后的容器高度重新解析 |
| `grid-fit-pair-tracks` | 两条 fit-content 轨道的上限与未使用空间 |
| `grid-fit-percent-indefinite`／`grid-minmax-percent-min-indefinite`／`grid-minmax-percent-max-indefinite` | 未定宽度下百分比 fit-content 和 minmax 上下界在固有尺寸确定后重新解析 |
| `grid-fixed-max-single`／`grid-fixed-max-auto`／`grid-fixed-max-pair` | 固定增长上限的非弹性轨道在单轨、与 auto 混合及多轨分配中增长到上限 |
| `grid-implicit-positive-rows`／`grid-implicit-negative-rows`／`grid-implicit-mixed-rows` | 隐式行轨道序列在显式网格前后按方向循环 |
| `grid-self-ltr-rtl`／`grid-self-rtl-ltr` | Grid 项目自身方向与容器方向不同时，self-start／self-end 在内联轴反转 |
| `flex-self-ltr-rtl`／`flex-self-rtl-ltr` | Flex 列向项目自身方向与容器方向不同时，self-start／self-end 在交叉轴反转 |
| `flex-wrap-reverse-physical-row`／`flex-wrap-reverse-physical-column` | Flex 反向换行时 start／end 仍按书写方向，flex-start／flex-end 跟随反向交叉轴 |
| `leaf-intrinsic-ratio`／`leaf-intrinsic-ratio-max-height`／`leaf-intrinsic-ratio-max-width` | 叶子固有测量后按比例提高高度；宽度上限影响比例高度，Taffy 0.14 的最终比例高度可超过显式高度上限 |
| `block-replaced-ratio` | 替换 Block 测量叶子的固有宽度与比例高度 |
| `flex-replaced-ratio`／`flex-replaced-ratio-max-width` | Flex 的确定拉伸交叉轴经比例形成主轴基准，再按可用宽度和最大宽度收缩 |
| `grid-replaced-ratio`／`grid-replaced-ratio-fixed`／`grid-replaced-ratio-start`／`grid-replaced-ratio-max-width` | Grid 自动列通过行区域高度与比例获得固有贡献；固定列、显式 start 对齐及最大宽度改变结果 |
| `flex-auto-percent-gap`／`flex-fixed-percent-gap` | Flex 百分比主轴间隙在自动宽度计算时为零，确定宽度后重新伸缩项目；与固定宽度基准对照 |
| `grid-auto-percent-gap`／`grid-fixed-percent-gap` | Grid 百分比列间隙在自动宽度确定后重新形成轨道偏移，容器固有宽度不随之增加 |
| `flex-auto-cross-percent-gap`／`grid-auto-row-percent-gap` | 未定交叉轴百分比行距在 Flex 保持为零，在 Grid 高度确定后重新解析 |
| `block-float-negative-alongside`／`block-float-negative-below`／`block-float-negative-below-overwide`／`block-float-negative-trailing` | FlowRoot 的负外边距在浮动盒旁受边缘约束，移到下方可越过容器边缘；右浮动及负末端外边距与宽度不足时下移 |
| `nested-block-baseline`／`nested-flex-baseline`／`nested-grid-baseline` | 三种容器把首基线传给外层 Flex 的基线对齐；Grid 未参与内层基线对齐时使用首行项目的盒底 |
| `nested-block-layout-contain-baseline`／`nested-block-paint-contain-baseline`／`nested-flex-layout-contain-baseline`／`nested-flex-paint-contain-baseline`／`nested-grid-layout-contain-baseline`／`nested-grid-paint-contain-baseline` | Layout containment 抑制首基线，Paint containment 保留首基线，外层按边框盒底合成缺失基线 |
| `block-second-child-baseline`／`block-scroll-child-baseline` | Block 跳过无基线的首个流内子项；滚动子项的首基线钳制到边框盒 |
| `flex-wrap-reverse-baseline`／`flex-column-reverse-baseline` | Flex 容器从视觉起始行或列反向的起始项目生成首基线，固定交叉尺寸项目遵守反向换行的 flex-start |
| `grid-first-row-baseline`／`grid-aligned-baseline` | Grid 按行号选择首行，优先使用参与基线对齐的项目及其行内基线位移 |
| `flex-scroll-baseline`／`grid-scroll-baseline` | 滚动项目即使报告超过盒高的内容基线，参与基线对齐时也按边框盒高度钳制 |
| `root-block-padding-percent`／`root-block-border-percent`／`root-block-margin-percent` | Block 根的未定包含块宽度使报告的百分比边值为零；自身宽度确定后重新解析内边距和边框用于子项排布及溢出坐标 |
| `root-flex-padding-percent`／`root-flex-border-percent`／`root-flex-margin-percent`／`root-grid-padding-percent`／`root-grid-border-percent`／`root-grid-margin-percent` | Flex/Grid 根在未定包含块宽度下的百分比边值按零参与布局与报告，不把自身内容宽度误作包含块基准 |
| `repeat-gap-fixed-fill`／`repeat-gap-fixed-fit`／`repeat-gap-max-fill`／`repeat-gap-min-fill`／`repeat-gap-auto-fill`／`repeat-gap-fixed-percent-track` | 自动重复按固定、最大、最小或未定容器宽度选择次数；百分比间隙和百分比轨道参与计数，最小宽度约束参与初次轨道尺寸和最终容器尺寸；本地断言实际轨道条数 |
| `repeat-gap-row-max`／`repeat-gap-row-min` | 行自动重复按最大／最小高度及百分比行距计数，超出显式行的项目进入隐式行 |
| `placement-row-secondary-first`／`placement-column-secondary-first` | 确定次轴位置的项目优先占位，之后自动游标跳过已占用轨道；行列方向均差分 |
| `placement-row-dense-secondary`／`placement-column-dense-secondary` | Dense 模式在确定次轴的放置阶段和自动阶段重新从网格起点搜索 |
| `placement-negative-column-start`／`placement-negative-row-start` | 负方向隐式轨道形成自动放置游标的起点 |
| `placement-row-primary-backtrack`／`placement-column-primary-backtrack` | 稀疏流在主轴固定项目回到较早网格线时将次轴游标推进 |
| `grid-line-reverse-column`／`grid-line-reverse-negative`／`grid-line-reverse-named`／`grid-line-reverse-row` | 指定起止线反向时交换，覆盖数字、负线、命名线及双轴 |
| `grid-line-equal-column`／`grid-line-start-span`／`grid-line-both-spans` | 相同起止线形成一条轨道；起始 span 和结束线组合、双 span 冲突保留起始 span |
| `grid-line-named-span-start-auto`／`grid-line-named-span-auto-end` | 无确定另一端时，孤立具名 span 归一为一条轨道 |
| `grid-line-absolute-reverse-column`／`grid-line-absolute-reverse-row` | 绝对定位网格项的反向起止线先交换，再以所得区域作为对齐基准 |
| `named-line-missing-positive`／`named-line-missing-negative`／`named-line-missing-end-negative` | 缺失名称按 Taffy 0.14 的正负隐式线回退规则解析；结束线的负序号同样参与 |
| `named-line-existing-negative-third`／`named-line-existing-positive-third` | 名称存在但要求的出现次数超出显式网格范围时，按剩余次数推算隐式线 |
| `named-line-missing-forward-span`／`named-line-missing-backward-span`／`named-line-existing-backward-span-first` | 具名 span 在锚点一侧筛选候选线，缺失名称回退到隐式线；反向范围按冲突规则交换 |
| `rtl-mixed-block-flex-grid`／`rtl-mixed-grid-flex-block`／`rtl-mixed-flex-reverse` | 三种容器在嵌套方向切换、RTL Grid 列镜像和 Flex 反向主轴下的节点位置 |
| `grid-justify-items-end`／`grid-justify-safe-overflow`／`grid-justify-unsafe-overflow`／`grid-align-safe-overflow`／`grid-align-unsafe-overflow` | Grid 默认和项目覆盖对齐；安全位置在项目大于固定轨道时回退起始边 |
| `block-legacy-rtl-left`／`block-legacy-rtl-right`／`block-legacy-rtl-center` | RTL Block 的传统物理左、右和居中对齐 |
| `rtl-block-auto-overflow-both`／`rtl-block-auto-overflow-left`／`rtl-block-auto-overflow-right`／`rtl-block-auto-positive`／`rtl-block-auto-positive-left`／`rtl-block-auto-positive-right` | RTL Block 中双／单自动外边距在正剩余空间和溢出时的物理右边定位及报告边距 |
| `nested-percent-block`／`nested-percent-flex`／`nested-percent-grid` | 自动宽度容器的百分比内边距：Block/Flex 子项布局保持未定，Grid 项目按区域宽度解析；叶子的局部可滚动范围单独验收 |
| `nested-percent-depth-block`／`nested-percent-depth-flex`／`nested-percent-depth-grid` | 自动宽度容器下的三层树；Block 子项报告内边距与孙节点位置、容器高度及溢出范围分别差分 |

这些是参考工具的固定输入，预期来自 **Taffy 0.14.0 的锁定实现**。
后续若依据 CSS 规范、WPT 或浏览器补用例，必须在此注明规范版本／WPT 路径，
并将 Taffy 差异与 Axisly 错误分开记录。

## 树形参考协议

`axisly-taffy-reference` 保持旧 V1/V2 空白分隔协议；
`axisly-taffy-tree-reference` 单独读取 JSON `AXISLY_TREE_V1`；
`AXISLY_TREE_GRID_DETAILS_V1` 额外输出 Grid 的显式及正负隐式轨道数、
每条轨道的边框盒相对起止坐标、项目占用线号。
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

目前有 44 字段清单、二百六十三组可重放树形参考输入、十三组独立 calc
参考输入、独立树形工具及旧 Grid 差分回归；树形参考输入的矩形、盒边、
滚动条及节点局部可滚动溢出
同时经过新原生树差分；Grid 节点额外比较轨道位置、隐式范围和占用线号。
`test_flex_matrix.py` 另外在确定尺寸、自动尺寸配确定父级空间、
`max-content`、`min-content` 四种查询下各生成 1,944 组确定性的 Flex 组合：
四种主轴方向、五种换行／
平衡策略、三种项目对齐、三种主轴内容对齐、三种行对齐和两种方向；
平衡策略分别设置最少一、二、三行。该测试比较每个
节点的矩形、盒边、滚动条与溢出，项目尺寸固定。自动尺寸的平衡换行
额外核对按最少行数重算最大内容宽度后的容器尺寸。
`test_flex_bounds.py` 在确定父级空间、`max-content`、`min-content` 三种
查询下，分别组合四种主轴方向、五种换行、五组项目主轴尺寸与盒边配置、
LTR／RTL，以及明确尺寸、自动尺寸、自动尺寸受最大／最小主轴尺寸约束、
最小尺寸大于最大尺寸，共各 1,000 组。比较 flex 基础尺寸、假设尺寸、
冻结、负主轴外边距、padding／border、固有贡献和反向换行溢出的交互。
`min-content` 且无确定的换行上限时，每个项目各占一行；已形成的行分组
在容器尺寸确定后保持不变。
`test_flex_factors.py` 在确定尺寸、自动尺寸配确定父级空间、`max-content`、
`min-content` 四种查询下各组合四种主轴方向、五种换行／平衡策略、
八组 grow／shrink／flex-basis 输入和 LTR／RTL，共 1,280 组；特别覆盖系数和
小于 1 与行间隙交互、basis 小于盒边，以及 ContentBox／负外边距参与
固有贡献和实际布局尺寸的不同阶段。

`test_intrinsic_size.py` 逐轴组合 Block／Flex／Grid、min-content／max-content／
fit-content／三种有参 fit-content 长度和百分比、LTR／RTL、确定／最小／
最大内容根可用空间，以及 Block／Flex／Grid 父子算法的九种搭配和子容器
min/max 尺寸约束；还覆盖三层混合算法的 27 种组合和同节点双轴固有关键字，
共 2,142 组。除矩形和盒边外，同时比较节点局部溢出与
Grid 轨道。锁定的 Taffy 0.14 在根容器的尺寸查询中以根可用空间决定该轴
尺寸；普通 Block 父节点仅解析子项的宽度固有关键字，高度保持正常流内容
尺寸；Block／Flex 子容器的有参 fit-content 尺寸按最大内容结果输出，
Grid 子容器按参数钳制。这些版本行为由矩阵固定，不等同于 CSS 规范结论。
`intrinsic_container_invalidation` 另验证测量内容变化和关键字切换后重新
计算尺寸，输入不变时命中缓存。

`test_ratio_matrix.py` 组合三种根可用空间、Block／Flex／Grid 父子算法、
单轴已知尺寸、BorderBox／ContentBox、六种 min/max 约束和 LTR／RTL，
共 1,296 组，比较节点几何、局部溢出与 Grid 轨道。锁定的 Taffy 0.14
对容器最大尺寸沿比例传递，对叶子最大尺寸保持原轴约束；Flex 固有主轴
贡献使用未传递的样式下限，而分行使用传递后的假设尺寸；Grid 项目已
确定的外盒可以小于自身参与轨道计算的最小尺寸，RTL 溢出原点沿轨道
计算宽度确定。上述规则由参考矩阵固定，不能直接泛化为所有 CSS 实现。

`test_mixed_nested.py` 组合外层 Flex／Grid、三种项目对齐、四种主轴方向／
自动流、三种换行／轨道模板、内层 Flex／Grid、两种内层排列方向、两类内层
basis／增长策略、LTR／RTL 与明确尺寸、自动尺寸配确定父级空间、
`max-content`、`min-content` 四种尺寸情形，共 4,608 组。叶节点的最小内容与首选
内容尺寸不同；比较嵌套节点、叶节点的盒尺寸与滚动溢出，并对两级 Grid
比较轨道起止位置、显式及隐式范围和项目占用线号，验证固有约束
传递、交叉轴探测、非拉伸 Grid 子容器的尺寸确定及最终双轴布局。

`test_block_float_matrix.py` 生成 552 组 Block／Float 组合：根容器 Block／
FlowRoot、左右双侧浮动、四种 `clear`、普通 Block／FlowRoot／Flex／Grid
跟随节点、自动／固定宽度与 LTR／RTL；另有 128 组普通 Block 后代的浮动、
padding、外边距、方向与独立格式化上下文交互，以及 64 组三层 Block 的
跨层外边距折叠；再组合 104 组 Block `align-content` 的单组对齐、
空间不足时的安全回退与浮动，包括子树上下文边界。普通 Block 后代使用共享浮动上下文，
FlowRoot／Flex／Grid 使用独立上下文；差分比较父相对几何和滚动溢出。
`inherited_float_invalidation` 验证祖先浮动尺寸修改后，后代布局与缓存失效。
`plain_block_single_measure` 验证不含浮动的普通 Block 子树只调用一次宿主测量，
避免共享浮动上下文探测让一般布局重复计算。
复杂外边距折叠和更多深层嵌套仍待验收。

`test_grid_matrix.py` 生成 5,832 组 Grid 组合：九类双轨道模板分别用于行列，
含百分比上下限和固有尺寸混合；三种横向对齐、三种纵向对齐、LTR／RTL，
以及确定尺寸、自动尺寸配确定父级空间、`max-content`、`min-content`
四种查询。项目尺寸固定，比较节点几何及 Grid 详细轨道。
`test_grid_span_matrix.py` 增加跨两条行／列轨道的项目；同样九类双轨道模板
分别用于行列，包含 `0fr`、低系数 `fr`、百分比和固有上限，按上述四种
尺寸情形及 LTR／RTL 组成 1,296 组。跨弹性轨道项目按同一批规划基础
尺寸，且不增加非弹性轨道的增长上限；`max-content` 按每个项目跨越的轨道
求 `fr` 单位。固定最大值轨道以其增长上限计入跨格项目的余量；只有百分比
轨道且尺寸基准未定时，不把跨格项目作为固有轨道的最小贡献。这 1,296 组已
纳入 CTest 差分。
`test_grid_repeat_matrix.py` 组合 `auto-fill`／`auto-fit`、行／列轴、六类
固定／弹性／固有／百分比／双轨道模板、十种确定／自动／最小／最大尺寸与
父级可用空间组合、三种间隙、零至四个项目及 LTR／RTL，共 5,760 组。逐节点比较矩形、盒边、
溢出，并比较实际轨道、起止位置和项目占用线号。自动重复计数使用与参考
实现一致的单精度运算；固有尺寸阶段区分百分比解析基准与可用空间，有限
增长上限参与最大内容尺寸，`fr` 单位受容器最小／最大尺寸约束。
`test_grid_triple_span.py` 组合十二类三轨道模板、相互重叠的两轨道跨度与
真正跨三轨的项目；该项目的最小尺寸大于首选尺寸，覆盖完整项目集的四种
插入顺序和五种项目子集。行列双轴、LTR／RTL，以及明确尺寸、自动尺寸配
确定父级空间、`max-content`、`min-content` 四种查询共 1,728 组；
全部作为 CTest 差分回归，包含实际轨道尺寸、位置和项目占用线号。
跨格固有贡献先受单格项目形成的增长上限约束，同批项目只提交各轨道的
最大计划增量；零内容单格项目不创建有限增长上限。自动最小轨道在
`max-content` 查询下继续增长基础尺寸；固有尺寸查询下的最小贡献采用受限
`min-content`。增长上限先按最小内容、再按最大内容贡献分阶段分配，
有无限增长能力的轨道优先吸收缺额，`fit-content()` 参数限制其增长。
轨道尺寸计算分别传入
网格可用空间和容器内部尺寸；自动容器的百分比轨道在容器尺寸确定后
按 Taffy 的列重算与行基准钳制阶段处理；父级提供确定可用空间时保留
第一次尺寸结果。根节点的 RTL 位置按确定的可用宽度减去自身宽度计算。
`test_grid_line_matrix.py` 组合 12 种起始线和 12 种终止线、行／列轴、
LTR／RTL、四种自动流、第二项目的自动／确定次轴／确定主轴／确定双轴位置、
固定尺寸／自动拉伸尺寸及默认／双尺寸隐式轨道序列，共 36,864 组；
第三项目继续验证自动游标。矩阵按 12 个起始线分成独立 CTest，
每项 3,072 组；参考工具单次最多接受 1,024 组，脚本再分批执行。
比较全部节点的矩形、盒边、可滚动溢出、轨道起止位置和占用线号。
`test_grid_area_matrix.py` 组合三种矩形命名区域、五种轨道模板（含显式
轨道不足、具名线定次重复及具名线自动重复）、八种起止线、行／列轴、
LTR／RTL、四种自动流、固定或拉伸项目，以及确定／`max-content`／
`min-content` 查询，共 11,520 组。逐节点和逐轨道比较几何、占用线与
隐式轨道范围，验证区域生成线名与模板线名共存时的行为。
初始隐式网格范围
按原始数字端点估算，之后再解析具名线并扩张占用范围。
差分脚本将 Taffy 的父相对矩形换算为 Axisly 公共 API 使用的根相对矩形。
新 CSS `GridTracks` 同时报告实际轨道尺寸、边框盒相对位置、显式轨道数、
隐式范围和每个在流项目的起止线。上述矩阵、4,608 组混合嵌套树和
二百六十三组固定树输入已比较这些诊断；详细差分还发现并固定了空 Grid
误建隐式轨道、非拉伸纵横比项目没有扩大列轨道的问题。
复杂轨道取值组合和规范／WPT 精确用例索引仍未完成，不能据此验收 P0–P5。

Taffy 0.14 的 `TaffyTree` 对自定义 `calc` 句柄的解析回调固定返回 `0`，
而其 `CompactLength` 的 serde 反序列化明确拒绝 `calc`。树形参考工具仍拒绝
`calc` 输入。另建 `calc_reference.rs`，用自有 `LayoutPartialTree` 存活的表达式句柄
实现 `resolve_calc_value`，八组单节点和五组 Block／Flex／Grid 树形输入比较尺寸、
min/max、盒边、content-box、确定／未定基准、Flex basis／间隙、Grid 项目及 clamp。
更多固有尺寸、百分比循环和跨算法组合尚未验收，不能据此宣称全覆盖。

### 已裁决的参考实现差异

`flex-grow`／`flex-shrink` 系数和小于 1 且存在主轴 `gap` 时，锁定的
Taffy 0.14.0 `src/compute/flexbox.rs` 在已扣除间隙的初始剩余空间乘以系数和
之后，再减一次 `total_main_axis_gap`。Axisly 的 Taffy 兼容布局保留此结果，
由 `test_flex_factors.py` 固定。W3C
[Flexbox §9.7 第 5.2 步](https://www.w3.org/TR/css-flexbox-1/#resolve-flexible-lengths)只要求在
系数和小于 1 时乘以该和，并在缩放结果绝对值较小时使用它；没有第二次
扣除间隙。此处的 Taffy 差分通过不能作为该 CSS 规范步骤的通过证据。

Grid 具名 span 与负数字锚点的组合也有参考实现差异。在三个 20px 显式列
均以 `x` 命名边界线时，`grid-column: span 3 x / -1` 按
[CSS Grid §8.3 的负线与具名跨度规则](https://drafts.csswg.org/css-grid-1/#grid-placement)
应从第四条线向前数三条名为 `x` 的线，起点为第一条线。
可重放的浏览器用例见
[`tests/spec/grid_named_span_negative_anchor.html`](../tests/spec/grid_named_span_negative_anchor.html)。
本机 Chrome 无头布局在 5px 容器内边距下报告项目左边为 5px、跨度宽度
为 66px；锁定的
Taffy 0.14.0 在 `grid-line-matrix-10-3-0-0-0-0-0-0` 中报告 74px。
Taffy 的具名线解析先按负锚点原始一基坐标筛选候选线，且在向前 span 时
按候选线正序计数；Axisly 的 Taffy 兼容布局保留该版本结果。此项差分通过
不计入 CSS 规范符合性，后续规范／WPT 验收须单独标记。
