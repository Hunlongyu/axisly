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
`833248fad307ab882325a9d3cd3650a94a187ab817abc424f7331aff846eec54`。
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
