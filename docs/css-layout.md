# CSS 布局指南

`axisly::css::LayoutTree` 保存节点、样式、父子关系和缓存。每次计算指定一个根和可用空间，
默认同步返回完整布局结果，也可保留布局并按节点读取。快速示例见 [README](../README.md#快速开始)，接口定义见
[`css_tree.hpp`](../include/axisly/css_tree.hpp)。

## 创建和更新布局树

- `create_node(style, measure)` 创建节点，测量回调可省略；默认 `Style` 使用 Flex 布局。
- `append_child`／`insert_child` 建立父子关系，也可用于换父级或重新排序。
- `create_node_with_children` 创建带子节点的容器；`set_children` 整体更新子节点。
- `replace_child` 按索引替换；`detach_child_at`、`detach_children` 按索引或范围分离。
- `detach` 保留子树；`remove_node` 只删除节点并将其子节点变为根。
- `remove_subtree` 删除节点及其全部后代。
- `node_count`、`child_count`、`child_at`、`dirty` 提供查询，`clear` 清空节点。
- `set_style` 更新样式；`set_measure` 更新测量回调；外部内容改变时调用 `mark_dirty`。

`NodeId` 标识库中的节点，宿主自行维护它到控件的映射。失效句柄、跨树句柄和循环拓扑会被拒绝。
一次计算可从任意有效节点开始，输出仅包含该根的子树。
`style()`、`context()` 返回借用引用，`children()` 返回借用 span；跨树修改保留数据时应先复制，
不要在节点删除后继续使用引用，也不要在父子关系修改后继续使用旧 span。

字号、文字、图片、DPI 或其他测量依赖改变后，即使回调对象没有变化，也需要主动标脏。
`mark_dirty(node)` 只失效该节点和祖先；整个容器内容变化应使用 `mark_subtree_dirty(node)`。
无关分支可以在相同约束下复用缓存；一个叶子变化仍可能使祖先和兄弟节点重新分配尺寸。
树操作先完成参数检查和可能失败的内存准备，再提交父子关系；缓存失效与删除不分配工作列表。
操作因内存分配失败而抛出异常时，原有拓扑仍然有效。

## 类型化样式

```cpp
namespace css = axisly::css;
css::Style style;
style.display = css::Display::Flex;
style.flex_direction = css::FlexDirection::Column;
style.gap.height = css::Length::length(8);
style.box.size.width = css::Length::percent(0.5F); // 50%，不是 50
style.box.min_size.width = css::Length::length(120);
```

样式分为通用 `Style`、盒模型 `BoxStyle` 和轨道／放置 `GridStyle`。
长度值区分确定长度、百分比、auto、固有尺寸关键字及类型化计算表达式。
完整字段对应关系见 [兼容性说明](css-compatibility.md)。

`Unit` 为 `float`，逻辑坐标的单位由宿主约定；`Insets` 顺序为左、上、右、下。
已知零尺寸与未定尺寸有不同含义，不能把零当成缺省值。
可用空间通过 `AvailableSpace::definite()`、`indefinite()`、`min_content()` 和
`max_content()` 表达，不通过无穷或 NaN 表示。

## 内容测量

测量叶子在 `create_node` 或 `set_measure` 中接收 `MeasureFunction`。
回调返回内容盒尺寸及可选的首／末基线，不包含 padding、border 和 margin。
下面是一个固定尺寸图标的测量回调：

```cpp
auto icon = tree.create_node({}, [](const css::MeasureRequest& request) {
    return css::Measurement{{
        request.known_content_width.value_or(24),
        request.known_content_height.value_or(24)}};
});
```

文字宿主应根据请求执行自己的文字排版：

| 请求字段 | 含义 |
| --- | --- |
| `known_content_width`／`known_content_height` | 已确定的内容尺寸；有值时优先于回调该轴的返回值，零也是有效约束 |
| `available_width`／`available_height` | 确定的可用空间、未定空间或 min/max-content 查询 |
| `Measurement::content_size` | 本次约束下的内容尺寸，必须有限且非负 |
| `first_baseline`／`last_baseline` | 相对内容盒顶部的可选基线 |
| `scrollable_content_size` | 实际可滚动内容尺寸；省略时使用 `content_size` |

回调可能在同一帧被不同约束多次调用，也可能因缓存而完全不调用。
它应当根据请求和当前内容返回稳定结果，不应依赖调用顺序。
库不加载字体或进行文字塑形；换行、字体度量、图标和图片尺寸由宿主提供。

### 共享测量与节点上下文

少量独立控件可直接用节点级 `MeasureFunction`。宿主已有统一文字／图片测量服务，
或希望减少每节点捕获对象时，推荐共享 `MeasureProvider` 加节点上下文。

`set_context(node, shared_ptr<void>)` 保存宿主上下文，`context(node)` 读取它。
实现 `MeasureProvider::measure(node, style, context, request)` 后，通过
`LayoutOptions::measure_provider` 为一次计算提供共享测量。节点自己的 `MeasureFunction` 优先。
提供者用于所有需要内容测量的叶子，上下文可以为空。提供者对象在计算期间必须有效。

输入内容变化仍需标脏对应节点；共享测量策略整体改变时递增 `measurement_revision`。
更换提供者地址或 revision 会失效全树布局缓存，普通连续帧保持它们不变即可复用缓存。

## 结果与坐标

```cpp
const auto& result = tree.compute(root, available);
if (result.succeeded()) {
    if (const auto* node = tree.find_layout(icon)) {
        const auto bounds = node->border_box;
        // 应用 bounds 到宿主；嵌套控件需要按父控件坐标系转换。
    }
}
```

- `nodes` 按根优先、深度优先顺序保存完整几何；`border_box` 和 `content_box` 相对布局根。
- `margin`、`padding`、`border`、`scrollbar` 和基线分别报告。
- `scrollable_overflow_rect` 是节点局部滚动范围；裁剪、滚动偏移与滚动交互由宿主处理。
- `grid_tracks` 包含 Grid 轨道尺寸、位置、线名和项目放置等诊断数据。
- `LayoutResult::find` 做线性查找；频繁按 ID 读取时可使用树上的 `find_layout` 惰性索引。
- `layout(node, CoordinateSpace::Parent)` 返回相对父节点的几何值，无需从大坐标相减。
- `unrounded_layout(node)` 可在取整后继续读取原始逻辑坐标。

结果由树持有。树修改本身保留上一份已发布快照，下一次计算会更新它。
节点指针在重新计算或树销毁后不能继续使用；需要同时保留多帧时复制 `LayoutResult`。
`layout` 返回值副本，其生命周期独立于后续计算；`find_layout` 返回借用指针。

```cpp
css::LayoutOptions options;
options.publish_nodes = false;       // 保留内部布局，省去完整数组发布
options.collect_grid_tracks = false; // 不生成 Grid 诊断信息
const auto& status = tree.compute(root, available, options);
if (status.succeeded()) {
    if (auto bounds = tree.layout(icon, css::CoordinateSpace::Parent)) {
        // 应用 bounds->border_box 到嵌套控件。
    }
}
```

这时 `nodes` 和 `grid_tracks` 为空，数值错误检查仍会执行。`layout()` 按树路径查找节点，
首次访问宽分支时建立可复用的子节点索引；
大量节点遍历宜选择默认完整快照。调用 `find_layout()` 会惰性生成完整数组和索引。
更改诊断开关会失效诊断相关缓存，建议在稳定的使用模式中保持该选项不变。

## 接入宿主已有的树

[`css_source.hpp`](../include/axisly/css_source.hpp) 提供 `LayoutSource` 和 `LayoutSession`。
宿主实现 `node(NodeId)`，返回借用的 `Style`、子节点 span、父节点和上下文指针。
`LayoutSession` 直接借用它们进行计算，仅持有算法缓存和输出，不复制宿主样式及子节点数组。

```cpp
// host_tree 实现 css::LayoutSource；它的生命周期长于 session。
css::LayoutSession session(host_tree);
const auto& result = session.compute(host_root, available);
auto bounds = session.layout(host_child, css::CoordinateSpace::Parent);
```

- 宿主提供非零、唯一的 NodeId，并维持无环、一致的父子关系。
- 样式、子节点数组、上下文在计算期间不能改变或失效。
- 修改样式或内容后调用 `session.mark_dirty(node)`；修改拓扑、删除节点或复用 ID 后调用 `session.invalidate()`。
- `invalidate()` 丢弃节点绑定和缓存，上一份已发布几何仍可读取，直到下一次计算。
- 会话可通过 `LayoutOptions` 使用共享测量、取整、按需结果和诊断开关。
- 完整发布后的根坐标值查询使用惰性的宿主 ID 排序索引，避免逐节点读取时反复扫描全数组。
- 同一会话需要串行访问；回调不能重入或使该会话失效。

宿主已有布局管理器时，应让它与 Axisly 分别管理不同的控件范围，避免多方重复设置同一控件的矩形。

## 取整、内存与线程

`compute(root, available, axisly::PixelRounding{scale})` 可发布设备像素对齐的几何，
内部缓存保留逻辑坐标。跨临时树可共享 `LayoutMemory`，多棵独立树可交给 `LayoutBatch`。
具体生命周期与线程约定见 [执行资源](css-execution-resources.md)。

单棵树的计算、修改和查询需要串行访问或由宿主同步。测量回调不能修改或重入计算同一棵树。
默认批量模式将回调留在调用线程；显式并行前，宿主需保证回调和所用资源适合多线程访问。

## 错误与预算

样式工厂和树操作的非法参数通过异常报告。计算失败时检查 `result.error` 中的错误码、节点与消息；
失败结果为空，不返回部分几何。宿主回调异常原样传播，保留上一份完整发布结果。

入口检查和布局检查的边界如下。`validate_style` 检查类型化样式的属性域，不解析特定容器下的最终数值，
也不能保证随后 `compute` 成功；`set_style` 自身会完成同样的检查，无需为了提交而重复校验。
重复提交与节点当前值相同的样式时直接返回，不复制样式、不重新校验、不使缓存失效。
实际变更使用左值时先完成校验和副本准备，使用右值时先校验再移动；校验或副本分配失败
会保留旧样式和缓存。所有提交仍须遵守节点句柄有效性及计算期间禁止修改的约定。

| 阶段 | 条件举例 | 报告方式 |
| --- | --- | --- |
| 值工厂 | NaN／无穷长度、零 Grid 线号、非法 span | `std::invalid_argument`，对象尚未生成 |
| `validate_style`、`create_node`、`set_style` | 非法枚举、负 flex 因子、非正宽高比、Grid 区域不成矩形、非法模板组合 | `std::invalid_argument`，不提交非法样式 |
| 树修改和查询 | 无效句柄、循环父子关系、越界子索引、计算中修改同一棵树 | `std::invalid_argument`、`std::out_of_range` 或 `std::logic_error` |
| `compute` 的参数 | 非法确定空间、取整比例或原点 | `std::invalid_argument`，保留上一份发布结果 |
| 布局过程中 | 依赖容器的值、借用宿主样式、测量返回值、计算预算 | 下表的 `LayoutErrorCode`，发布空的失败结果 |
| 节点／共享测量器抛出异常 | 包括 `invalid_argument`、`overflow_error`、`length_error`、`bad_alloc` 及非标准异常 | 保留类型和值并向调用方传播，保留上一份完整发布结果 |
| 库自身的内存分配失败 | 样式复制、缓存、结果存储分配失败 | `std::bad_alloc`；不会伪装成非法样式或空布局 |

| `LayoutErrorCode` | 含义与边界 |
| --- | --- |
| `None` | 计算成功 |
| `InvalidRoot` | 请求的根或遍历时需要的节点不存在；借用树应检查宿主拓扑 |
| `InvalidStyle` | 解析后的 gap 为负、盒模型参数不合法，或借用树的样式在计算时未通过校验；属性域合法不代表每个容器尺寸下都合法 |
| `Unsupported` | 保留错误码，当前算法没有产生此码的路径；不支持的浏览器功能不能用这个枚举自动检测 |
| `UnresolvedPercentage` | 算法需要确定盒边时仍缺少百分比基准；未定百分比并非一律报错，许多布局路径按各自规则延后或按零处理 |
| `InvalidMeasurement` | 回调正常返回，但尺寸／基线等未通过测量值校验；与回调抛出异常不同 |
| `NumericOverflow` | 库内计算或几何发布超出有限 `Unit` 范围 |
| `LimitExceeded` | 布局深度、测量次数、轨道／放置／均衡换行工作量等超过上限；建树节点数超限在入口抛 `std::length_error` |

当前库要求启用 C++ 异常，不支持 `-fno-exceptions`。尚无 `check_style`／`try_set_style` 的无异常契约；
来自配置文件的值应由宿主在输入边界捕获验证异常。仅增加 `noexcept` 包装不足以消除值工厂、
样式复制及宿主回调的异常，不能据此宣称整个“校验、提交、计算”流程无异常。

`TreeLimits` 默认限制最大深度为 64，树节点数、每次计算的测量次数及两类缓存条目预算各为 100,000。
可在构造树时按业务规模调整。预算用于限制工作量，超限不应被当成正常空布局。
两类缓存条目预算控制缓存保留量；达到缓存上限可以继续计算，不等于布局工作量超限。

## 接口迁移

此前在容器上调用 `mark_dirty` 会连同全部后代失效。依赖此行为的调用应改为
`mark_subtree_dirty`；单节点内容变化继续使用 `mark_dirty`。
`NodeLayout` 中原有的 `overflow_origin_border` 和 `overflow_coordinate_width`
已移入私有算法状态；宿主应使用公开的 `border` 和 `scrollable_overflow_rect`。
默认不取整、根相对坐标保持原有约定。`remove` 已删除，原调用改为
`remove_subtree`，递归删除行为不变。
`compute` 的第三个参数应明确使用 `PixelRounding{...}` 或 `LayoutOptions{...}`，避免裸 `{}` 的重载歧义。
