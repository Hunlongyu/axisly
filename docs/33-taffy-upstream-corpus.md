# Taffy 0.14.0 全量上游用例重放

## 固定输入与准入

参考库锁定为 `taffy = 0.14.0`，源码提交为
`77f385683c1d698c91a23a259f87fdddf26925fb`，来自发布包的
`.cargo_vcs_info.json`。开发工具下载该提交的
[官方源码归档](https://codeload.github.com/DioxusLabs/taffy/zip/77f385683c1d698c91a23a259f87fdddf26925fb)。
归档 SHA256 为
`baf36d0a3f896d901b81c241b332e2fc59f2f1b6ab05c574b8323b29e35d0baa`。

`tests/xml` 下全部 6020 个 XML 纳入重放。按相对 POSIX 路径排序后，
依次哈希 `路径 UTF-8 + NUL + 原始文件字节 + NUL`，结果为
`833448fad307ab882325a9d3cd3650a94a187ab817abc424f7331aff846eec54`。
数量或摘要变化会终止转换。每个用例保留原路径、文件摘要、取整开关、
XML 预期和完整输入树；未知属性、拓扑不一致、非法值不会被静默忽略。
巨大索引、重复轨道列表及有限 f32 极值同样纳入。

`upstream_fixture.py` 转换为独立树形参考协议，`css_case_codegen.py`
生成类型化 C++20 输入。超大重复列表以运行时展开的 typed runs 表达，
保留全部显式条目和原来的 repeat 类型。输入转换与 CSS 生产库分离。
它没有为生产库增加 XML、JSON、CSS 解析或 Rust 依赖。

文本模型来自该提交的 `tests/common/src/lib.rs`：Ahem 字形为 10×10，
U+200B 为换行机会，字节段长度决定最小／最大内容宽度，确定可用空间
决定换行。`vertical-lr` 仅转换宿主测量轴；没有宣称支持 CSS 书写模式。
许可保存在 `tests/reference/TAFFY_UPSTREAM_LICENSE.txt`。

## 重放与断言

仅启用参考测试的构建配置下载上游归档。构建、运行全部用例：

```powershell
cmake --build build/reference-release --target axisly_upstream_audit
```

可通过 `test_upstream_corpus.py --group leaf` 等命令单独重放布局组。
原生和参考工具的进程错误、超时、单项布局错误都会记录为失败；
整批进程失败时二分重放，定位到具体输入。任何失败都会让命令返回非零。
当前全量审计保留差异报告，尚未加入要求全部通过的 CTest 门禁。

与 Taffy 比较每个节点的未取整矩形、margin、padding、border、
scrollbar、可滚动溢出，以及 Grid 轨道坐标、显式／隐式轨道数和项目线范围。
使用现有参考套件的 0.01px 绝对容限，没有为上游审计放宽。

另比较 XML 的 x/y/width/height；按上游的 f32 累积位置和边缘取整规则
恢复取整结果，容限为上游 XML 测试使用的 0.1px。
**这一层目前只比较几何**，尚未断言 XML 的 scroll_width／scroll_height、
resolved-rows／resolved-columns。这些预期继续保留在转换结果中。
上游 XML 预期与 Taffy 本身的差异单独记录，不能作为 Axisly 布局通过的理由。

## 首次全量结果

以生产代码提交 `1ab29fe` 为基线，2026-09-30 的 Release 重放：

| 上游组 | 总数 | Taffy 对照通过 | 布局差异 | 原生错误 |
| --- | ---: | ---: | ---: | ---: |
| Block | 940 | 824 | 116 | 0 |
| Block/Flex | 44 | 44 | 0 | 0 |
| Block/Grid | 56 | 56 | 0 | 0 |
| containment | 32 | 32 | 0 | 0 |
| Flex | 2648 | 2504 | 144 | 0 |
| Float | 88 | 54 | 34 | 0 |
| Grid | 2132 | 1906 | 138 | 88 |
| Grid/Flex | 24 | 24 | 0 | 0 |
| 测量叶子 | 56 | 52 | 4 | 0 |
| 合计 | 6020 | 5496 | 436 | 88 |

全部 6020 个输入均被参考工具接受。Taffy 与 XML 的几何对照有 6 项差异，
集中于 RTL 百分比外边距的半像素基线用例，仍需分析并补充规范判断。
原始报告：

- [首次完整对照](reference-validation/taffy-upstream-2026-09-30/baseline.json)
- [Taffy 与 XML 几何对照](reference-validation/taffy-upstream-2026-09-30/oracle-xml-geometry.json)

待修复输入包括测量约束传递、浮动与折叠边距、定位区域、超大网格
截断边界及内在轨道贡献。它们不计入通过集合；能力矩阵继续保留
相应的“部分”状态。本轮没有完成 P0–P5 的全部验收。

开发适配器的 12 项准入／生成检查通过；扩展 Ahem 参考上下文后，
既有 Release CTest 与适配器检查合计 348/348 通过。全量 6020 审计
保持独立非零结果，不计入这个通过数。

## 叶节点测量约束阶段重构后

叶节点计算从通用调度器移到 `leaf_layout.cpp`。测量请求先选择父级已知
尺寸或可用空间，扣除外边距，再按 min/max 钳制外盒预算，最后扣除
padding、border 和滚动条内容预留；指定内容尺寸使用同样的约束顺序。
四组独立宿主断言固定指定／自动尺寸、min 大于 max 及滚动条的组合。

全量重放通过 **5588/6020**，新增通过 92 项，没有从通过变为失败的输入。
剩余 344 项布局差异和 88 项原生 Grid 边界错误继续保留为失败。
原始报告见 [叶节点约束阶段](reference-validation/taffy-upstream-2026-09-30/leaf-measurement-space.json)。
Release／Debug 各通过 349/349，MSVC ASan 通过 348/348；宿主三种
配置各通过 297/297，安装消费 1/1。完整上游审计仍未进入通过门禁。

## 超大 Grid 边界与模块分层

模板展开、具名线／放置、轨道求解分为独立模块。显式轨道按源顺序
截断，repeat 计数运算先限制计数预算；自动重复可在轨道列表中途截断。
数字线、跨度、初始隐式范围和最终占用范围共用原点两侧各 10000 条
轨道的边界，每轴支持最多 20000 条轨道。完全越界的项目保留一条轨道。
该裁切过程对应 [CSS Grid §5.4](https://www.w3.org/TR/2025/CRD-css-grid-1-20250326/#overlarge-grids)，
具体限值固定为 Taffy 0.14 的选择。

全量重放通过 **5676/6020**，88 个原生边界错误全部通过，没有新增失败。
剩余 344 项均为布局差异。原始报告见
[Grid 边界对照](reference-validation/taffy-upstream-2026-09-30/grid-limits.json)。
Release／Debug 各通过 350/350；独立超大 Grid 断言在 MSVC ASan 下
通过 1/1，覆盖源顺序、正方向裁切、双向 20000 条轨道和部分自动重复。
完整 MSVC ASan 门禁随后通过 349/349（同一源码阶段与二进制，未改
超时或断言）。宿主与性能在后续组合阶段复核。

## 绝对定位尺寸和 Grid 区域分层

`positioned_item_layout.cpp` 将项目外盒解析、关键字查询、inset 填充、
比例传递、min/max 钳制、未定轴测量和最终子树布局分成明确步骤。
Block／Flex 使用内容尺寸查询，Grid 保留固有尺寸查询；Flex 子树的
包含块与样式／inset 的解析区域分别传入。最终两轴尺寸传给子树，
内容的自然溢出独立保留。定位包含块扣除滚动条，Block 的单个自动
外边距可以吸收负空间。

`grid_positioned_area.cpp` 独立解析两侧网格线；未指定或不在已有网格
内的端点使用相应的 padding-box 边界，绝对定位不创建隐式轨道。
间隙使用相邻轨道的真实起止边缘，折叠 auto-fit 轨道保留物理 RTL 位置。
三组独立数值断言覆盖三种父布局、最终测量约束、自然溢出、滚动条、
负自动边距、单端点网格区域、无效端点及折叠轨道。

全量上游重放通过 **5806/6020**，较 Grid 边界阶段新增通过 130 项，
没有新增失败或原生错误；余下 214 项布局差异继续修复。
[本阶段完整报告](reference-validation/taffy-upstream-2026-09-30/positioned-grid-area.json)
保留全部失败及 XML 对照。Release／Debug 门禁各通过 353/353。
定位、Grid 区域、滚动条及树查询的 MSVC ASan 检查通过 8/8；
该阶段使用定向 ASan 检查，未将上一阶段的完整通过数计为本阶段结果。

## 滚动条小外盒边界

padding／border 与滚动条内容预留分别建模。Flex 基础尺寸、收缩权重和
交叉轴下限只使用 padding／border；布局和测量仍扣除滚动条内容预留。
Block、Flex、Grid 和绝对定位分别保留有符号物理空间用于 RTL 及对齐，
传给宿主的测量空间保持非负。独立断言覆盖三种父布局、双向书写、
正常流与绝对定位、小外盒居中以及小于滚动条的伸缩项目。

全量对照 **5830/6020**，新增通过 24 项，没有新增失败，余下 190 项
布局差异。[原始报告](reference-validation/taffy-upstream-2026-09-30/gutter-boundaries.json)
保留全部差异。Release 通过 354/354；本阶段的 Debug／ASan 和宿主
将在后续组合阶段重新构建复核。

## Flex 项目查询阶段

项目样式、内容基础尺寸、自动最小尺寸和比例传递移至
`flex_item_sizing.cpp`。内容查询只保留交叉轴包含块，已计算但不确定的
主轴不解析百分比尺寸；百分比盒边保留真实包含块宽度。基础尺寸、
关键字测量预算和最终交叉轴拉伸分别求解，自动最小尺寸受比例传递的
最大尺寸限制。均衡换行按请求行数与间隙分配交叉测量预算，自动交叉
边距不参与基线共享，零收缩权重保留其内在基础贡献。

三组独立数值测试覆盖百分比 min、自然溢出与比例上限、stretch、
深层百分比确定性、基线自动边距、均衡测量以及负边距。
全量对照 **5890/6020**，较滚动条阶段新增通过 60 项，无新增失败。
**Flex 2648/2648** 与测量叶节点 56/56 均通过；其余 130 项集中于
Block、Float、Grid。[完整报告](reference-validation/taffy-upstream-2026-09-30/flex-item-stages.json)
保留所有差异。Release 357/357 通过，Debug／ASan 在本阶段源码上复核。
