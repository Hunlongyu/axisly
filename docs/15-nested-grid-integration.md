# 15 — M7 嵌套网格与宿主接入

版本 `0.0.8`。核心保持单网格、同步、无状态的计算模型。嵌套由宿主参考适配器完成；本阶段同时把捕获式 C++ 回调接入公共头文件。

## 文件与使用范围

| 文件 | 职责 |
| --- | --- |
| `include/gridly/layout.hpp` | 已安装的 C++ 回调便利重载 |
| `examples/nested_grid_adapter.hpp` | 只依赖公开 API 的宿主侧嵌套参考实现，不安装为核心 API |
| `examples/nested_grid.cpp` | 三层嵌套和合成换行度量的控制台示例 |
| `tests/nested/nested_tests.cpp` | 回调、递归、坐标、错误传播和最终尺寸重排测试 |

宿主可复制参考适配器，或按自己的 UI 数据结构实现相同流程。核心不引入 UI 控件类型、父子节点指针或持久化 `GridTree`。

## 捕获式回调

```cpp
int calls = 0;
auto result = gridly::compute_grid(spec, items, available,
    [&](gridly::ItemId id, const gridly::MeasureInput& input) -> gridly::MeasureResult {
        ++calls;
        return host.measure(id, input);
    });
```

模板只参与接受 `(ItemId, const MeasureInput&)` 且返回值可转换为 `MeasureResult` 的调用。原三参数静态接口，以及函数指针加 `void*` 的接口保持原样，`nullptr` 仍表示无回调。

便利层同步借用函数对象，不复制、不移动其状态、不保留引用；支持 const 函数对象、普通函数、可变和只可移动的 lambda。成功的单网格计算仍是每条目两次回调，异常原样向上传播。函数对象及其捕获内容的有效性由调用者保证。

## 宿主对象与标识

参考实现的 `NestedGrid` 包含当前网格的 `GridSpec`、条目列表、`ItemId -> const NestedGrid*` 子网格映射，以及普通条目的测量回调和 context。未映射到子网格的条目使用普通回调；普通回调为空时使用 minimum=0 和静态 `preferred_size`。

```cpp
// Include the source-only example adapter header before using these types.
using namespace gridly;
using namespace gridly::examples;

const NestedGrid child{
    .spec = {.columns = {Track::weighted()}, .rows = {Track::content()}},
    .items = {{.id = 0, .preferred_size = {100, 24}}}
};
const NestedGrid root{
    .spec = {.columns = {Track::fixed(200)}, .rows = {Track::content()}},
    .items = {{.id = 0}},
    .children = {{0, &child}}
};
auto result = compute_nested_grid(root, {200, 100});
if (result.succeeded()) {
    // Only now resolve each item.path to a widget and apply item.rect.
}
```

root 中的 ID 0 路径为 `/0`，child 中的 ID 0 路径为 `/0/0`。ID 只需在所属网格内唯一，不能把整个结果放入仅以单个 ID 为键的全局控件表。条目按前序输出：父级容器条目、它的全部后代，再到下一个同级条目；同级保持输入顺序。

宿主拥有所有网格、控件与回调 context。同步调用结束前，不能销毁或移动被引用的网格，也不能在测量回调里修改本次输入、子网格映射或条目存储。子网格指针不转移所有权；同一描述可被多个非祖先路径共享，每个路径单独测量和排版。

## 有限空间测量策略

`compute_grid()` 返回给定有限空间下的占用尺寸，没有单独的全局固有最小／最大内容查询。参考适配器使用以下有限探测策略。一次父级测量子网格时：

| 变量 | 含义 |
| --- | --- |
| W | `known_width.value_or(available.width)` |
| H | `known_height.value_or(available.height)` |
| P | 子网格在 `(W, H)` 下的实际占用，作为 preferred |
| Y | 子网格在 `(W, 0)` 下的实际占用，用于高度下界 |
| X | 子网格在 `(0, 0)` 下的实际占用，用于宽度下界 |

最后返回 `minimum = {min(X.width, P.width), min(Y.height, P.height)}`，`preferred = P`。H=0 时复用 P 为 Y，W=0 时复用 Y 为 X，不做相同空间的重复探测。

高度下界与 preferred 使用相同宽度。不能直接拿零宽度布局的高度作为正常宽度下的 minimum，否则换行文本会把父级高度错误地撑大。`known_width` 为已知零时始终使用零，不回退到 advisory available。

每轴取较小值保证返回合法的 minimum/preferred 顺序；这是参考宿主的有限采样政策。若普通控件的宽度也随 advisory 高度反向变化，采样并不求解所有宽高组合的固定点。任一探测返回非法度量或计算错误都会中止，不能通过取较小值掩盖错误。

Weighted 子网格会随本次可用宽度增长；该 preferred 不是无限空间下的自然宽度。实际 UI 可以根据字体、图片比例、业务最小尺寸等提供更适合的测量策略。参考实现不宣称 CSS 的 min-content/max-content 语义，也不把这个策略写入核心。

## 最终布局与坐标

1. 用递归测量回调计算当前网格。
2. 将当前条目的 `rect` 和 `cell_rect` 加上祖先原点，暂存为根网格坐标。
3. 对每个子网格，以父级条目最终 `rect.width/height` 再计算一次，并以 `rect.x/y` 作为子网格原点。
4. 全部层级成功后，一次性返回完整结果，由宿主应用。

最终排列使用经过 margin、跨格和对齐处理的 `rect`，不能使用 `cell_rect`，也不能直接复用测量探测时的布局。对子网格同样先测宽、再按最终宽度测高；现有单网格回调的 `known_height` 仍未知。

祖先偏移在 double 中累计，输出时转换为 Unit，不做像素取整。子网格固定尺寸可以超出父容器，裁剪由宿主决定。`NestedResult::size` 是根网格本身的占用尺寸，不是所有后代的包围盒；`rect/cell_rect` 都相对根网格原点。

适配器检查翻译后的坐标和完整子网格占用范围能否用有限 Unit 表达。局部计算有效但祖先偏移叠加后超出范围时，返回适配器的 `NumericOverflow`。

## 递归与错误

默认 `NestedLimits`：

| 限制 | 默认值 | 计数方式 |
| --- | --- | --- |
| maximum_depth | 32 | 根网格深度为 1，包含测量递归和最终排版递归 |
| maximum_layouts | 4096 | 每次核心计算，包括测量探测和最终排版 |

进入子网格前检查它是否已在当前祖先栈上，发现自身或间接循环立即返回 `Cycle`。同级共享描述不会误判。循环、深度和预算检测覆盖参考适配器管理的 children 关系；宿主自行在普通回调中创建新的递归调用，仍需自行保证终止。

空指针或映射到不存在条目 ID 的子网格返回 `InvalidChild`。超过深度或预算分别返回 `DepthLimit`、`WorkLimit`；零限制禁止相应的第一次进入／计算。悬空指针不是可检查的输入错误，必须由宿主按生命周期约定避免。

核心失败时返回 `GridFailure`，保留完整 `GridError`，并把局部条目 ID 附加到子网格路径。网格级错误没有局部 ID，路径指向出错网格。所有返回错误均有非空说明、零 size 和空 items，包括最终排版到一半才失败的情况。

用户回调异常和分配异常原样传播；局部栈通过 RAII 回退，下一次调用重新建立独立状态。适配器不会调用控件更新方法，失败前的探测不会写入控件几何。测量本身应避免宿主可见副作用。

`layout_count` 记录实际核心计算次数，`measurement_count` 记录整个递归过程实际调用适配器回调的次数，包括用于测量子网格的调用；错误时保留已经发生的计数。这些总计不同于某一次 `GridResult` 的局部计数。

参考适配器没有跨调用缓存，多层探测会增加工作量。默认预算保证失败可诊断，但合法的复杂树也可能先达到预算。性能优化与 retained tree 属于 M10；本阶段不把递归探测次数描述为恒定或线性。

## 示例和验证

`gridly_nested_example` 包含固定侧栏、面板标题和三级文字条目。合成文字以 600 单位首选宽度、20 单位行高估算换行，未加载真实字体。

| 容器宽度 | 根网格占用 | 最内层文字矩形 | 核心计算／测量次数 |
| --- | --- | --- | --- |
| 640 | 640 × 122 | `{136, 58, 480, 40}` | 39 / 94 |
| 360 | 360 × 142 | `{136, 58, 200, 60}` | 39 / 94 |
| 220 | 220 × 282 | `{136, 58, 60, 200}` | 39 / 94 |

Windows 独立启动时保留结果，按回车关闭；已有终端或重定向运行正常结束。

2026-09-24，Windows x64，MSVC 19.51.36257，CMake 4.4.2，Ninja：

| 验证项 | 结果 |
| --- | --- |
| Debug 构建与测试 | 无编译告警，205/205 通过 |
| Release 构建与测试 | 无编译告警，205/205 通过 |
| M7 新增测试 | 34 项，覆盖回调兼容、嵌套换行／跨度／范围、路径、循环、深度、预算、异常恢复和两类坐标溢出 |
| 随机最终布局对照 | 250 组，在实际父级分配下与独立子网格计算对照矩形、单元格、重复执行结果及计数 |
| M0～M6 回归 | 原 171 项继续通过，已有数值／随机对照仍保留 |
| 公共头文件 | 7 个独立编译，Debug／Release 均通过 |
| 安装包／子目录接入 | 分别 1/1 通过，实际调用新的捕获式测量重载 |
| 七个 Release 示例独立启动 | 隐藏控制台读取结果与回车提示，输入回车后均以 0 退出 |

上述为 M7 阶段的验证记录。后续已完成 [M8 开发期共同子集对照](16-reference-validation.md)；真实字体／UI、SDL3 图形运行和跨平台执行仍未完成。
