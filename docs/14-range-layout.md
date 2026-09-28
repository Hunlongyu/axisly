# M6 Range 轨道、上下界与重新分配

`0.0.7` 实现全部六种合法 Range 组合。固定／Content／权重／Range 定义会转换为本次计算使用的 `TrackState`，再参与测量贡献、跨格分配和最终尺寸计算。转换代码位于 `src/detail/track_state.hpp/.cpp`，公共轨道保持不变。

## 六种组合

```cpp
auto bounded = Track::range(Track::fixed(80), Track::fixed(160));
auto preferred = Track::range(Track::fixed(80), Track::content());
auto flexible = Track::range(Track::fixed(100), Track::weighted(2));
auto capped = Track::range(Track::content(), Track::fixed(240));
auto intrinsic = Track::range(Track::content(), Track::content());
auto intrinsic_flexible = Track::range(Track::content(), Track::weighted());
```

以下用 L／U 表示固定下界／上界，m／p 表示该轨道的单格最小／首选尺寸贡献。贡献包含该轴 margin；多个条目取最大值，空轨道的 m、p 为零。

| 下界 | 上界 | 初始最小值 | 首选目标／增长规则 |
| --- | --- | --- | --- |
| Fixed(L) | Fixed(U) | L | `clamp(p, L, U)`，按内容增长且不超过 U |
| Fixed(L) | Content | L | `max(L, p)`，按内容增长 |
| Fixed(L) | Weighted(w) | L | 按 w 分配剩余空间 |
| Content | Fixed(U) | `min(m, U)` | `min(p, U)`，按内容增长且不超过 U |
| Content | Content | m | p，等价于普通 Content |
| Content | Weighted(w) | m | 按 w 分配剩余空间 |

表中是收集单格贡献后的初始约束，之后仍会计入跨格要求。无测量回调时，m 来自零条目 minimum 加 margin，p 来自静态 `preferred_size` 加 margin。

Fixed 上界是数值上限；该类 Range 根据内容首选尺寸增长。它不会自动占满上限。例如 `80..160`、首选尺寸 120、容器宽度 500 时，轨道只占用 120；没有条目时占用 80。需要分配容器剩余空间时使用 Weighted 上界。

## 下界与冲突处理

Fixed 下界为该轴提供显式最小值。较小的内容 preferred 不会降低它，较大的单格内容 minimum 也不会自动提高它。例如 `range(fixed(80), fixed(160))`，测量 minimum=200、preferred=240，容器宽度 100 时分配 100。若需要随内容调整最小值，应选择 Content 下界。

固定下界也替代单格 margin 对 minimum 的贡献；margin 仍会计入首选尺寸并在最终对齐时扣除，过大边距仍按 M3 规则折叠尺寸。跨格 minimum 可在上限内提高计算下限，延续 M5 的跨度约束规则。

Fixed 上界具有硬限制作用。Content minimum=200、上界=160 时，该轨道的最小和首选值都会裁剪到 160，条目按实际区域布局，不把上界扩大到 200。所有轨道达到数值上限后留下剩余空间。

固定下界大于固定上界仍属于工厂参数错误，`Track::range()` 抛出 `std::invalid_argument`。Weighted 下界、嵌套 Range、携带额外 minimum／maximum 的 Weighted 上界继续被拒绝。合法的 Weighted 上界形式是 `Track::weighted(weight)`。

## 分配与跨格

上界为 Fixed 或 Content 的 Range 与普通 Content 一起参与内容尺寸分配。先预留固定尺寸、padding、gap 和权重计算下限，再满足内容首选目标；空间不足时按各轨道 `preferred - minimum` 的容量同比收缩。达到 minimum 后允许网格溢出容器。

上界为 Weighted 的 Range 与普通 Weighted 使用同一比例分配器。`range(fixed(L), weighted(w))` 等价于 `weighted(w, L)`；Content 下界则额外提供动态测量 minimum。最终分配依旧是受上下限约束的权重比例尺寸，而非在 minimum 上再加一份比例空间。

跨格处理保留 M5 的短跨度优先、同跨度快照和逐轨道最大增量合并规则，并增加有上限的内容增长：

1. minimum 缺口先分配给跨度内按内容增长的轨道，包括 Fixed／Content 上界的 Range。
2. 可增长轨道平分增量；达到上限的轨道冻结，未满足的部分继续分给其余内容轨道。
3. 内容轨道全部达到上限后，剩余 minimum 缺口交给跨度内权重轨道，按权重继续冻结和重新分配。
4. preferred 缺口只扩大内容目标，不提高权重 minimum。所有上限都不足以满足内容时，保留合法轨道尺寸并裁剪条目。

例如三个内容 Range 的上限为 10、30、100，跨三列 preferred=90，初始下界均为零。前两列依次冻结，最终首选分配为 `10、30、50`。容器只有 45 时，再按可收缩量得到 `5、15、25`。

另一个跨度内包含上限 100 的内容 Range，以及权重为 1、3 的两条轨道，第一条权重轨道上限为 30。跨格 minimum=300 时，内容先分得 100，剩余 200 交给权重轨道，最终为 `100、30、170`。

## 测量与数值边界

Range 不增加测量轮数。列使用第一次测量的宽度；列和跨格区域确定后，以最终条目宽度重测高度，行的 Content 下界和首选目标使用第二次结果。known_height 仍保持未知，已知零宽度仍通过 `optional{0}` 传入。

minimum／preferred 必须有限、非负且顺序合法；即使上限为零，也不能用裁剪掩盖非法回调结果。非法值继续返回 `InvalidMeasurement`，并记录条目 ID 和实际回调次数。

内部无显式上限以 double 的无穷表示，仅用于约束元数据；公共尺寸、测量结果和最终几何仍要求有限。尺寸加 margin、跨度累计和上下界处理使用 double。合法的固定上限可在最终分配前裁剪超大内容贡献；没有固定上限且 minimum 总占用超出有限 Unit 时，仍返回 `NumericOverflow`。输出保留 float 精度，不做像素取整。

所有已定义的合法 Track 组合和显式跨度现在都能参与计算。`UnsupportedFeature` 枚举保留，但不再用于拒绝合法 Range。

## 示例与验证

新增 `axisly_range_example`，使用两个条目和模拟度量：

- 第一列为 `range(fixed(80), fixed(160))`，首选宽度 240。
- 第二列为 `range(content(), weighted())`，最小宽度 100，首选宽度 400。
- 行为 `range(fixed(30), content())`，根据宽度重测后的文字高度确定首选目标。
- 列间 gap=12，四侧 padding 均为 12。

实际输出：

| 容器宽度 | 两列宽度 | 网格占用 | 测量次数 |
| --- | --- | --- | --- |
| 600 | 160、404 | 600 × 54 | 4 |
| 300 | 160、104 | 300 × 104 | 4 |
| 180 | 80、100 | 216 × 104 | 4 |

最后一行的最小占用超过容器，保留两列下界。Windows 独立启动时保留输出，按回车关闭；已有终端及重定向运行正常结束。

2026-09-24，Windows x64，MSVC 19.51.36257，CMake 4.4.2，Ninja：

| 验证项 | 结果 |
| --- | --- |
| Debug 构建与测试 | 无编译告警，171/171 通过 |
| Release 构建与测试 | 无编译告警，171/171 通过 |
| M6 新增测试 | 31 项，覆盖六种组合、冲突、空内容、跨格冻结、重测量和数值边界 |
| 与既有轨道的等价对照 | 500 组，比较 Fixed/Fixed 等值范围、Fixed/Weighted 和 Content/Content，包括跨格与换行 |
| 随机 Range 边界 | 500 组，检查显式上限、动态／固定下限、输入顺序和测量宽度一致性 |
| 原 M0～M5 回归 | 140 项继续通过，原 Range 未实现断言已改为合法范围验收；此前四组各 500 个数值／随机用例仍通过 |
| 公共头文件 | 7 个独立编译，Debug／Release 均通过 |
| 安装包／子目录接入 | 分别 1/1 通过，实际计算固定上限列和内容下界权重行 |
| 六个 Release 示例独立启动 | 隐藏控制台读取结果与回车提示，输入回车后均以 0 退出 |

上述为 M6 阶段的本机算法、接入和控制台运行记录。后续已完成 [M7 嵌套网格接入](15-nested-grid-integration.md)；Taffy 对照、真实字体／宿主 UI、SDL3 图形演示和跨平台运行尚未完成。
