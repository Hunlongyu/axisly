# 16 — M8 Taffy 共同子集与差异重放

版本 `0.0.9`。本阶段加入独立于生产库的参考验证工具，保留 M0～M7 的布局行为与公开 API。

## 边界与依赖

`AXISLY_BUILD_REFERENCE_TESTS` 默认 OFF，只有显式启用时才查找 Python、Cargo 和 rustc。它要求 `AXISLY_BUILD_TESTS=ON`，不支持跨架构运行。普通 Debug／Release 预设显式关闭该选项。

生产 `axisly::axisly` 的源文件、链接依赖、头文件和安装内容不引用 Taffy、Rust 或 Python。参考工具不安装、不导出为库目标，作为子目录接入时也不自动启用。

参考包固定为 [Taffy 0.14.0](https://docs.rs/taffy/0.14.0/taffy/struct.TaffyTree.html)，`Cargo.toml` 使用精确版本，`Cargo.lock` 锁定传递依赖。构建使用 `cargo build --release --locked`，只启用 std、taffy_tree、grid 特性。两种 C++ 构建配置均对照同一 Rust Release 配置；Cargo 首次需要访问配置的包仓库，后续可复用本机缓存。

Python 3.10+ 对照器仅使用标准库。Rust 使用安装好的工具链，本机验证版本为 1.98.0；不修改用户全局工具链或镜像设置。

## 组成与数据流

| 文件 | 职责 |
| --- | --- |
| `tests/reference/native_runner.cpp` | 读取用例，以公开 API 调用 axisly，输出 JSON 矩形 |
| `tests/reference/taffy/` | 独立 Rust 可执行程序，转换同一输入并调用 Taffy |
| `tests/reference/compare.py` | 输入校验、随机生成、进程调用、数值比较、保存与重放失败 |
| `tests/reference/fixtures/common.json` | 30 个可审阅的固定用例，包括适配器边距回归 |
| `tests/reference/test_reference.py` | 22 项工具自测，包含真实进程协议错误和失败用例重放 |

Python 把同一份 JSON 输入编码成带版本的空白分隔协议，一次批量发送给两个独立进程。C++ 端不需要 JSON 库；两端均拒绝错误协议版本、截断输入、超限计数、非法数值和多余尾部数据。

输出一行对应一个用例，包含输入顺序中的 ID、`rect`、`cell` 和实际占用 `size`。对照器先校验返回数量、顺序、ID、数组形状和有限非负数值，再比较几何。任一进程失败、超时或输出损坏均返回失败，不作为“跳过”。

## 共同子集

准入规则仅根据原始输入判断，不读取任一布局结果来筛选。生成用例和手动重放都经过相同校验；不支持的输入明确报错。

| axisly 输入 | Taffy 映射 | 准入条件 |
| --- | --- | --- |
| Fixed(s) | 固定长度 s | 允许零轨道、容器不足及布局超出容器 |
| Weighted(w, m) | `minmax(m, w fr)` | 无上限，每个权重 ≥1；该轴条目 min/preferred/margin 均为零且 Stretch |
| Range(Fixed(m), Weighted(w)) | `minmax(m, w fr)` | 与上述权重轨道相同 |
| Content | `minmax(min-content, max-content)` | 同一轴不含权重轨道，不跨格，空间足够容纳首选尺寸总和 |
| 显式放置与跨度 | 从零基索引转为 CSS 一基网格线及 span | 允许固定／权重轴上跨行、跨列和重叠条目 |
| Start／Center／End／Stretch | 对应 justify-self／align-self | 固定／Content 轴上 margin 不超出单元格；非 Stretch 首选尺寸可容纳 |
| padding／gap | 固定长度 | 有限、非负；两边都采用逻辑单位，不取整 |

每轴 1～16 条显式轨道，每用例至多 128 条目，每次 1～10000 个用例。数值输入范围 0～1000000；ID 为完整 uint64，包含 0 和最大值。

权重都至少为 1，是为了排除 CSS 总 fraction 小于 1 时只占用部分自由空间的语义差异。条目在权重轴上的固有贡献和 margin 归零，仍保留轨道显式数值下界、冻结重新分配和最小值溢出的对照。某轴含 Content 时要求首选尺寸可容纳，避免把 axisly 的同比收缩规则当成 CSS 轨道算法。

以下仍由 axisly 原有测试验收，不纳入本阶段 Taffy 相等断言：Content 收缩、含 Content 的跨格分配、单轴 Content 与权重混合、权重上限、其他 Range 组合、超大 margin、被裁剪的非 Stretch 首选尺寸、空轴、依赖宽度换行的回调、嵌套宿主策略、自动放置、百分比及 CSS 其他特性。

因此，对照成功证明的是上述共同子集的一致性，不代表与 CSS Grid 完全兼容，也不能证明所有布局均无缺陷。

## 测量与几何映射

用例提供恒定的 minimum/preferred 度量。axisly 回调返回它们；Taffy 在 MinContent 查询时返回 minimum，其他未知尺寸查询返回 preferred，已知尺寸按其约束返回。可用空间作为建议，不额外裁剪这份恒定度量。axisly 既有规则仍负责最终尺寸和对齐。

Taffy 调用 `disable_rounding()`。根节点按输入容器使用 border-box，轨道整体对齐设为 Start，避免额外分配固定／内容轨道周围的剩余空间。叶节点最小样式尺寸显式为零，避免 CSS 自动最小值混入 axisly 的条目规则。

Taffy 的根布局尺寸代表容器盒子，不能直接当作 axisly 的占用范围。因此先在仅包含真实条目的树上计算并保存原始矩形，再加入零固有尺寸、零 margin、Stretch 的探针：

- 每个条目的探针使用同一放置／跨度，取得完整 `cell_rect`。
- 全网格探针覆盖全部显式行列，取得轨道与内部 gap 的总尺寸，再加根 padding 得到占用 `size`。

第二次计算会检查每个原始条目的矩形是否变化，超过同样误差即报错。用于比较的条目矩形来自加入探针之前的计算，避免探针无意中改变参考结果。

每个分量使用：

```text
abs(axisly - taffy) <= 0.0001 + 0.000001 * max(abs(axisly), abs(taffy))
```

先拒绝 NaN、无穷、负尺寸／坐标和无法表示为有限 Unit 的输出。这个误差用于 float 计算差异，不用于掩盖整像素偏移；工具自测会验证超过阈值的条目、单元格和占用差异均被检出。

## 构建与运行

在 MSVC 环境中，或 Linux/macOS 的原生编译器环境中：

```text
cmake --preset reference-release
cmake --build --preset reference-release
ctest --preset reference-release
```

Debug 替换为 `reference-debug`。预设依赖 Ninja；使用其他生成器时可手动指定 `-DAXISLY_BUILD_REFERENCE_TESTS=ON`。默认 CTest 包含原 205 项，以及 `axisly.reference.compare`、`axisly.reference.harness` 两项；后者内部执行 22 个工具自测。

只构建和运行参考工具：

```text
cmake --build build/reference-release --target axisly_reference_native axisly_taffy_reference
ctest --test-dir build/reference-release -L reference --output-on-failure
```

默认种子 20260925，随机生成固定、权重、Content 三类，各 250 个；加上 30 个固定用例，总计 780。固定用例覆盖 16 种双轴对齐组合、分数坐标、容器为零、跨格重叠、显式下界、空条目、Content 测量和最大 uint64 ID。

## 失败文件与重放

几何不一致时，工具返回 1，并在指定 failure-dir 写入 `failure.json`：包含 schema=1、一个完整输入用例、种子、差异分量和两边结果。协议或进程错误保留整批输入，以便重现；非法源输入返回 2。

以下为 Windows x64 本机构建路径；其他平台使用构建日志中的 Rust host 目录及无 `.exe` 后缀的可执行文件：

```powershell
python tests/reference/compare.py `
  --axisly build/reference-release/tests/reference/axisly_reference_native.exe `
  --taffy build/reference-release/tests/reference/cargo/x86_64-pc-windows-msvc/release/axisly-taffy-reference.exe `
  --replay build/reference-release/tests/reference/failures/failure.json `
  --failure-dir build/reference-release/tests/reference/replay
```

指定 `--replay` 后只重放文件中的用例，不追加随机数据；diagnostic 保留为旧结果记录，不作为期望值输入。成功运行不会删除旧失败文件，避免丢失调查材料。新的失败会覆盖同一 failure-dir 下的 `failure.json`，需要保留多个问题时使用不同目录。

不指定 `--replay` 时，可通过 `--seed` 和 `--random-cases` 调整生成范围，后者是每类的数量。目录与输入不会触发 shell 命令，子进程通过参数列表启动；Windows 批量验证不弹出控制台。

## 本机验证记录

2026-09-25，Windows x64，MSVC 19.51.36257、CMake 4.4.2、Ninja、Rust/Cargo 1.98.0、Python 3.14.7：

| 验证项 | 结果 |
| --- | --- |
| reference-debug | 无编译告警，207/207 CTest 通过 |
| reference-release | 无编译告警，207/207 CTest 通过 |
| 两种配置中的实际对照 | 各 780/780，通过上述误差检查 |
| 工具自测 | 各 22/22；覆盖错误输出、准入拒绝、差异检测、失败保存和真实双进程重放 |
| 真实适配器回归重放 | Content 同列不同 margin 用例通过，已保留在固定用例中 |
| Rust 格式／静态检查 | `cargo fmt --check`、`cargo clippy --locked -- -D warnings` 通过 |
| 纯核心构建 | 关闭测试／示例后仅构建 C++ 静态库，未查找 Rust/Python |
| 安装包／源码子目录接入 | 各 1/1，通过原公开 API 计算 |

GitHub Actions 增加单独的 Linux 参考验证任务，失败时上传用例和 CTest 日志。当前记录为本机验证，远端 CI 尚未运行；Linux/macOS、真实字体／UI 和 SDL3 图形运行不在本次已验证范围内。下一阶段是 M9 SDL3 可视化演示。
