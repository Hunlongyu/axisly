# 原生 Grid 指南

`axisly::compute_grid` 接收轨道、条目和有限可用空间，同步返回布局。
需要缓存时使用 `GridCache`；需要保留父子关系时使用 `GridTree`。
这些接口采用原生 UI 的轨道约束语义。使用 CSS Block／Flexbox／Grid 混合布局时，
请使用独立的 [`css::LayoutTree`](css-layout.md)。

## 最小示例

```cpp
#include <axisly/axisly.hpp>
#include <array>

axisly::GridSpec spec;
spec.columns = {axisly::Track::fixed(180), axisly::Track::weighted(1), axisly::Track::weighted(2)};
spec.rows = {axisly::Track::fixed(80), axisly::Track::weighted()};
spec.column_gap = 8;
spec.row_gap = 8;
spec.padding = {12, 12, 12, 12};
const std::array items{
    axisly::GridItem{.id = 1, .column = 1, .row = 0},
    axisly::GridItem{.id = 2, .column = 2, .row = 1}
};
auto result = axisly::compute_grid(spec, items, {640, 360});
if (result.succeeded()) {
    // result.items[0].rect == Rect{200, 12, 140, 80}
    // result.items[1].rect == Rect{348, 100, 280, 248}
}
```

## 轨道和尺寸

| 轨道 | 规则 |
| --- | --- |
| `Track::fixed(size)` | 保留固定尺寸，空间不足时允许溢出 |
| `Track::weighted(weight, minimum, maximum)` | 按权重分配剩余空间，并满足上下限 |
| `Track::content()` | 根据内容的最小／首选尺寸增长或收缩 |
| `Track::percentage(ratio)` | 按扣除 padding 后的可用空间解析，不预先扣 gap |
| `Track::range(lower, upper)` | 下界支持 Fixed／Content／Percentage，上界还支持 Weighted |

权重分配满足 `size[i] = clamp(weight[i] * lambda, minimum[i], maximum[i])`。
minimum 是尺寸下界；例如等权轨道、第一条下界 80，预算 100 时分配 80／20，预算 200 时分配 100／100。
约束触顶后冻结该轨道并重新分配；空间低于全部下界时允许溢出，全部触顶时保留未用空间。

Content 轨道在 preferred 与 minimum 之间按可收缩量分配空间。
Range 的固定上界限制内容增长；固定下界可以覆盖较小的内容首选尺寸。
Range 不允许嵌套，Weighted 不能作为下界，作为上界时不能再带独立的数值上下限。

`GridResult::size` 是含 padding 的实际网格占用，不一定等于 available。
跨格条目的区域包含跨度内的 gap；跨格最小贡献先扩大内容轨道，再在上限内提高权重轨道下界。
固定轨道不因内容贡献而变大。条目默认 Stretch，其他对齐使用最终可用单元格内的首选尺寸。

## 输入、测量与所有权

- `Unit` 为 `float`；`Insets` 顺序为左、上、右、下。行列从 0 开始，span 默认 1。
- `ItemId = 0` 合法，同一次计算中 ID 必须唯一；输出保持输入顺序。margin 必须有限且非负。
- 默认没有测量回调时，最小尺寸为零，首选尺寸取 `GridItem::preferred_size`。
- 有回调且成功时，每项测量两次：先确定内容贡献，再按最终对齐后的宽度重测高度。
  `known_height` 保持未知，`known_width` 的已知零与未知不同。
- 测量的 minimum／preferred 必须有限、非负且各轴 minimum 不大于 preferred。
- `compute_grid` 同步借用输入，不修改输入，也不保存跨次状态；支持函数指针和捕获式 lambda。
- 回调及其 context 由宿主保证有效，计算期间不能修改输入；回调异常原样传播。
- 失败返回错误码、零尺寸和空条目列表；非法 Track 工厂参数抛出 `invalid_argument`。

## 完整结果缓存

```cpp
axisly::GridCache cache(spec, items);
auto first = cache.compute({640, 360});
auto same = cache.compute({640, 360}); // same.cache_hit == true
const auto& layout = same.result;
auto updated = cache.items()[0];
updated.margin.left = 12;
cache.set_item(0, updated); // 按输入下标更新
```

`set_spec`、`set_items`、`set_item`、`set_measure` 的实际变化自动失效缓存。
外部文字、字体、DPI 等改变时调用 `invalidate()`，或改变 `compute(size, content_version)` 的版本。
更新接口用于输入改变时，无需每帧重复提交整份输入。

缓存拥有输入快照，回调 context 仍由宿主拥有。借用结果需在下一次计算、修改或销毁前消费，
长期持有时显式复制。`CachedGridResult::measurement_count` 是本次调用的实际测量次数，
命中为零；`result.measurement_count` 保留生成该布局时的计数。错误结果不缓存。
缓存和树实例需要独占访问；回调不能修改或递归计算同一实例。

## 自动放置与隐式轨道

```cpp
axisly::GridSpec spec{
    .columns = {axisly::Track::weighted(), axisly::Track::weighted()},
    .rows = {axisly::Track::content()},
    .column_gap = 8,
    .row_gap = 8,
    .auto_flow = axisly::AutoFlow::RowDense,
    .allow_implicit_tracks = true
};
axisly::GridItem item{
    .id = 7,
    .column = axisly::auto_position,
    .row = axisly::auto_position,
    .preferred_size = {120, 24}
};
```

`AutoFlow` 包含 `Explicit`（默认）、`Row`、`Column`、`RowDense`、`ColumnDense`。
`auto_position` 可用于一个或两个轴。默认 `GridItem{}` 仍是第 0 行第 0 列；不会把旧代码
自动改成流式布局。行流逐列再换行，列流逐行再换列；Dense 为每项重新搜索前面的空洞，
稀疏模式从上次游标继续。

先占用完全显式条目，再放置主轴已指定的条目，最后处理主轴自动的条目。自动条目避开
所有已占用单元，跨度中的单元都算占用；完全显式条目仍允许重叠。结果保持输入顺序，
`ItemLayout` 的 `column`／`row`／`column_span`／`row_span` 给出解析后的放置。

轨道增长必须设置 `allow_implicit_tracks`。`implicit_column`／`implicit_row` 是统一的
补充轨道模板，默认 Content，可使用任意合法 Track。纯显式坐标也可请求轨道增长。
`max_implicit_tracks` 默认每轴最多新增 4096 条；`max_placement_work` 默认 1,000,000，
限制搜索与占用处理工作。超限分别返回 `TrackLimit`／`PlacementLimit`，不发布部分结果。
不允许增长且放不下时返回 `InvalidPlacement`。这套数值 API 不包含具名网格线、模板区域、
自动重复模板或负索引。

## 百分比与 Range

`Track::percentage(0.25f)` 表示 **25%**，参数是比例而非 25。比例必须有限且非负；可大于 1，
此时允许像 Fixed 一样溢出容器。

基数为 `max(0, available - 起始 padding - 结束 padding)`，**不预先扣 gap**。
gap 单独加入占用，因此两个 50% 轨道加 gap 可能超出容器；剩余权重轨道只分剩余空间。
每次重排都重新计算基数，不缓存旧尺寸的百分比值。

Range 的上下界均可用 Percentage；下界仍不能用 Weighted，上界 Weighted 不能再附带
独立数值限制。例如 `range(percentage(.2f), weighted())`。
同单位固定上下界或百分比上下界倒置在工厂中报错；混合单位可能在某些尺寸下倒置，
解析时以下界为准。Content 最小值仍受数值上界约束。

## 首基线对齐

`VerticalAlignment::Baseline` 按条目**起始行**建立首基线组，支持跨行条目。
`MeasureResult::baseline` 是距条目顶部的偏移，必须在 `[0, preferred.height]` 内；
不传测量函数时使用 `GridItem::baseline`。未提供基线时使用首选高度的底边。
最终宽度测量返回的基线覆盖初始测量的基线。

同组最大 `margin.top + baseline` 决定目标基线，差值参与行的最小／首选尺寸贡献。
布局时对齐条目自身基线，底部 margin 仍参与所需高度。Fixed／Range 上限与现有约束优先；
空间不足时裁剪到单元格内，允许基线无法完全对齐。横向对齐行为不变。
不包含末基线、垂直书写方向或浏览器全部 baseline 合成规则。

## 像素取整

```cpp
auto result = axisly::round_layout(
    axisly::compute_grid(spec, items, available),
    axisly::PixelRounding{.scale = 1.25f, .origin_x = 0, .origin_y = 0});
```

取整只处理最终输出，测量和轨道分配仍使用逻辑坐标。先取整起止边缘再求宽高，避免
分别取整宽度产生裂缝。由位置和宽度重建的边缘先归一到 `Unit` 精度，与相邻条目的
公开坐标一致。公式为 `round((origin + edge) * scale) - round(origin * scale)`，
再除以 scale。origin 是整个布局的绝对逻辑原点，允许负值；返回坐标相对于已取整原点。
scale 必须有限且大于 0。函数按值接收结果，可移动临时结果或复制借用缓存；错误结果
原样返回，取整参数非法或数值溢出时返回空结果。不要反复取整同一份已取整几何。

树布局使用 `tree.compute(root, available, PixelRounding{scale})`，统一对根坐标取整，
避免每层先取整再叠加误差。默认没有取整。

## 条目测量缓存

`MeasurementCache` 可独立使用，默认每个 ItemId 保留最近 8 组完整约束，构造参数允许
1–64 组，LRU 淘汰。键包含 ItemId、known_width／known_height 的值和有无、available
的宽高；已知 0 与未知不同。回调绑定变化清空所有条目，非法测量不缓存，异常传播。
实例内条目数量由宿主控制，可用 `invalidate(id)` 删除条目、`clear()` 清空。
`measurement_count()` 和 `hit_count()` 自构造或 `reset_statistics()` 起累积。

```cpp
axisly::GridCache cache(spec, items);
cache.set_measure(measure, context);
cache.enable_measurement_cache();
auto first = cache.compute({640, 360});
// 某个条目的文本发生变化：
cache.invalidate_item(7);
auto changed = cache.compute({640, 360});
```

`GridCache` 默认不启用条目测量缓存，保持旧行为。启用后 `measurement_count` 统计实际宿主
回调次数，可以小于 2N。完整结果命中依然是 0 次。单条目失效不清除其他 ID 的测量。
`set_item` 清除受影响 ID，`set_items`／`set_spec` 的实际变化、`invalidate()` 或内容版本
改变会清除全部测量。失败／异常后丢弃该网格的测量条目，便于重新尝试。

宿主须在文字、字体、DPI、图片或任何外部测量依赖变化后主动失效；缓存不会猜测 context
地址背后的对象变化。仅当完整约束相等时复用，不能把“宽度一样”视为充分条件。

## GridTree

```cpp
axisly::GridTree tree;
auto root = tree.create_grid(spec);
auto label = tree.create_leaf(item, measure, context);
tree.append_child(root, label);
const auto& result = tree.compute(root, {640, 360});
if (result.succeeded()) {
    const auto* layout = result.find(label);
    // 使用 layout->rect 设置宿主控件坐标。
}
// 外部内容变更后：
tree.mark_dirty(label);
```

树拥有节点、样式和关系，宿主仍拥有控件与测量上下文。NodeId 在进程中唯一且不复用，
宿主 `GridItem::id` 另行保留并传入回调，允许不同节点使用相同宿主 ID。
`create_grid` 和 `create_leaf` 初始没有父级；`insert_child`、`append_child` 支持重新排序和
换父级；插入位置以从目标列表移除该子节点后的列表为准。每节点最多一个父级。
`detach` 保留子树，`remove` 删除整个子树；失效／跨树句柄和循环引用被拒绝。

`set_item`、`set_spec`、`set_measure` 的相同值不触发工作。叶子 `mark_dirty` 只失效该叶子
通往根的路径；grid 的 `mark_dirty` 表示其整个子树内容可能改变。未变化分支在相同约束下
复用测量和布局。失效的单个网格仍完整分配共享轨道，祖先宽度变化可能要求重新测量兄弟。
`is_dirty` 表示节点的依赖失效标记，不是所有后代根坐标是否发生移动的查询。

子网格采用有限空间测量：先在父提供的空间下求首选尺寸，再用相同宽度、零高度探测高度
下限，最后用零空间探测宽度下限；最小值不超过首选值。它沿用已有宿主参考适配器的
原生策略，并非 CSS 无限空间 intrinsic sizing。最终安排严格使用父分配的对齐后矩形，
可以因子网格 Fixed 轨道而溢出该矩形。子网格自身作为条目的基线默认采用底边。

`TreeLimits` 默认最大 64 层网格、每次最多 100,000 次求解请求（包括缓存命中）；
超限返回 `TreeLimit`。所有根坐标及边缘检查浮点范围。返回根节点在内的深度优先扁平结果。
布局错误只返回错误与计数，不返回部分几何；回调异常传播且不保留部分测量。

计数均表示本次调用：`layout_count` 为实际网格重新计算次数，`measurement_count` 为宿主
叶子回调次数，`cache_hits` 为网格结果复用次数。整树原样复用时 `cache_hit=true`，这些
工作计数都为 0。任意变化后的成功输出仍按深度优先遍历发布全部节点，不能把局部失效
解释为整个结果的生成也只有 O(1) 成本。完整示例见 `examples/retained_grid.cpp`。


## 宿主侧嵌套适配器

[`nested_grid_adapter.hpp`](../examples/nested_grid_adapter.hpp) 是仅依赖公开 API 的参考实现，
不作为核心接口安装。它借用宿主网格描述，以路径区分不同层级的条目 ID。
宿主必须在同步调用期间保持网格、控件和回调 context 有效。

其有限空间探测分别计算 `(W,H)` 的首选尺寸、`(W,0)` 的高度下界和 `(0,0)` 的宽度下界，
再将 minimum 钳制到 preferred 内。高度下界使用同一宽度，避免零宽换行把正常高度撑大。
该策略不求解任意宽高相互依赖的固定点，也不等同于 CSS min/max-content。
最终安排按父级分配的条目矩形重新计算子网格，将坐标转换为根相对值。
子网格的固定轨道可能超出父级矩形，裁剪由宿主处理。

需要库直接管理拓扑和脏标记时，使用上面的 `GridTree`。
完整示例见 [示例指南](examples.md)，构建和安装见 [构建指南](building.md)。
