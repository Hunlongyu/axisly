# M0 初始化决策

本文保留 M0（`0.0.1`）阶段的决策与验证快照。后续进度见 [M1 固定布局](09-fixed-layout.md) 和 [M2 权重布局](10-weighted-layout.md)。

## 名称与工程边界

原始初始化阶段使用 `gridly`；当前统一名称为 `axisly`。CMake 工程和包名称为
`axisly`，链接目标为 `axisly::axisly`，C++ 命名空间为 `axisly`，公共入口为
`<axisly/axisly.hpp>`。以下命令和示例已更新到当前名称，早期验证数据仍是
当时 Grid 专项阶段的结果。

M0 提供 C++20/STL 静态库和公共值类型，不实现布局算法，不引入 SDL、Qt、Rust、Taffy 或外部测试库。构建目录为 `build/`，不进入版本控制。`0.0.1` 仅标识初始化阶段。

## 公共类型

现有提案中的 Geometry、GridSpec、GridItem、alignment、测量回调和输出类型已经落地。新增错误信息中的可选 `item_id` 用于区分网格错误与某个条目的错误；默认结果为 `NotComputed`，防止尚未计算的空结果被解释为成功。

固定轨道尺寸为非负有限值，权重为严格正的有限值。工厂在创建时拒绝 NaN、无穷值和非法数值，使用 `std::invalid_argument` 报告程序传参错误。后续计算入口使用结果错误类型报告放置、几何和测量数据错误；内存分配异常不承诺转换为状态值。

## Range 表示

```cpp
auto bounded = axisly::Track::range(axisly::Track::fixed(100), axisly::Track::fixed(300));
auto flexible = axisly::Track::range(axisly::Track::fixed(100), axisly::Track::weighted(1));
```

Range 下界支持 Fixed 或 Content，上界支持 Fixed、Content 或 Weighted；不支持嵌套 Range，也不将 Weighted 解释为最小尺寸。固定上下界在工厂阶段校验顺序。使用 `std::variant` 保存各类定义，避免递归堆分配；调用者可以只读检查 `definition()`。

Content 上下界的测量贡献、无法同时满足的内容约束、冻结与重新分配策略留到 M4/M6 通过测试明确。当前仅确定可表达的数据结构，不宣称已经解决这些算法语义。

## 验收范围与后续工作

M0 契约测试覆盖：几何初始值、轨道创建和非法参数、Range 数据及非法组合、2x2/3x3 网格、显式放置、双向 span、边距和对齐表达、测量回调的未知/零约束、宿主上下文、结果与错误表达。每个公共头文件单独编译。

这些验证证明数据模型可用，不构成任何布局算法、窗口、像素对齐或 SDL 渲染验证。GitHub Actions 配置了 Windows/MSVC 和 Linux/GCC/Clang，实际远程执行结果应另行记录。

M1 的交付范围按原路线图：固定行列、显式放置、非对称 padding、独立 gap、矩形输出和零容器尺寸测试。届时增加真实的 `compute_grid()`；测量便利重载、调试轨道数据及最终舍入选项按需要引入，不先加入空实现。

## 本机验证记录

2026-09-24，Windows x64，MSVC 19.51.36257，CMake 4.4.2，Ninja：

| 项目 | 结果 |
| --- | --- |
| Debug 构建与契约测试 | 通过，8/8 |
| Release 构建与契约测试 | 通过，8/8 |
| 6 个公共头文件分别编译 | 两种配置均通过 |
| API 示例运行 | 输出 3 列、3 行、3 个条目的 M0 模型 |
| 安装后 `find_package(axisly)` 消费工程 | Release 编译、链接、运行通过，1/1 |
| `add_subdirectory()` 消费工程 | Debug 编译、链接、运行通过，1/1；库内测试和示例默认关闭 |

最终构建未产生编译告警。消费工程位于 `tests/consumer/`，可分别通过 `CMAKE_PREFIX_PATH` 或 `AXISLY_SOURCE_DIR` 指定接入方式。尚未执行远程 CI 或 Linux/macOS 构建。
