# CSS 规范、WPT 与 Taffy 0.14 用例索引

本索引把三种预期分开记录：锁定版本的 Taffy 0.14.0、指定版本的
CSS 规范，以及可执行的浏览器或 WPT 用例。Taffy 差分相等只证明
Axisly 的兼容路径在该输入上的结果相等；规范结论另行验收。
已确认的差异保存在可重放用例中，不以修改预期的方式消除。

| ID | 规则与来源 | 本库重放输入 | 已观察结果 | 结论 |
| --- | --- | --- | --- | --- |
| FLEX-ALIGN-01 | [CSS Box Alignment 3 §4.3（2026-01-30）](https://www.w3.org/TR/2026/WD-css-align-3-20260130/#distribution-values)：`stretch` 在 Flex 中无法拉伸固定尺寸项目时回退 `flex-start` | [`flex_reverse_justify_stretch.html`](../tests/spec/flex_reverse_justify_stretch.html)；Taffy 用例 `content-alignment-11-5-0-0` 来自 [`test_content_alignment_matrix.py`](../tests/reference/test_content_alignment_matrix.py) | 四个 40px 项目、90px 容器、4px gap、`row-reverse wrap`、LTR：Taffy 两行的项目左边均为 `[44, 0]`；Chrome 153.0.8010.53 均为 `[50, 6]`，本地 HTML 报 `PASS` | Axisly 保留 Taffy 0.14 结果；此项 Taffy 差分不得计作 CSS 规范通过 |
| FLEX-LINE-01 | [CSS Flexbox 1 §6 与 §9.4（2025-10-14）](https://www.w3.org/TR/2025/CRD-css-flexbox-1-20251014/#algo-cross-line)：允许换行的容器即使只有一行，也先按项目计算行交叉尺寸，再做 `align-content` | [`test_content_alignment_matrix.py`](../tests/reference/test_content_alignment_matrix.py) 中 `Wrap`／`WrapReverse` 的单行与多行输入；[`test_item_alignment_matrix.py`](../tests/reference/test_item_alignment_matrix.py) 中反向换行项目对齐 | Axisly 与 Taffy 的锁定输入差分通过；单行 `NoWrap` 与 `Wrap` 的处理已分开 | 规范文字与兼容行为一致；精确 WPT 用例仍待移植 |
| FLEX-CROSS-02 | [WPT `layout-algorithm_algo-cross-line-001.html`](https://github.com/web-platform-tests/wpt/blob/master/css/css-flexbox/layout-algorithm_algo-cross-line-001.html)：两个 `overflow: scroll` 的 Flex 项保持 200px 高度 | [`wpt_scroll_cross_size`](../tests/css/css_flex_tests.cpp) 独立断言；[`wpt-flex-cross-scroll`](../tests/reference/fixtures/tree.json) 由 Taffy 0.14 对照；[浏览器重放](../tests/spec/wpt_flex_scroll_cross_size.html) | 200px 宽容器为 200×200；两个项目分别为 `[0,0,100,200]`、`[100,0,100,200]`，Axisly、Taffy 及 Chrome 153.0.8010.53 相同，重放报 `PASS` | 已移植几何断言；未移植浏览器滚动条绘制和完整 reftest |
| FLEX-SHRINK-02 | [WPT `flex-002.htm`](https://github.com/web-platform-tests/wpt/blob/master/css/css-flexbox/flex-002.htm)：负空间按 `flex-shrink × flex-basis` 分配 | [`wpt_scaled_flex_shrink`](../tests/css/css_flex_tests.cpp) 独立断言；[`wpt-flex-scaled-shrink`](../tests/reference/fixtures/tree.json) 由 Taffy 0.14 对照；[浏览器重放](../tests/spec/wpt_flex_scaled_shrink.html) | 基准宽 300、200，收缩系数 2、1，200px 缺额按 600:200 分配，得到两个 150px 项目；Axisly、Taffy 及 Chrome 153.0.8010.53 相同，重放报 `PASS` | 已移植数值预期；未运行浏览器原始 reftest |
| FLEX-GROW-01 | [CSS Flexbox 1 §9.7 第 5.2 步](https://www.w3.org/TR/2025/CRD-css-flexbox-1-20251014/#resolve-flexible-lengths)：系数和小于 1 时使用按和缩放的剩余空间 | [`test_flex_factors.py`](../tests/reference/test_flex_factors.py) 中小系数及 gap 的组合 | Taffy 0.14 在缩放后再减一次主轴 gap；Axisly 的版本兼容路径保留该结果 | 已裁决的 Taffy 与规范差异；规范预期仍需独立数值测试 |
| GRID-LINE-01 | [CSS Grid 1 §8.3 负线与具名跨度](https://drafts.csswg.org/css-grid-1/#grid-placement) | [`grid_named_span_negative_anchor.html`](../tests/spec/grid_named_span_negative_anchor.html) 与 `grid-line-matrix-10-3-0-0-0-0-0-0` | 指定具名线及 `span 3 x / -1`：Chrome 项目宽 66px，Taffy 宽 74px | Axisly 保留 Taffy 0.14 结果；规范差异单列 |

## WPT 移植界限与后续

### PIXEL-RTL-01：XML 的负半像素整数预期

全量 XML 中，三种算法和两种盒尺寸的 6 个 RTL 百分比外边距用例，
孙节点的未取整矩形均为 `[-0.5,0.5,50,10]`。Axisly 与 Taffy 相同，
[Chrome 重放](../tests/spec/rtl_percentage_half_pixel.html) 也给出该值。
XML 存储 `[0,1,50,10]`，而 Taffy 对负半像素舍入及累计边缘舍入
得到 `[-1,1,51,10]`（Block/Flex）或 `[-1,1,50,10]`（Grid 的累计
绝对位置不同）。这是整数取整预期的差异，不能据此声明 CSS 几何不符合。
CSS 2.2 [§10.3.3](https://www.w3.org/TR/CSS22/visudet.html#blockwidth)
的 RTL 过约束规则与该负左边位置一致；浏览器仍保留分数几何。
原始观察见 [normative-browser.json](reference-validation/taffy-upstream-2026-09-30/normative-browser.json)。

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
[原始结果](reference-validation/taffy-upstream-2026-09-30/block-padding-browser.json)
记录浏览器版本。兼容元数据差分不计作 CSS padding 规范通过。

上表两条 WPT 用例已转成 Axisly 独立数值断言及锁定版 Taffy 树形输入。
它们只覆盖原始 reftest 的布局几何部分。2026-09-30 的 Chrome 无头模式
分别执行本地重放并报告 `PASS`；平台滚动条绘制和原始 reftest 的
自动执行仍待验收。
纳入后的参考 Release／Debug 各通过 340/340，ASan 通过 339/339；
GeeyoouUI Release／Debug／MSVC ASan 各通过 296/296。

新增规范或 WPT 用例时，索引须同时写明输入、规范版本或 WPT 路径、
断言来源、Axisly/Taffy 结果，以及浏览器结果若用浏览器裁决。
尚未覆盖的尺寸循环、复杂 Grid 轨道和深层 Block 折叠仍按
[能力矩阵](30-taffy-014-capability-matrix.md) 的“部分”状态追踪。
