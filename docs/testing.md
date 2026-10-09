# 测试与参考验证

日常构建运行 CTest 契约测试，覆盖样式、布局、测量、缓存失效、错误恢复、内存、并行和取整。
Release 下保留测试断言。构建入口见 [构建指南](building.md)。

## 核心测试

```text
cmake --preset release
cmake --build --preset release
ctest --preset release
```

Debug 使用 `debug` 预设。Windows 可用 `./scripts/build-msvc.ps1 -Preset release`
一次完成工具链初始化、构建和测试。

## Taffy 差分测试

参考配置另需 Python 3.10+ 和 Rust/Cargo。首次构建下载锁定的参考依赖，生产库不链接 Rust 或 Taffy。

```text
cmake --preset reference-release
cmake --build --preset reference-release
ctest --preset reference-release
```

Windows 可用 `./scripts/build-msvc.ps1 -Preset reference-release`；Debug 使用 `reference-debug`。

参考测试统一验证 CSS 布局语义：

| 对象 | 驱动与输入 | 比较范围 |
| --- | --- | --- |
| CSS `LayoutTree` | `tests/reference/test_tree.py`、`test_css_tree.py` 和各属性矩阵 | 混合布局、尺寸阶段、基线、溢出、定位、Grid 诊断等 |
| 上游完整输入 | `tests/reference/test_upstream_corpus.py` 与 `test_upstream_rounding.py` | 锁定 Taffy 输入的逻辑几何与实际像素取整输出 |

`test_tree.py` 从实际编译的 Taffy `Style` 导出字段清单，并检查
[能力矩阵](css-compatibility.md)没有缺项或重复项。
规范差异与浏览器重放输入见 [规范对照](css-conformance.md)。

## 上游语料重放

Taffy 固定为 `0.14.0`，上游源码提交
`77f385683c1d698c91a23a259f87fdddf26925fb`。
归档及 SHA-256 固定在 [`upstream.cmake`](../tests/reference/upstream.cmake)，
转换器校验全部 6020 个 XML 的数量与内容摘要，保留来源路径、取整开关和 XML 预期。
未知属性、非法值与拓扑错误不会被静默忽略。

文本测量使用同一上游 Ahem 模型；XML 的 `vertical-lr` 只转换宿主测量轴，
不代表引擎实现了 CSS 垂直书写。上游许可见
[`TAFFY_UPSTREAM_LICENSE.txt`](../tests/reference/TAFFY_UPSTREAM_LICENSE.txt)。

```text
cmake --build build/reference-release --target axisly_upstream_audit
ctest --test-dir build/reference-release -R "^axisly.reference.upstream(_rounding)?$" --output-on-failure
```

每个节点比较矩形、盒边、滚动范围，以及 Grid 的轨道尺寸、坐标、线名和放置。
未取整对照使用参考套件的 0.01px 绝对容限；XML 预期使用上游的 0.1px 容限。
取整测试调用生产库的取整接口，不以比较器中的模拟结果代替。

结果写入对应构建目录的 `tests/reference/upstream-audit.json` 和
`upstream-rounding-audit.json`。进程错误、超时和布局差异均使测试失败；
批量进程失败时二分重放定位输入。Taffy 与 XML 本身的差异单列，输入仍参加 Taffy 对照。

参考工具的输入、生成器和源代码保留在 `tests/reference/`；生成结果留在 `build/`。
旧原生 Grid 的算法与专属契约测试已删除，因此测试总数减少；CSS 契约、
宿主树接入、属性矩阵及全部 6020 项上游输入继续保留。

语料通过表示该组输入和指定参照下的结果一致，不等同于完整 CSS 规范或完整浏览器 WPT 通过。

## Sanitizer

Windows x64：

```powershell
./scripts/build-sanitizers.ps1
./scripts/build-sanitizers.ps1 -Compiler MSVC
```

Linux GCC／Clang 可配置 `-DAXISLY_ENABLE_SANITIZERS=ON`，启用 ASan／UBSan。
Sanitizer 用于验证内存和未定义行为，其耗时不作为性能结果。

Linux 的线程检测使用独立构建目录和 `-DAXISLY_ENABLE_TSAN=ON`，不能与 ASan／UBSan 混用。
CI 对共享 `LayoutMemory` 和并行 `LayoutBatch` 运行 ThreadSanitizer，并配置 Linux ARM64、
macOS ARM64 的构建、测试与安装后消费者验证。工作流配置不代表这些平台已经在本机运行。

## 接口、异常与覆盖率

- `axisly.css.api.*` 检查树操作、上下文、测量策略、坐标、取整和结果生命周期。
- `axisly.css.mutation.allocation_failure` 逐次使分配失败，检查拓扑提交和缓存一致性；
  分离、删除、整棵子树标脏必须在禁止分配时仍可完成。
  MSVC Debug STL 在 `noexcept` 容器构造中分配调试代理，创建节点的全分配失败扫描在
  Release／RelWithDebInfo ASan 执行；Debug 保留正常创建契约及其余七项修改操作的故障注入。
- `axisly.css.source.stateful_*` 使用四个固定随机种子，每组连续修改 400 次，
  检查持久树与宿主树接入，对包括分离节点在内的每个根逐步对照重新建树的几何、Grid 诊断和拓扑。
- `axisly.css.grid.optimization_contract` 检查定向缓存失效、auto-repeat 双槽替换、
  嵌套工作区复用和测量异常后的恢复；启用 `AXISLY_ENABLE_GRID_METRICS` 时还验证重建次数。
- `axisly.css.grid.placement_index` 对四种自动放置方向执行 1,600 组固定种子差分，
  将行区间索引与线性扫描比较，覆盖负坐标、重叠、隐藏项和大跨度回退。
- `axisly.css.memory.contract` 同时检查 Flex、Grid 和浮动 Block 工作区的资源归还。

Clang 可在独立目录配置 `-DAXISLY_ENABLE_COVERAGE=ON`，然后运行：

```sh
python scripts/coverage.py --build build/coverage
```

脚本执行 CTest，使用全新的 LLVM profile 目录合并所有测试程序的数据，输出
`coverage/summary.json`、完整 `coverage.json` 和带分支计数的 HTML。
摘要只统计 `src/`、`include/`，不把测试代码和标准库计入库覆盖率。
`--minimum-line`、`--minimum-branch` 可设置门槛；缺少 profile 或库分支数据会直接失败。
CI 配置的初始门槛为行覆盖率 85%、分支覆盖率 70%。
LLVM 的诊断保存在 `diagnostics.log` 并列入摘要；Windows COFF 合并可能报告
内联定义的零哈希记录不匹配，解读覆盖率时应同时检查这些诊断。
Windows clang-cl x64 同样支持覆盖率，工具不在 PATH 时显式传入
`--ctest`、`--llvm-profdata`、`--llvm-cov` 的路径。

## 性能验证

核心基准使用 `benchmark-release`，三方对比使用 `comparison-release` 或 `comparison-clang`。
具体输入、计时边界、原始时间单位、几何检查和重放方法见
[性能基准指南](../benchmarks/comparison/README.md)。
基准在构建、测试和其他计时任务结束后串行运行，结果保存到 `build/` 下独立目录。
