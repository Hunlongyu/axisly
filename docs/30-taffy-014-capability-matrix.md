# Taffy 0.14.0 布局字段与 Axisly 能力矩阵

本矩阵以锁定的 `taffy = 0.14.0` 源码 `src/style/mod.rs` 的 `Style` 为清单。
开发参考工具启用 `block_layout`、`float_layout`、`flexbox`、`flexbox_balance`、
`grid`、`calc`、`content_size` 和布局诊断。开发树形工具还启用 Taffy 的
`parse`，仅把单个 CSS 样式值转成 Taffy 类型；Axisly 生产库不依赖它。
绘制、文本塑形、DOM 与样式表级联不计入布局能力。独立树形工具
`tests/reference/taffy/src/bin/tree/`
可从实际编译的
`Style` 导出字段名；`test_tree.py` 会检查本表没有漏项或重复项。

**状态**：`部分`表示旧 Grid 入口存在有界的相近能力，绝不表示 CSS 语义等效；
`未支持`表示公开 API 无对应行为；`内部字段`不构成布局能力。
本表仅记录 2026-09-29 的现状。测试列中 `已建` 是 Taffy 参考输入，
不是 Axisly 通过差分；`待建` 是后续阶段必须落地的用例。

| Style 字段 | Taffy 0.14 输入与关键取值 | Axisly 现状 | 阶段 | 参考用例 |
| --- | --- | --- | --- | --- |
| `display` | Block／FlowRoot／Flex／Grid／None，决定子树算法 | 部分：GridTree 仅 Grid／叶子 | P2–P4 | 已建：mixed-block-flex-grid、hidden-and-positioned |
| `item_is_table` | Block 子项表格标记；Taffy 未实现 Table 布局 | 未支持 | P4 | 待建：block-table-item |
| `item_is_replaced` | 替换元素的固有尺寸规则 | 未支持 | P1 | 待建：replaced-intrinsic |
| `box_sizing` | BorderBox／ContentBox | 未支持 | P1 | 已建：content-box-and-negative-margin |
| `direction` | Ltr／Rtl 的内联方向与 Grid 列方向 | 未支持 | P1/P4 | 待建：rtl-mixed |
| `overflow` | x/y：Visible／Clip／Hidden／Scroll；0.14.0 无 Auto 变体 | 未支持 | P4 | 待建：overflow-min-size |
| `scrollbar_width` | Scroll／Auto 所预留的数值宽度 | 未支持 | P4 | 待建：scrollbar-reservation |
| `contain` | None／Layout／Paint／Content；0.14.0 的尺寸包含尚未实现 | 未支持 | P4 | 待建：contain-layout-paint |
| `float` | None／Left／Right，Block 子项 | 未支持 | P4 | 已建：block-float-clear |
| `clear` | None／Left／Right／Both | 未支持 | P4 | 已建：block-float-clear |
| `position` | Relative／Absolute | 未支持 | P4 | 已建：hidden-and-positioned |
| `inset` | 四边 Length／Percent／Auto／Calc | 未支持 | P4 | 已建：hidden-and-positioned；待建：percent-inset |
| `size` | 双轴 Dimension：Length／Percent／Auto／固有关键字／Calc | 部分：GridItem 的 preferred_size 仅作回退 | P1 | 已建：flex-row-grow；待建：intrinsic-size |
| `min_size` | 双轴 Length／Percent／Auto／Calc | 未支持：轨道下界不是节点最小尺寸 | P1 | 待建：min-max-node |
| `max_size` | 双轴 Length／Percent／Auto／Calc | 未支持：轨道上界不是节点最大尺寸 | P1 | 待建：min-max-node |
| `aspect_ratio` | 可选宽高比 | 未支持 | P1 | 待建：aspect-ratio |
| `margin` | 四边 Length／Percent／Auto／Calc，允许负值 | 部分：GridItem 仅非负数值 | P1 | 已建：content-box-and-negative-margin；待建：auto-margin |
| `padding` | 四边 Length／Percent／Calc | 部分：GridSpec 仅固定数值 | P1 | 已建：flex-row-grow；待建：percent-padding |
| `border` | 四边 Length／Percent／Calc | 未支持 | P1 | 已建：content-box-and-negative-margin |
| `align_items` | 交叉／块轴项目默认对齐，含基线与安全值 | 未支持：旧 API 仅逐条目对齐 | P2/P3 | 待建：align-items |
| `align_self` | 单项目交叉／块轴覆盖值 | 部分：GridItem 纵向对齐及首基线 | P2/P3 | 待建：align-self-full |
| `justify_items` | Grid 子项内联轴默认对齐 | 未支持：旧 API 仅逐条目对齐 | P3 | 待建：justify-items |
| `justify_self` | Grid 单项目内联轴覆盖值 | 部分：GridItem 水平四向对齐 | P3 | 待建：justify-self-full |
| `align_content` | 行／轨道整体交叉轴分布，含 space-* 与安全值 | 未支持 | P2/P3 | 待建：align-content |
| `justify_content` | 主／内联轴整体分布，含 space-* 与安全值 | 未支持 | P2/P3 | 待建：justify-content |
| `gap` | 双轴 Length／Percent／Calc | 部分：GridSpec 仅固定数值间隙 | P2/P3 | 已建：flex-row-grow；待建：percent-gap |
| `text_align` | Block 行内内容对齐 | 未支持 | P4 | 待建：block-text-align |
| `flex_direction` | Row／RowReverse／Column／ColumnReverse | 未支持 | P2 | 已建：flex-row-grow；待建：flex-reverse-column |
| `flex_wrap` | NoWrap／Wrap／WrapReverse／Balance／BalanceReverse | 未支持 | P2 | 已建：flex-balance-lines；待建：wrap-reverse |
| `flex_line_count` | balance 等多行算法的最少行数 | 未支持 | P2 | 已建：flex-balance-lines |
| `flex_basis` | Dimension 的 flex 初始主轴尺寸 | 未支持 | P2 | 待建：flex-basis-intrinsic |
| `flex_grow` | 非负弹性增长系数 | 未支持 | P2 | 已建：flex-row-grow |
| `flex_shrink` | 非负收缩系数，按基准尺寸缩放 | 未支持 | P2 | 待建：flex-shrink-freeze |
| `grid_template_rows` | 固有／minmax／fit-content／fr／repeat 轨道 | 部分：原生 Fixed／Weighted／Content／Range／Percentage 语义不同 | P3 | 已建：css-grid-tracks-placement；待建：css-grid-track-matrix |
| `grid_template_columns` | 同上，含 auto-fill／auto-fit 与具名线 | 部分：原生轨道类型较少且语义不同 | P3 | 已建：css-grid-tracks-placement；待建：css-grid-track-matrix |
| `grid_auto_rows` | 隐式行轨道尺寸序列 | 部分：GridSpec 只有一个循环轨道值 | P3 | 待建：implicit-track-sequence |
| `grid_auto_columns` | 隐式列轨道尺寸序列 | 部分：GridSpec 只有一个循环轨道值 | P3 | 待建：implicit-track-sequence |
| `grid_auto_flow` | Row／Column／RowDense／ColumnDense | 部分：四个流向存在，完整 CSS 放置规则未验收 | P3 | 待建：css-auto-placement |
| `grid_template_areas` | 命名矩形区域 | 未支持 | P3 | 待建：named-areas |
| `grid_template_column_names` | 显式列网格线名 | 未支持 | P3 | 已建：css-grid-tracks-placement；待建：named-lines |
| `grid_template_row_names` | 显式行网格线名 | 未支持 | P3 | 待建：named-lines |
| `grid_row` | 起止线、负索引、span、名称 | 部分：非负起点＋正跨度 | P3 | 已建：css-grid-tracks-placement；待建：negative-named-lines |
| `grid_column` | 起止线、负索引、span、名称 | 部分：非负起点＋正跨度 | P3 | 已建：css-grid-tracks-placement；待建：negative-named-lines |
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

当前 P0 已完成 44 字段清单、七组可重放参考输入、独立树形工具及旧
Grid 差分回归。全部字段的取值组合、规范／WPT 精确用例索引，以及
新原生树参与的差分仍待完成，因此 P0 尚未宣布验收完成。

Taffy 0.14 的 `TaffyTree` 对自定义 `calc` 句柄的解析回调固定返回 `0`，
而其 `CompactLength` 的 serde 反序列化明确拒绝 `calc`。因此树形参考工具
当前拒绝 `calc` 输入；要验收 `calc`，需新增实现
`LayoutPartialTree::resolve_calc_value` 的专用参考树，不能把返回 `0` 当作规范答案。
