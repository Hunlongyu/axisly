# 0.1.0 API 契约与接入

生产库仅依赖 C++20/STL，统一入口为 `<gridly/gridly.hpp>`。各头文件可以独立包含。
版本号和 CMake 包保持一致；`find_package(gridly CONFIG REQUIRED)` 与
`add_subdirectory` 都链接 `gridly::gridly`。本文件定义本次稳定下来的接口和语义，
原始 API 提案、早期里程碑和性能报告保留历史版本信息。

## 兼容与所有权

- 无状态 `compute_grid` 的旧重载、默认显式放置和浮点输出保持不变；成功且有回调时，
  仍按输入顺序执行一次初始测量，再执行一次最终宽度下的高度测量，共 2N 次。
- 已有枚举值保留顺序，新值追加。新增 aggregate 字段放在末尾，旧的指定初始化代码仍有效。
  新增字段改变结构体布局，必须重新编译库和消费者；没有跨版本二进制 ABI 承诺。
- `0.x` 为开发版本。后续不兼容变化须明确列入变更记录并更新版本，已有合法输入的语义
  修改必须附回归用例。正式 1.0 发布仍需要真实宿主验证；功能完成不等同于发布。
- 原始计算同步借用输入和回调。`GridCache`／`GridTree` 拥有输入快照和缓存，回调 context
  仍由宿主拥有。它们不可复制、移动或并发访问；回调不能修改或递归计算同一实例。
  可计算另一实例。借用结果在下一次计算、修改或销毁前消费，跨帧保存时显式复制。
- 核心非法数据返回错误码和空几何；数值溢出返回 `NumericOverflow`。回调异常原样传播。
  Track 工厂非法参数抛 `invalid_argument`；树中失效／跨树句柄抛 `out_of_range`，
  非法类型或环抛 `invalid_argument`；回调重入／修改抛 `logic_error`。

## 自动放置与隐式轨道

```cpp
gridly::GridSpec spec{
    .columns = {gridly::Track::weighted(), gridly::Track::weighted()},
    .rows = {gridly::Track::content()},
    .column_gap = 8,
    .row_gap = 8,
    .auto_flow = gridly::AutoFlow::RowDense,
    .allow_implicit_tracks = true
};
gridly::GridItem item{
    .id = 7,
    .column = gridly::auto_position,
    .row = gridly::auto_position,
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
auto result = gridly::round_layout(
    gridly::compute_grid(spec, items, available),
    gridly::PixelRounding{.scale = 1.25f, .origin_x = 0, .origin_y = 0});
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
gridly::GridCache cache(spec, items);
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
gridly::GridTree tree;
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

## Sanitizer

`GRIDLY_ENABLE_SANITIZERS=ON` 在 Windows 启用 ASan，在 Linux GCC／Clang 启用 ASan 和
UBSan。Windows 开发脚本发现 Visual Studio 预览工具链，Clang 模式发现自身 runtime
目录，明确链接 ASan runtime，并临时配置 DLL 搜索路径；支持 x64 动态 CRT。
Clang 直接调用链接器时需要显式提供运行库，见
[Clang 官方说明](https://clang.llvm.org/docs/UsersManual.html#finding-clang-runtime-libraries)。
CI 配置分别运行 GCC／Clang ASan+UBSan；本机验证与远端 CI 执行状态分开记录。
