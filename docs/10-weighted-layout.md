# M2 权重轨道布局

本文记录 M2（`0.0.3`）的行为与验证。当前版本已推进至 [M3 对齐与边距](11-alignment-and-margin.md)，权重分配语义保持不变。纯固定轨道继续使用 M1 的几何规则。

## 公共接口

```cpp
Track Track::weighted(
    float weight = 1.0f,
    Unit minimum = 0.0f,
    std::optional<Unit> maximum = std::nullopt);
```

原有 `Track::weighted()` 和 `Track::weighted(2)` 调用保持有效。为落实 M2 路线图的上下限、冻结和重新分配要求，增加两个可选参数；没有把 Content 或 Range 的完整语义提前引入计算入口。

```cpp
auto plain = axisly::Track::weighted(2);
auto at_least = axisly::Track::weighted(1, 120);
auto bounded = axisly::Track::weighted(1, 80, 240.0f);
```

权重必须为有限正数，minimum 为有限非负数，maximum 缺省表示没有用户指定的上限，指定时须有限且不小于 minimum。上下限相等合法，包括上下限均为零。工厂拒绝非法参数并抛出 `std::invalid_argument`；计算入口也校验轨道定义。

`WeightedTrack` 的只读定义现在包含 weight、minimum 和 maximum。API 尚未稳定，升级静态库时应同时更新头文件并重新编译消费工程，不保证与 `0.0.2` 二进制兼容。

## 分配语义

每个轴先扣除 padding、相邻轨道间的 gap 和固定轨道尺寸，再对剩余预算分配权重轨道。

在上下限允许满足预算时，求一个共同的比例系数 `k`：

```text
size[i] = clamp(k * weight[i], minimum[i], maximum[i])
sum(size[i]) = available - padding - gaps - fixed_sizes
```

最小值是比例结果的下限，不是预先分配后再叠加一份权重。例如两条等权轨道、第一条 minimum 为 80：总预算 100 时得到 `80, 20`，总预算 200 时得到 `100, 100`。

- 固定轨道不收缩，也不参与权重比例。
- 无额外约束时，`1:2:3` 的权重按 `1:2:3` 分配剩余预算。
- 预算不足以容纳最小尺寸时保留全部最小尺寸，允许网格溢出。
- 预算超过全部最大尺寸时保留全部最大尺寸，剩余空间不分配。
- 空间恰好耗尽时，minimum 为零的权重轨道得到零尺寸；padding 和 gap 继续存在。
- 空轴仍只占据该轴的 padding。没有条目时，显式轨道仍正常计算。

`GridResult::size` 继续表示实际占用范围。它可以等于 available（预算可以分完）、大于 available（固定尺寸／最小尺寸溢出）或小于 available（全部触顶、纯固定轨道或空轴）。

## 冻结与重新分配

实现位于 `src/detail/track_sizing.cpp`。一次试分配后比较约束造成的总差额，只冻结确定需要保留的那一类边界，再对其余轨道分配剩余预算。

不能同时冻结全部上下限违例。例如等权的两条轨道，总预算 100，第一条 minimum 为 60，第二条 maximum 为 1：初次按比例分配为 `50, 50`，最终应为 `99, 1`，第一条不能被永久固定在 60。

每轮重新求未冻结轨道的权重和，避免通过减去巨大权重丢失极小权重。内部使用 `double`，最终转为 `Unit`；输出保留浮点精度，不进行像素取整。累计坐标在已求出的有限占用边界内处理舍入误差，避免有限最大容器被误判为无穷。不可表示的最小占用范围仍返回 `NumericOverflow`，不输出部分结果。

## Range 与后续阶段

M2 的数值上下限属于 Weighted 本身。`Track::range(...)` 暂时仍返回 `UnsupportedFeature`，Content 贡献和 Range 组合分配留在 M4／M6。

Range 的 Weighted 上界只接受 `Track::weighted(weight)` 形式，不接受另带数值上下限的 Weighted，避免两套上下限重复定义。原有 `Track::range(Track::fixed(100), Track::weighted(1))` 数据模型不受影响。

M3 下一步处理 margin 与 Start／Center／End／Stretch 对齐。M2 仍只支持零 margin、单格放置与两轴 Stretch；测量回调、span 和 SDL3 演示尚未实现。

## 测试与验证

新增 22 项权重测试，总计 50 项 CTest：原有 8 项数据契约、20 项固定布局、22 项权重布局。覆盖比例、混排、双轴、非对称 padding／gap、零空间、空间不足、上下限、相互冲突的约束、逐轮冻结、触顶留白、极大／极小权重、最大有限／次正规容器尺寸、权重缩放、重复 resize 和输入不变性。

其中一项使用固定种子 `20260924` 生成 500 组用例，以独立的二分求解方法验证上述 clamp 方程。普通比较容限为绝对值 `1e-5` 与相对值 `1e-6` 的较大者；次正规数另做精确检查。该验证不等同于路线图 M8 的 Taffy 差分测试。

2026-09-24，Windows x64，MSVC 19.51.36257，CMake 4.4.2，Ninja：

| 验证项 | 结果 |
| --- | --- |
| Debug 构建与测试 | 无编译告警，50/50 通过 |
| Release 构建与测试 | 无编译告警，50/50 通过 |
| 独立方程求解对比 | 两种配置均通过 500 组固定种子用例 |
| 7 个公共头文件分别编译 | 两种配置均通过 |
| 权重示例实际运行 | 640、940、190 三种容器宽度下正确分配／溢出 |
| 安装包 `find_package(axisly)` | Release 编译、链接与固定／权重上下限计算验证通过，1/1 |
| 子目录 `add_subdirectory()` | Debug 编译、链接与固定／权重上下限计算验证通过，1/1 |

跨平台 CI、真实 UI 与 SDL3 渲染不属于此次验证范围。
