# M1 固定轨道布局

本文记录 M1（`0.0.2`）阶段的行为与验证。当前版本已推进至 [M2 权重布局](10-weighted-layout.md)，纯固定轨道的几何语义保持不变。核心只依赖 C++20/STL。

## 公共入口

```cpp
gridly::GridResult gridly::compute_grid(
    const GridSpec& spec,
    std::span<const GridItem> items,
    Size available);
```

声明位于 `<gridly/layout.hpp>`，也由 `<gridly/gridly.hpp>` 导出。`GridSpec` 和 items 只在同步调用期间借用，结果拥有独立的矩形数组。计算不修改输入，不缓存状态，不持有宿主对象。

M1 支持固定行、固定列、显式单格放置、独立行列间隙、非对称 padding 和两轴 Stretch。条目输出顺序与输入一致。不同 ID 可以显式占据同一个单元格，宿主决定绘制和层级顺序。

## 几何语义

以列为例，行的公式相同：

```text
column[i].x = padding.left + sum(column[0..i-1].width) + i * column_gap
item.width  = column[item.column].width

result.width = padding.left + sum(column widths)
             + max(column_count - 1, 0) * column_gap + padding.right
```

只有相邻轨道之间存在 gap，末尾不增加 gap。零尺寸轨道合法，相邻零尺寸轨道之间仍保留间隙。不进行像素取整。

`available` 是宿主提供的外部空间，尺寸必须有限且非负。固定轨道不根据它缩放或裁剪；当容器为零或不足时，条目可以超出容器。`GridResult::size` 表示整个显式网格连同 padding 的实际占用范围，不是简单回传 `available`，也不只是已放置条目的包围盒。网格空白单元格仍占据其轨道尺寸。

没有条目的空网格合法；没有轨道的轴仅占据该轴的 padding，不产生 gap。如果存在条目，它引用的行、列必须真实存在。M1 尚不创建隐式轨道。

## 校验与失败

计算按容器、gap、padding、列、行、输入条目的次序校验，返回首个遇到的错误。同一条目先校验放置、span、margin、alignment 和 ID，再检查 M1 功能支持范围。位置信息必须有效后才判断 span 是否越界，使用减法比较避免无符号加法回绕。

| 输入情况 | 错误码 |
| --- | --- |
| 容器尺寸为负、NaN 或无穷 | `InvalidContainerSize` |
| gap 为负、NaN 或无穷 | `InvalidGap` |
| padding 为负、NaN 或无穷 | `InvalidPadding` |
| 固定轨道尺寸非法 | `InvalidTrack`，正常工厂会在创建时提前拒绝 |
| 行列索引越界或引用空轴 | `InvalidPlacement` |
| span 为零或超出已有轨道 | `InvalidSpan` |
| margin 为负、NaN 或无穷 | `InvalidMargin` |
| alignment 为未定义枚举值 | `InvalidAlignment` |
| 同一次调用中存在重复 ID（包括零） | `DuplicateItemId` |
| 有效但尚未实现的功能 | `UnsupportedFeature` |
| 累计坐标或占用尺寸超出 `Unit` 有限范围 | `NumericOverflow` |

M1 未实现的功能包括：Weighted／Content／Range 轨道，范围内但大于 1 的 span，非零 margin，以及 Start／Center／End 对齐。它们会被明确拒绝，不会被当作固定尺寸或默认参数处理。测量回调入口将在后续阶段加入。

失败结果的 `size` 为零，`items` 为空，`error.message` 提供诊断；条目错误带 `error.item_id`，网格错误没有条目 ID。成功时 `error.code == None`，诊断为空。内存分配异常按 STL 行为传播，不转成布局错误。

## 实现边界

`src/compute_grid.cpp` 负责校验、编排和结果生成。M1 的 `src/detail/fixed_tracks.cpp` 将公共轨道解析成内部的 offset/size，使用 `double` 累加并在转换为 `Unit` 前检查范围；M2 将该实现合并到 `src/detail/track_sizing.cpp`。内部头文件不安装，不属于公共 API。

这一阶段尚不提供 SDL3 窗口、测量回调计算、跨轨道贡献、像素取整和真实 UI 框架适配。下一阶段 M2 在保持固定轨道行为的基础上加入权重空间分配。

## 验证

M1 测试位于 `tests/fixed/`：20 项固定布局测试加上原有 8 项数据契约测试。黄金矩形使用显式预期值；误差容限为绝对值 `1e-5`，重复计算还检查输出完全相等。

覆盖单格、多行、多列、非对称 padding、间隙、小数、零容器、零轨道、空轴、容器溢出、ID 和顺序、各类非法输入、未实现功能、算术溢出、输入不变性及失败后的重复调用。7 个公共头文件分别编译。`tests/consumer/` 通过实际调用 `compute_grid()` 验证子目录接入和安装包接入。

2026-09-24，Windows x64，MSVC 19.51.36257，CMake 4.4.2，Ninja：

| 验证项 | 结果 |
| --- | --- |
| Debug 构建与测试 | 无编译告警，28/28 通过 |
| Release 构建与测试 | 无编译告警，28/28 通过 |
| 7 个公共头文件分别编译 | 两种配置均通过 |
| `gridly_api_example.exe` 实际运行 | 3×2 网格，6 个矩形，占用尺寸 `500 × 232` |
| 安装包 `find_package(gridly)` | Release 编译、链接与计算验证通过，1/1 |
| 子目录 `add_subdirectory()` | Debug 编译、链接与计算验证通过，1/1 |

跨平台 CI、真实 UI 和 SDL3 渲染验证不属于本次本机结论。
