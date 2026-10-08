# CSS 布局兼容性

Axisly 的 CSS 布局以锁定的 Taffy 0.14.0 为版本参照。
本表对应其 `Style` 的 43 个布局字段及 1 个内部字段，列出公开类型、实现范围与参考用例。
`已实现`表示有对应的类型化接口、算法和回归覆盖，不表示完整 CSS 规范认证。

参考测试启用 Block、浮动、Flexbox、均衡换行、Grid、calc、内容尺寸及布局诊断。
开发参考工具的单值解析功能仅用于输入转换，Axisly 生产库接收 C++ 类型。
字段清单由 [`test_tree.py`](../tests/reference/test_tree.py) 与实际编译的参考库自动校验。

## 字段映射

| Style 字段 | Taffy 0.14 输入与关键取值 | Axisly 现状 | 范围 | 参考用例 |
| --- | --- | --- | --- | --- |
| `display` | Block／FlowRoot／Flex／Grid／None，决定子树算法 | 已实现：Style.display 调度五种算法／隐藏树；混合树、固有尺寸、基线及失效已验收 | 通用布局 | 已建：mixed-block-flex-grid、hidden-and-positioned、nested-block／flex／grid-baseline |
| `item_is_table` | Block 子项表格标记；Taffy 未实现 Table 布局 | 已实现：Style.item_is_table 映射 Block 表格标记的固有宽度及折叠边界；不提供 Table 算法 | Block | 已建：block-table-intrinsic、block-table-margin-boundary |
| `item_is_replaced` | 替换元素的固有尺寸规则 | 已实现：Style.box.replaced；替换叶子、比例传递、自动最小值及跨算法 calc 查询已验收 | 通用布局 | 已建：block-replaced-intrinsic、block-replaced-ratio、flex-replaced-ratio、grid-replaced-ratio |
| `box_sizing` | BorderBox／ContentBox | 已实现：Style.box.box_sizing；统一边框盒／内容盒约束、装饰和滚动条空间 | 通用布局 | 已建：content-box-and-negative-margin、calc-leaf-content-box、test_scrollbar_matrix.py |
| `direction` | Ltr／Rtl 的内联方向与 Grid 列方向 | 已实现：Style.box.direction；LTR／RTL、父子方向、自身对齐、根坐标与滚动条起点 | 通用布局 | 已建：rtl-block-child、rtl-mixed-block-flex-grid／grid-flex-block／flex-reverse、rtl-block-auto-overflow-both／left／right、grid-self-ltr-rtl、flex-self-rtl-ltr、test_grid_triple_span.py、test_scrollbar_matrix.py |
| `overflow` | x/y：Visible／Clip／Hidden／Scroll；0.14.0 无 Auto 变体 | 已实现：Style.overflow_x/y；四种取值、自动最小尺寸及逐节点可滚动范围 | 通用布局 | 已建：overflow-scroll-padding、overflow-rtl-origin、overflow-hidden-boundary、overflow-unreachable、overflow-absolute-padding、grid-scroll-automatic-minimum、wpt-flex-cross-scroll、test_overflow_matrix.py |
| `scrollbar_width` | `Overflow::Scroll` 所预留的数值宽度；x/y 转置为垂直／水平滚动条空间 | 已实现：Style.scrollbar_width；双轴预留、RTL、小于装饰尺寸及嵌套比例 | 通用布局 | 已建：flex-scrollbar-reservation、overflow-scroll-padding、test_scrollbar_matrix.py |
| `contain` | None／Layout／Paint／Content；0.14.0 的尺寸包含尚未实现 | 已实现：Style.contain；格式化上下文、浮动包含、基线及溢出传播；Size containment 不在 Taffy 0.14 范围 | 通用布局 | 已建：block-contain-float、nested-block／flex／grid-layout／paint-contain-baseline、test_contain_matrix.py |
| `float` | None／Left／Right，Block 子项 | 已实现：Style.float_side；共享 Block 浮动上下文、独立格式化上下文、双侧区域和固有宽度 | Block | 已建：block-float-clear、block-float-flow-root、block-float-fixed-wrap、block-float-both-sides、block-float-vertical-hole、block-float-negative-below、block-float-intrinsic-child、block-float-intrinsic-pair、block-float-intrinsic-flex、block-float-intrinsic-grid、block-float-matrix、inherited-float-invalidation |
| `clear` | None／Left／Right／Both | 已实现：Style.clear；四种取值、空块 clearance、折叠边距状态及浮动自身清除 | Block | 已建：block-float-clear、block-float-self-clear、block-float-matrix |
| `position` | Relative／Absolute | 已实现：Style.position；相对偏移、绝对尺寸求解、静态对齐及 Grid 区域 | 通用布局 | 已建：hidden-and-positioned、grid-absolute-area、absolute-block-static-flow、absolute-block-auto-static、absolute-block-auto-floats、absolute-flex-static-center、absolute-grid-static-center、test_position_matrix.py |
| `inset` | 四边 Length／Percent／Auto／Calc | 已实现：Style.inset；数值／百分比／auto／calc、双边拉伸、方向冲突与定位包含块 | 通用布局 | 已建：absolute-auto-margins、absolute-rtl-overconstraint、absolute-percent-basic、absolute-percent-border、absolute-percent-grid-area、test_position_matrix.py |
| `size` | 双轴 Dimension：Length／Percent／Auto／固有关键字／Calc | 已实现：Style.box.size；数值／百分比／auto／固有关键字／calc，明确区分已知零和未定尺寸 | 通用布局 | 已建：flex-row-grow、block-min-content-width、block-max-content-width、flex-auto-content-width、grid-auto-content-width、test_intrinsic_size.py、intrinsic_container_invalidation、test_position_matrix.py、calc-leaf-size／indefinite／partial-basis、calc-block-child、calc-flex-gap-basis、calc-grid-gap-item |
| `min_size` | 双轴 Length／Percent／Auto／Calc | 已实现：Style.box.min_size；下限、自动最小值、循环百分比及 calc | 通用布局 | 已建：block-min-width-before-children、flex-min-width-before-children、grid-min-width-before-tracks、flex-auto-min-height、grid-auto-min-height、calc-leaf-min-max、test_flex_bounds.py |
| `max_size` | 双轴 Length／Percent／Auto／Calc | 已实现：Style.box.max_size；上限、过约束钳制、替换元素与 calc | 通用布局 | 已建：flex-auto-max-width-wrap、flex-auto-max-height-wrap、flex-auto-max-width-balance、calc-leaf-min-max、test_flex_bounds.py、test_mixed_nested.py 全组合 9,216 组 |
| `aspect_ratio` | 可选宽高比 | 已实现：Style.box.aspect_ratio；已知单轴、拉伸、替换元素及 Grid 轨道贡献 | 通用布局 | 已建：block-ratio-width、block-ratio-auto-width、flex-ratio-height、grid-ratio-content-box、leaf-intrinsic-ratio-max-height、flex-replaced-ratio-max-width、grid-replaced-ratio-max-width、test_ratio_matrix.py |
| `margin` | 四边 Length／Percent／Auto／Calc，允许负值 | 已实现：Style.box.margin；auto／负值／百分比／calc、Block 折叠及 Flex/Grid 分配 | 通用布局 | 已建：flex-auto-main-pair／cross-pair／overflow、flex-column-reverse-auto-margin、content-box-and-negative-margin、block-adjacent-margins、block-parent-child-margins、block-empty-margin-chain、rtl-block-auto-overflow-both／left／right、rtl-block-auto-positive／left／right、grid-auto-margins、block-float-negative-below、root-block／flex／grid-margin-percent、calc-leaf-margins、test_position_matrix.py |
| `padding` | 四边 Length／Percent／Calc | 已实现：Style.box.padding；共用解析、未定基准与父项目报告阶段；版本元数据差异单列 | 通用布局 | 已建：flex-row-grow、root-block／flex／grid-padding-percent、calc-leaf-content-box／indefinite／partial-basis、calc-block-child、nested-percent-block／flex／grid、nested-percent-depth-block／flex／grid |
| `border` | 四边 Length／Percent／Calc | 已实现：Style.box.border；装饰下限、内容空间、溢出坐标与 calc | 通用布局 | 已建：content-box-and-negative-margin、root-block／flex／grid-border-percent、calc-leaf-content-box／partial-basis |
| `align_items` | 交叉／块轴项目默认对齐，含基线与安全值 | 已实现：Style.align_items；默认项目对齐、安全回退、首基线及自身方向 | 通用布局 | 已建：flex-baseline、grid-baseline、flex-wrap-reverse-baseline、grid-first-row-baseline、flex-scroll-baseline、grid-scroll-baseline、grid-align-safe／unsafe-overflow |
| `align_self` | 单项目交叉／块轴覆盖值 | 已实现：Style.align_self；项目覆盖值、基线分组、auto margin 与安全回退 | 通用布局 | 已建：flex-self-ltr-rtl、flex-self-rtl-ltr、flex-wrap-reverse-physical-row／column、flex-wrap-reverse-baseline、test_item_alignment_matrix.py、test_baseline_matrix.py |
| `justify_items` | Grid 子项内联轴默认对齐 | 已实现：Style.justify_items；Grid 内联对齐、方向与安全回退 | 通用布局 | 已建：grid-justify-items-end、grid-justify-safe／unsafe-overflow、test_mixed_nested.py |
| `justify_self` | Grid 单项目内联轴覆盖值 | 已实现：Style.justify_self；Grid 项目覆盖值、默认拉伸及定位区域 | 通用布局 | 已建：grid-self-ltr-rtl、grid-self-rtl-ltr、test_item_alignment_matrix.py |
| `align_content` | 行／轨道整体交叉轴分布，含 space-* 与安全值 | 已实现：Style.align_content；Flex 行／Grid 行轨道分布、单行 wrap、安全值及反向 | 通用布局 | 已建：grid-content-alignment、648 组 Flex 方向／换行／对齐组合差分、block-align-matrix、test_item_alignment_matrix.py、test_content_alignment_matrix.py |
| `justify_content` | 主／内联轴整体分布，含 space-* 与安全值 | 已实现：Style.justify_content；Flex 主轴／Grid 列轨道分布；Taffy stretch 差异单列 | 通用布局 | 已建：grid-content-alignment、test_content_alignment_matrix.py |
| `gap` | 双轴 Length／Percent／Calc | 已实现：Style.gap；数值／百分比／calc、未定轴与循环重算；小系数 Flex 差异单列 | 通用布局 | 已建：flex-row-grow、flex-auto-percent-gap、grid-auto-percent-gap、flex-auto-cross-percent-gap、grid-auto-row-percent-gap、calc-flex-gap-basis／auto-gap、calc-grid-gap-item／auto-gap、repeat-gap-fixed／max／min-fill |
| `text_align` | Auto／LegacyLeft／LegacyRight／LegacyCenter，Block 的传统内联对齐 | 已实现：Style.text_align；Block 三种 legacy 盒对齐及方向；不负责文本塑形 | Block | 已建：block-legacy-text-align、block-legacy-rtl-left／right／center |
| `flex_direction` | Row／RowReverse／Column／ColumnReverse | 已实现：Style.flex_direction；四向轴映射、反向、定位和基线 | Flexbox | 已建：flex-row-grow、flex-column-reverse-center／wrap／rtl-wrap-reverse／auto-margin |
| `flex_wrap` | NoWrap／Wrap／WrapReverse／Balance／BalanceReverse | 已实现：Style.flex_wrap；五种换行模式、分行、平衡及反向交叉轴 | Flexbox | 已建：flex-balance-lines、flex-auto-max-width-wrap、flex-auto-max-width-wrap-reverse、test_flex_matrix.py、test_flex_bounds.py、test_mixed_nested.py |
| `flex_line_count` | balance 等多行算法的最少行数 | 已实现：Style.flex_line_count；balance 最小行数、内容变更和边界 | Flexbox | 已建：flex-balance-lines、flex-auto-max-width-balance、test_flex_matrix.py |
| `flex_basis` | Dimension 的 flex 初始主轴尺寸 | 已实现：Style.flex_basis；固有查询、装饰、比例传递、假设尺寸和冻结基准 | Flexbox | 已建：flex-intrinsic-basis、calc-flex-gap-basis、test_flex_factors.py、test_flex_bounds.py、test_mixed_nested.py |
| `flex_grow` | 非负弹性增长系数 | 已实现：Style.flex_grow；冻结迭代、min/max 和小系数；版本 gap 差异单列 | Flexbox | 已建：flex-row-grow、flex-small-grow-factors、flex-grow-max-freeze、test_flex_factors.py |
| `flex_shrink` | 非负收缩系数，按基准尺寸缩放 | 已实现：Style.flex_shrink；按基准加权、冻结迭代及自动最小值 | Flexbox | 已建：flex-small-shrink-factor、flex-shrink-min-freeze、test_flex_factors.py、test_mixed_nested.py、wpt-flex-scaled-shrink |
| `grid_template_rows` | 固有／minmax／fit-content／fr／repeat 轨道 | 已实现：Style.grid.template_rows；完整轨道阶段、跨格贡献、重复和循环轴 | Grid | 已建：css-grid-tracks-placement、grid-auto-fill／fit、grid-percent-indefinite-rows、repeat-gap-row-max／min、test_grid_matrix.py、test_grid_span_matrix.py、test_grid_repeat_matrix.py、test_grid_triple_span.py |
| `grid_template_columns` | 同上，含 auto-fill／auto-fit 与具名线 | 已实现：Style.grid.template_columns；fr／auto／固有／minmax／fit-content／repeat；calc 自动重复差异单列 | Grid | 已建：css-grid-tracks-placement、named-area-placement、grid-span-order-short-first／long-first、grid-equal-intrinsic-growth、grid-capped-intrinsic-growth、grid-scroll-automatic-minimum、grid-spanning-flex-minimum、grid-fr-small-min-query／max-query、grid-percent-indefinite-tracks、grid-fixed-max-pair、repeat-gap-fixed-percent-track、test_grid_matrix.py、test_grid_span_matrix.py、test_grid_repeat_matrix.py、test_grid_triple_span.py |
| `grid_auto_rows` | 隐式行轨道尺寸序列 | 已实现：Style.grid.auto_rows；前后隐式序列、负线、双向边界裁切和 calc | Grid | 已建：grid-implicit-positive-rows、grid-implicit-negative-rows、grid-implicit-mixed-rows |
| `grid_auto_columns` | 隐式列轨道尺寸序列 | 已实现：Style.grid.auto_columns；前后隐式序列、负线、双向边界裁切和 calc | Grid | 已建：grid-implicit-negative-lines、grid-negative-implicit-sequence |
| `grid_auto_flow` | Row／Column／RowDense／ColumnDense | 已实现：Style.grid.auto_flow；四种流、Dense、游标和源顺序 | Grid | 已建：grid-row-dense-holes、placement-row／column-secondary-first、placement-row／column-dense-secondary、placement-negative-column／row-start、placement-row／column-primary-backtrack、test_grid_line_matrix.py |
| `grid_template_areas` | 命名矩形区域 | 已实现：Style.grid.template_areas；矩形区域、生成线名和端点解析 | Grid | 已建：named-area-placement、test_grid_area_matrix.py |
| `grid_template_column_names` | 显式列网格线名 | 已实现：Style.grid.column_line_names 与重复边界线名；顺序去重及完整输出诊断 | Grid | 已建：css-grid-tracks-placement、grid-named-line-occurrences、named-line-missing-positive／negative／forward-span／backward-span、named-line-existing-negative-third／positive-third／backward-span-first、test_grid_line_matrix.py、test_grid_area_matrix.py |
| `grid_template_row_names` | 显式行网格线名 | 已实现：Style.grid.row_line_names 与重复边界线名；顺序去重及完整输出诊断 | Grid | 已建：named-area-placement、test_grid_line_matrix.py、test_grid_area_matrix.py |
| `grid_row` | 起止线、负索引、span、名称 | 已实现：Style.grid.row；正负端点、具名线、span、反向／相同线与绝对区域 | Grid | 已建：named-area-placement、grid-absolute-area、grid-line-reverse-row、test_grid_line_matrix.py |
| `grid_column` | 起止线、负索引、span、名称 | 已实现：Style.grid.column；正负端点、具名线、span、反向／相同线；负锚点具名 span 版本差异单列 | Grid | 已建：grid-implicit-negative-lines、grid-named-line-occurrences、grid-line-reverse-column／negative／named、grid-line-equal-column、grid-line-both-spans、grid-line-named-span-start-auto／auto-end、test_grid_line_matrix.py |
| `dummy` | Rust 泛型编译占位字段 | 内部字段 | 内部实现 | 无需用例 |

## 验证与边界

固定用例、属性组合、错误路径、混合树和全部 6020 个锁定上游输入参与参考验证。
测试覆盖逻辑几何及实际取整输出，执行方法见 [测试指南](testing.md)。
Taffy 兼容结果与浏览器规范结果存在已知差异，具体输入与断言见 [规范对照](css-conformance.md)。

| 范围 | 当前约定 |
| --- | --- |
| 样式输入 | C++ 类型化样式；没有 CSS 字符串解析、选择器或级联 |
| 文本与绘制 | 宿主提供内容测量与基线，负责文字塑形、绘制和控件交互 |
| 浏览器排版 | 不提供内联格式化、Table 算法、垂直 writing-mode 或分页／分栏碎片化 |
| 滚动 | 报告滚动范围并预留滚动条空间；滚动交互、裁剪由宿主处理 |

内置布局树和借用宿主树的接入方式见 [README](../README.md#选择接入方式)，两者使用相同布局语义。
