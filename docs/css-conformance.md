# CSS 规范与版本兼容差异

本索引把三种预期分开记录：锁定版本的 Taffy 0.14.0、指定版本的
CSS 规范，以及可执行的浏览器或 WPT 用例。Taffy 差分相等只证明
Axisly 的兼容路径在该输入上的结果相等；规范结论另行验收。
已确认的差异保存在可重放用例中，不以修改预期的方式消除。
下列浏览器观察属于指定版本的开发期记录；重放入口保留在 `tests/spec/`。

| ID | 规则与来源 | 本库重放输入 | 已观察结果 | 结论 |
| --- | --- | --- | --- | --- |
| FLEX-ALIGN-01 | [CSS Box Alignment 3 §4.3（2026-01-30）](https://www.w3.org/TR/2026/WD-css-align-3-20260130/#distribution-values)：`stretch` 在 Flex 中无法拉伸固定尺寸项目时回退 `flex-start` | [`flex_reverse_justify_stretch.html`](../tests/spec/flex_reverse_justify_stretch.html)；Taffy 用例 `content-alignment-11-5-0-0` 来自 [`test_content_alignment_matrix.py`](../tests/reference/test_content_alignment_matrix.py) | 四个 40px 项目、90px 容器、4px gap、`row-reverse wrap`、LTR：Taffy 两行的项目左边均为 `[44, 0]`；Chrome 153.0.8010.53 均为 `[50, 6]`，本地 HTML 报 `PASS` | Axisly 保留 Taffy 0.14 结果；此项 Taffy 差分不得计作 CSS 规范通过 |
| FLEX-LINE-01 | [CSS Flexbox 1 §6 与 §9.4（2025-10-14）](https://www.w3.org/TR/2025/CRD-css-flexbox-1-20251014/#algo-cross-line)；[WPT align-content-wrap-001.html](https://github.com/web-platform-tests/wpt/blob/master/css/css-flexbox/align-content-wrap-001.html) 的水平布局 | [`wpt_single_line_wrap`](../tests/css/css_flex_tests.cpp)；[浏览器重放](../tests/spec/wpt_flex_single_line_wrap.html)；单行／多行／反向换行矩阵 | 70px 高容器和 20px 高项目：七种对齐偏移为 `[0,50,25,0,25,25,0]`，独立断言和 Chrome 重放通过；Axisly/Taffy 单行矩阵相同 | 已移植水平几何断言；垂直 writing-mode 不在本布局引擎范围 |
| FLEX-CROSS-02 | [WPT `layout-algorithm_algo-cross-line-001.html`](https://github.com/web-platform-tests/wpt/blob/master/css/css-flexbox/layout-algorithm_algo-cross-line-001.html)：两个 `overflow: scroll` 的 Flex 项保持 200px 高度 | [`wpt_scroll_cross_size`](../tests/css/css_flex_tests.cpp) 独立断言；[`wpt-flex-cross-scroll`](../tests/reference/fixtures/tree.json) 由 Taffy 0.14 对照；[浏览器重放](../tests/spec/wpt_flex_scroll_cross_size.html) | 200px 宽容器为 200×200；两个项目分别为 `[0,0,100,200]`、`[100,0,100,200]`，Axisly、Taffy 及 Chrome 153.0.8010.53 相同，重放报 `PASS` | 已移植几何断言；未移植浏览器滚动条绘制和完整 reftest |
| FLEX-SHRINK-02 | [WPT `flex-002.htm`](https://github.com/web-platform-tests/wpt/blob/master/css/css-flexbox/flex-002.htm)：负空间按 `flex-shrink × flex-basis` 分配 | [`wpt_scaled_flex_shrink`](../tests/css/css_flex_tests.cpp) 独立断言；[`wpt-flex-scaled-shrink`](../tests/reference/fixtures/tree.json) 由 Taffy 0.14 对照；[浏览器重放](../tests/spec/wpt_flex_scaled_shrink.html) | 基准宽 300、200，收缩系数 2、1，200px 缺额按 600:200 分配，得到两个 150px 项目；Axisly、Taffy 及 Chrome 153.0.8010.53 相同，重放报 `PASS` | 已移植数值预期；未运行浏览器原始 reftest |
| FLEX-GROW-01 | [CSS Flexbox 1 §9.7 第 5.2 步](https://www.w3.org/TR/2025/CRD-css-flexbox-1-20251014/#resolve-flexible-lengths)：系数和小于 1 时使用按和缩放的剩余空间 | [`small_factors_gap`](../tests/css/css_flex_tests.cpp)；[`test_flex_factors.py`](../tests/reference/test_flex_factors.py)；[浏览器重放](../tests/spec/flex_small_factors_gap.html) | 两项目、系数各 0.25、gap 10px：200px 容器／basis 40px 的增长宽为 Taffy 62.5、规范/Chrome 67.5；100px 容器／basis 80px 的收缩宽为 Taffy 57.5、规范/Chrome 62.5 | 两套独立数值断言均通过；Axisly 保留锁定 Taffy 的重复扣 gap 行为 |
| GRID-LINE-01 | [CSS Grid 1 §8.3 负线与具名跨度](https://drafts.csswg.org/css-grid-1/#grid-placement) | [`grid_named_span_negative_anchor.html`](../tests/spec/grid_named_span_negative_anchor.html) 与 `grid-line-matrix-10-3-0-0-0-0-0-0` | 指定具名线及 `span 3 x / -1`：Chrome 项目宽 66px，Taffy 宽 74px | Axisly 保留 Taffy 0.14 结果；规范差异单列 |

## 其他兼容差异

### GRID-CALC-01：自动重复的固定尺寸准入

[CSS Grid 1 §7.2.3.1](https://www.w3.org/TR/css-grid-1/#repeat-syntax) 的固定尺寸
允许长度与百分比计算。[独立浏览器输入](../tests/spec/grid_calc_auto_repeat.html)
在 200px 容器中使用 `calc(20px + 10%)` 与 5px gap：`auto-fill` 产生
四条 40px 列，`auto-fit` 保留四条显式轨道并折叠两个空列，计数重复产生
两条 40px 列。

Taffy 0.14 的 `TrackSizingFunction::has_fixed_component` 只识别直接长度和
百分比，纯 calc 边界的自动重复会丢弃整个显式模板。Axisly 保留此版本行为，
仍布局全部子项及隐式轨道。960 组 calc 轨道矩阵逐节点比较两者的几何、
盒边、溢出、轨道和放置；包括计数／自动重复、双轴、隐式负线与正线。
一边直接长度、另一边 calc 的 minmax 可以参与自动重复，也纳入同一矩阵。
这是版本兼容差异，不能计为自动重复 calc 的 CSS 规范通过。
Chrome 153.0.8010.53 的开发期重放中，三种模板均为 PASS。

### PIXEL-RTL-01：XML 的负半像素整数预期

全量 XML 中，三种算法和两种盒尺寸的 6 个 RTL 百分比外边距用例，
孙节点的未取整矩形均为 `[-0.5,0.5,50,10]`。Axisly 与 Taffy 相同，
[Chrome 重放](../tests/spec/rtl_percentage_half_pixel.html) 也给出该值。
XML 存储 `[0,1,50,10]`，而 Taffy 对负半像素舍入及累计边缘舍入
得到 `[-1,1,51,10]`（Block/Flex）或 `[-1,1,50,10]`（Grid 的累计
绝对位置不同）。这是整数取整预期的差异，不能据此声明 CSS 几何不符合。
CSS 2.2 [§10.3.3](https://www.w3.org/TR/CSS22/visudet.html#blockwidth)
的 RTL 过约束规则与该负左边位置一致；浏览器仍保留分数几何。

### BLOCK-PADDING-01：百分比 padding 的报告阶段

[CSS 2.2 §8.4](https://www.w3.org/TR/CSS22/box.html#padding-properties)
规定四边百分比 padding 均按包含块宽度解析。独立输入为 40px 宽的
Block 根、100px 宽且四边 padding 10% 的子块，以及 10px 高的孙块。
Axisly 与 Taffy 的流几何均为子块高度 18px、孙块 `[4,4,92,10]`。
Taffy 0.14 的已发布 padding 元数据为 `[4,0,4,0]`：上下边在父块的
早期项目列表中按未定高度解析。Axisly 保留此版本兼容输出。

[`percentage_item_stages`](../tests/css/css_block_flow_tests.cpp) 分别断言
流几何及兼容元数据；[浏览器重放](../tests/spec/block_percentage_padding_metadata.html)
按规范断言 computed padding 为 `[4,4,4,4]`，Chrome 重放通过。
兼容元数据差分不计作 CSS padding 规范通过。

上表三条 WPT 水平几何用例已移植为独立数值断言、锁定版 Taffy 对照和
Chrome 重放。浏览器原始 reftest 的绘制、滚动条皮肤和垂直 writing-mode
不属于本布局库验收对象，不能从几何相等推断它们通过。

新增规范或 WPT 用例时，索引须同时写明输入、规范版本或 WPT 路径、
断言来源、Axisly/Taffy 结果，以及用于裁决的浏览器结果。
布局字段、组合及全量上游的当前验收以[能力矩阵](css-compatibility.md)
为准，已确认差异保留版本兼容行为。
