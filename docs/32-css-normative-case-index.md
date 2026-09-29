# CSS 规范、WPT 与 Taffy 0.14 用例索引

本索引把三种预期分开记录：锁定版本的 Taffy 0.14.0、指定版本的
CSS 规范，以及可执行的浏览器或 WPT 用例。Taffy 差分相等只证明
Axisly 的兼容路径在该输入上的结果相等；规范结论另行验收。
已确认的差异保存在可重放用例中，不以修改预期的方式消除。

| ID | 规则与来源 | 本库重放输入 | 已观察结果 | 结论 |
| --- | --- | --- | --- | --- |
| FLEX-ALIGN-01 | [CSS Box Alignment 3 §4.3（2026-01-30）](https://www.w3.org/TR/2026/WD-css-align-3-20260130/#distribution-values)：`stretch` 在 Flex 中无法拉伸固定尺寸项目时回退 `flex-start` | [`flex_reverse_justify_stretch.html`](../tests/spec/flex_reverse_justify_stretch.html)；Taffy 用例 `content-alignment-11-5-0-0` 来自 [`test_content_alignment_matrix.py`](../tests/reference/test_content_alignment_matrix.py) | 四个 40px 项目、90px 容器、4px gap、`row-reverse wrap`、LTR：Taffy 两行的项目左边均为 `[44, 0]`；Chrome 153.0.8010.53 均为 `[50, 6]`，本地 HTML 报 `PASS` | Axisly 保留 Taffy 0.14 结果；此项 Taffy 差分不得计作 CSS 规范通过 |
| FLEX-LINE-01 | [CSS Flexbox 1 §6 与 §9.4（2025-10-14）](https://www.w3.org/TR/2025/CRD-css-flexbox-1-20251014/#algo-cross-line)：允许换行的容器即使只有一行，也先按项目计算行交叉尺寸，再做 `align-content` | [`test_content_alignment_matrix.py`](../tests/reference/test_content_alignment_matrix.py) 中 `Wrap`／`WrapReverse` 的单行与多行输入；[`test_item_alignment_matrix.py`](../tests/reference/test_item_alignment_matrix.py) 中反向换行项目对齐 | Axisly 与 Taffy 的锁定输入差分通过；单行 `NoWrap` 与 `Wrap` 的处理已分开 | 规范文字与兼容行为一致；精确 WPT 用例仍待移植 |
| FLEX-GROW-01 | [CSS Flexbox 1 §9.7 第 5.2 步](https://www.w3.org/TR/2025/CRD-css-flexbox-1-20251014/#resolve-flexible-lengths)：系数和小于 1 时使用按和缩放的剩余空间 | [`test_flex_factors.py`](../tests/reference/test_flex_factors.py) 中小系数及 gap 的组合 | Taffy 0.14 在缩放后再减一次主轴 gap；Axisly 的版本兼容路径保留该结果 | 已裁决的 Taffy 与规范差异；规范预期仍需独立数值测试 |
| GRID-LINE-01 | [CSS Grid 1 §8.3 负线与具名跨度](https://drafts.csswg.org/css-grid-1/#grid-placement) | [`grid_named_span_negative_anchor.html`](../tests/spec/grid_named_span_negative_anchor.html) 与 `grid-line-matrix-10-3-0-0-0-0-0-0` | 指定具名线及 `span 3 x / -1`：Chrome 项目宽 66px，Taffy 宽 74px | Axisly 保留 Taffy 0.14 结果；规范差异单列 |

## WPT 对接队列

下列 WPT 原始测试已核对测试目标，但尚未转成 Axisly 的树形输入。
它们不计入本库已通过的规范用例，也不替代上表的精确重放。

| WPT 路径 | 原测试目标 | 对应能力与下一步 |
| --- | --- | --- |
| [`css/css-flexbox/layout-algorithm_algo-cross-line-001.html`](https://github.com/web-platform-tests/wpt/blob/master/css/css-flexbox/layout-algorithm_algo-cross-line-001.html) | `overflow: scroll` 时 Flex 项目的交叉尺寸 | 将滚动盒的交叉尺寸与行尺寸同时转为树形断言，覆盖 `overflow`／`align_content` |
| [`css/css-flexbox/flex-002.htm`](https://github.com/web-platform-tests/wpt/blob/master/css/css-flexbox/flex-002.htm) | 负剩余空间中按 Flex basis 缩放 `flex-shrink` | 转成有明确尺寸的差分输入，并独立计算规范预期；覆盖 `flex_shrink` |

新增规范或 WPT 用例时，索引须同时写明输入、规范版本或 WPT 路径、
断言来源、Axisly/Taffy 结果，以及浏览器结果若用浏览器裁决。
尚未覆盖的尺寸循环、复杂 Grid 轨道和深层 Block 折叠仍按
[能力矩阵](30-taffy-014-capability-matrix.md) 的“部分”状态追踪。
