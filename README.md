# Axisly

面向原生 UI 的 C++20 通用布局库。核心只依赖 STL，通过数据和测量回调
与宿主 UI 对接，不持有控件，不依赖 Qt、SDL 或 Win32。

## 当前进度：独立 CSS 布局管线与原生 Grid

当前版本为 `0.1.0`。`compute_grid()` 支持固定／权重／Content／Range／百分比轨道、显式与自动放置、隐式轨道、跨格、padding／gap／margin、两轴独立对齐与首基线对齐。列宽确定后重新测量高度；`round_layout()` 可按设备像素取整。默认仍是显式放置、浮点坐标和无状态计算。

可选 `GridCache` 复用完整布局和工作区，可进一步启用条目测量缓存；`MeasurementCache` 也可独立使用。可选 `GridTree` 管理节点、父子关系、嵌套布局和脏标记传播，修改叶子时可复用无关分支。上述入口保持原生 Grid 语义，历史 [功能与验收记录](docs/22-roadmap-completion.md) 和 [API 契约](docs/23-api-contract.md) 仍可查阅。

新增 `axisly::css::LayoutTree` 使用独立 CSS 布局管线，支持 Block／FlowRoot、
浮动与 clear、Flexbox（含 balance）、CSS Grid，以及混合树的尺寸、盒模型、
基线、定位、方向、溢出、contain 与类型化 calc。Taffy 0.14 的 43 个布局字段
已有实现与验收；固定版本的全部 6020 个上游输入通过几何及轨道诊断对照。
[详细字段矩阵](docs/30-taffy-014-capability-matrix.md)、
[验收计划](docs/29-taffy-014-layout-parity-plan.md)及
[规范差异](docs/32-css-normative-case-index.md)说明版本兼容结果和验证边界。
生产库接收 C++ 类型，不解析 CSS 字符串，也不提供样式表、DOM、文本塑形或绘制。
完整 CSS 规范的长期目标仍在 [Flexbox 规范矩阵](docs/28-flexbox-conformance-matrix.md)
中单独追踪。项目尚未发布，也未承诺跨版本二进制兼容。

SDL3 演示保留十个可交互场景和等宽字体。Taffy 对照、SDL3 演示和性能工具仍为可选开发依赖，不进入生产库。历史性能记录见 [缓存接口](docs/20-layout-cache.md) 和 [工作区优化](docs/21-workspace-optimization.md)。

后续已修复真实 Windows Debug CI 分配问题，新增 913 个高级功能 Taffy 对照及
`GridTree` 局部失效／真实字体 benchmark，见 [扩展验收记录](docs/24-ci-and-extended-validation.md)。
真实宿主现已接入 [GeeyoouUI 的完整 19 页 Showcase](docs/26-geeyoou-showcase.md)，
包含 axisly 页面布局、真实文字测量、动态控件及自动窗口验收。

### CSS 布局入口

```cpp
#include <axisly/css_tree.hpp>
namespace css = axisly::css;

css::LayoutTree tree;
css::Style row;
row.display = css::Display::Flex;
row.gap.width = css::Length::length(8);
const auto root = tree.create_node(row);

css::Style child;
child.display = css::Display::Block;
const auto leaf = tree.create_node(child, [](const css::MeasureRequest&) {
    // 实际应用根据已知内容尺寸和 min/max-content 请求测量文字或控件。
    return css::Measurement{{80, 24}, 18, 18};
});
tree.append_child(root, leaf);
const auto& result = tree.compute(root, {
    css::AvailableSpace::definite(320), css::AvailableSpace::definite(100)});
if (result.succeeded()) {
    const auto bounds = result.find(leaf)->border_box;
    // 将根相对 bounds 应用到宿主；像素取整使用宿主的实际 DPI。
}
```

结果由树持有，后续计算会更新它；内容变化时调用 `mark_dirty(leaf)`，
样式变化使用 `set_style()`。测量请求区分未知、已知零、有限可用空间及
min/max-content，宿主可以报告基线与独立的可滚动内容范围。

### 从旧名称迁移

此前的私有开发名称为 `gridly`。源码接入改用 `<axisly/axisly.hpp>` 和
`axisly::`；CMake 改用 `find_package(axisly CONFIG REQUIRED)` 与
`axisly::axisly`；构建选项前缀改为 `AXISLY_`。旧名称没有保留公开别名，
因此已接入旧名称的宿主需要同步修改并重新构建。历史基准和 CI 记录中的
`gridly` 表示改名前的实测版本。

## 构建与测试

需要支持 C++20 的编译器、CMake 3.25+；预设另需 Ninja。默认构建不下载依赖。

Windows 推荐用 PowerShell 脚本发现 Visual Studio（含预览版本），初始化 MSVC x64，完成构建与测试：

```powershell
./scripts/build-msvc.ps1 -Preset release
```

工程会探测 MSVC 的实际头文件依赖前缀，避免中文诊断编码不匹配导致 Ninja 漏编译。

Windows 使用已初始化 MSVC 环境的终端。可在 `cmd` 中先执行：

```bat
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars64.bat"
```

随后在项目根目录执行；Linux/macOS 可在编译器可用的终端直接执行：

```text
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

Release 将上述 `debug` 替换为 `release`。Windows 示例：

- `build/debug/axisly_api_example.exe`：输出 3×2 固定网格的实际矩形。
- `build/debug/axisly_weighted_example.exe`：展示固定＋权重混合网格在三个容器宽度下的矩形。
- `build/debug/axisly_alignment_example.exe`：对照四种对齐的原始单元格和最终条目矩形。
- `build/debug/axisly_measurement_example.exe`：展示 Content 收缩、宽度变化后的高度测量和调用次数。
- `build/debug/axisly_spanning_example.exe`：展示跨三列标题、跨两行侧栏，以及容器变窄后的重新测量。
- `build/debug/axisly_range_example.exe`：展示固定上限、内容最小值、权重增长以及容器不足时的布局。
- `build/debug/axisly_nested_example.exe`：展示三层嵌套、容器变窄后的换行、完整 ID 路径和根坐标。
- `build/debug/axisly_retained_example.exe`：展示自动放置、隐式行、百分比列、首基线、DPI 取整、整树命中和单条目失效。

这些是控制台示例，不是图形窗口。Windows 双击启动并独占交互式控制台时，结果会保留到按回车关闭；在已有终端内运行或重定向输出时，打印后正常结束。先前版本打印完成便退出，双击时表现为窗口立即关闭。

测试检查不会被 Release 的 `NDEBUG` 禁用。

Windows x64 可使用本机 LLVM 运行 AddressSanitizer：`./scripts/build-sanitizers.ps1`；
MSVC 使用 `-Compiler MSVC`。Linux GCC／Clang 使用 `-DAXISLY_ENABLE_SANITIZERS=ON`，
同时启用 ASan／UBSan。这些是开发构建，发布库保持该选项关闭。

不使用 Ninja 时可使用本机默认生成器：

```text
cmake -S . -B build/default -DCMAKE_BUILD_TYPE=Debug
cmake --build build/default --config Debug
ctest --test-dir build/default -C Debug --output-on-failure
```

## 可选的 GeeyoouUI 完整演示（Windows）

复用 GeeyoouUI 的控件与交互，由 axisly 计算页面和容器布局。默认关闭，首次启用需要
Python 3.10+、Git 和网络，下载锁定版本的 GeeyoouUI、Blend2D、AsmJit。

```powershell
./scripts/build-msvc.ps1 -Preset geeyoou-release
./build/geeyoou-release/bin/axisly_geeyoou_showcase.exe
```

19 个页面包含概览、表单、七类表格、实时 HMI、主题切换、弹窗和 3D 视图。
布局页增加百分比、自动放置、隐式行、跨格、基线与动态增删；侧栏支持滚动。
固定工艺画面保留最小画布与控件内部坐标。测试在隐藏的真实 Win32 窗口中遍历全部页面，
保存截图和验收数据。构建、适配边界与验证结果见 [宿主演示说明](docs/26-geeyoou-showcase.md)。

## 可选的 Taffy 对照验证

普通 `debug`／`release` 预设保持 C++／STL 构建。开发期对照另外需要 Python 3.10+ 和 Rust/Cargo，首次启用时 Cargo 会下载锁定的参考依赖：

```text
cmake --preset reference-release
cmake --build --preset reference-release
ctest --preset reference-release
```

Windows 同样先初始化 MSVC 环境。Debug 使用 `reference-debug`。对照固定使用 Taffy 0.14.0、关闭像素取整；默认运行 30 个固定用例和 750 个随机用例，比较实际占用、条目矩形和完整跨格区域。失败时保存 JSON 输入及两边结果，可用 `--replay` 单独重放。

共同子集包含固定布局、带数值下界的权重分配、显式跨度、可容纳条目的对齐／margin，以及空间充足时的 Content 测量。Content 收缩、含 Content 的跨度及其他存在语义差异的组合会明确拒绝，不计入对照成功数。规则、误差和重放命令见 [M8 对照验证](docs/16-reference-validation.md)。

## 可选的性能基准

基准只依赖 C++20／STL，启用 `AXISLY_BUILD_BENCHMARKS` 或使用专用预设：

```powershell
./scripts/build-msvc.ps1 -Preset benchmark-release
./build/benchmark-release/axisly_bench.exe --output build/benchmark-release/timing.csv
./build/benchmark-release/axisly_bench_allocations.exe --output build/benchmark-release/allocations.csv
```

其他平台用 `cmake --preset benchmark-release`、对应 build/test 预设，再运行两个无 `.exe` 后缀的程序。`benchmark-debug` 用于正确性检查，不作为性能结论。

覆盖 3×3 固定网格、20／31／50／100／316 阶权重网格、乱序 ID、连续缩放、Content、Range、密集跨格和宿主嵌套。计时程序使用普通分配器；分配程序单独统计，不混用其耗时。Taffy 原生 bench、计时范围、CSV 含义及本机结果见 [M10 性能基准](docs/18-performance-benchmarks.md)。

另有独立的 `comparison-release` 预设，对相同输入比较无缓存 axisly、`GridCache`、Taffy 0.14.0 和 Yoga 3.2.1。它需要 Rust/Cargo、Python，首次构建下载锁定的 Yoga 源码。先校验全部矩形，再分别测新树首次布局、复用树连续缩放、输入不变的重复调用；Yoga 仅参与能映射为单轴 Flexbox 的场景。历史基线见 [同场景性能对比](docs/19-comparative-benchmarks.md)，缓存接口见 [完整布局缓存](docs/20-layout-cache.md)，工作区优化记录见 [0.0.13 复测](docs/21-workspace-optimization.md)，最新数据见 [0.1.0 完成记录](docs/22-roadmap-completion.md)。比较工具同样默认关闭，不安装或链接进生产核心。

## 接入

图形程序的构建与操作见下文及 [M9 说明](docs/17-sdl3-playground.md)。

作为子目录引入，测试和示例默认关闭：

```cmake
add_subdirectory(path/to/axisly)
target_link_libraries(your_app PRIVATE axisly::axisly)
```

也可以安装后用 CMake 包接入：

```text
cmake --install build/release --prefix build/install
```

消费工程将安装前缀加入 `CMAKE_PREFIX_PATH`，然后：

```cmake
find_package(axisly CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE axisly::axisly)
```

公共入口为 `<axisly/axisly.hpp>`，命名空间为 `axisly`：

```cpp
#include <axisly/axisly.hpp>
#include <array>

axisly::GridSpec spec;
spec.columns = {
    axisly::Track::fixed(180),
    axisly::Track::weighted(1),
    axisly::Track::weighted(2)
};
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
    // result.size == Size{640, 360}
    // result.items[0].rect == Rect{200, 12, 140, 80}
    // result.items[1].rect == Rect{348, 100, 280, 248}
}
```

`available` 表示宿主提供的外部空间。固定轨道保留声明尺寸；扣除 padding、gap、固定尺寸和 Content 占用后，权重轨道按比例分配剩余空间。结果 `size` 是轨道、间隙和 padding 的实际占用范围，可以小于或大于 `available`。

数值约束示例：

```cpp
auto proportional = axisly::Track::weighted(2);                // 权重 2，无额外上下限
auto minimum = axisly::Track::weighted(1, 120);                // 最少 120
auto bounded = axisly::Track::weighted(1, 80, 240.0f);         // 80..240
```

最小值是比例分配结果的下限，不是在最小值上再加一份比例空间。空间不足时保留最小值，所有轨道达到上限后留白。详情见 [M2 行为约定](docs/10-weighted-layout.md)。

对齐与边距示例：

```cpp
axisly::GridItem label{
    .id = 3,
    .margin = {8, 4, 8, 4},
    .horizontal_alignment = axisly::HorizontalAlignment::Center,
    .vertical_alignment = axisly::VerticalAlignment::End,
    .preferred_size = {60, 24}
};
```

先从单元格扣除 margin。Stretch 填满剩余空间；其他对齐使用不超过剩余空间的首选尺寸。`preferred_size` 默认为零，非 Stretch 的条目需提供静态尺寸或测量回调。过大边距使可用尺寸归零，条目仍留在单元格内。固定轨道不会被条目尺寸撑大；Content 考虑条目尺寸和边距，跨格最小尺寸还可在上限内提高权重轨道的计算下限。`cell_rect` 保存完成轨道分配、尚未应用条目对齐时的完整区域。详情见 [M3 行为约定](docs/11-alignment-and-margin.md) 和 [M5 跨格规则](docs/13-spanning-layout.md)。

## 测量与 Content

```cpp
auto measure = +[](axisly::ItemId id, const axisly::MeasureInput& input,
                   void* user_data) -> axisly::MeasureResult {
    // 用 id 从宿主找到控件，并根据 input.known_width 测量换行后的高度。
    // 示例使用固定的模拟尺寸；完整换行示例见 examples/measurement_grid.cpp。
    (void)id;
    (void)input;
    (void)user_data;
    return {{40, 20}, {120, 30}};
};
axisly::GridSpec measured_spec{
    .columns = {axisly::Track::content()},
    .rows = {axisly::Track::content()}
};
const std::array measured_items{axisly::GridItem{.id = 7}};
auto measured = axisly::compute_grid(measured_spec, measured_items, {100, 100}, measure);
// measured.size == Size{100, 30}; measured.measurement_count == 2
```

Content 的最小／首选尺寸取该轨道所有条目尺寸加 margin 后的最大值。先预留固定尺寸、padding、gap 和权重轨道的数值最小值，再满足 Content 首选尺寸；空间不足时按各轨道可收缩量同比收缩，保留最小尺寸，允许网格超出容器。没有条目的 Content 轨道为零。不传回调时，条目的 minimum 为零、preferred 为静态 `preferred_size`。

有回调的成功布局中，每个条目测量两次：第一次获取固有尺寸；第二次传入最终条目宽度，再确定行高。跨列内部 gap、横向非 Stretch 对齐、margin 和尺寸裁剪都会计入这个宽度。第二次返回的宽度不再影响列布局，`known_height` 保持未知。既有 Fixed／Weighted／Content 单格行为保持 M4 约定；跨度和 Range 按 M5／M6 规则贡献尺寸，固定尺寸和数值上限仍具有优先级。

完整约定及验证记录见 [M4 测量与 Content](docs/12-measurement-and-content.md)。

## 跨行与跨列

```cpp
axisly::GridSpec spanning_spec{
    .columns = {axisly::Track::fixed(80), axisly::Track::content()},
    .rows = {axisly::Track::fixed(20), axisly::Track::fixed(30)},
    .column_gap = 10,
    .row_gap = 5
};
const std::array spanning_items{
    axisly::GridItem{.id = 1, .column_span = 2, .row_span = 2, .preferred_size = {200, 40}}
};
auto spanning = axisly::compute_grid(spanning_spec, spanning_items, {300, 100});
// 两列为 80、110；cell_rect 和默认 Stretch 的 rect 均为 {0, 0, 200, 55}。
```

span 必须为正且落在显式网格内。完整区域包括所跨轨道及内部 gap，margin 只扣除一次。条目可重叠，输出顺序保持不变。

尺寸贡献按短跨度到长跨度处理，同一跨度使用共同快照、按每条轨道需要的最大增量合并。缺口优先由 Content 和按内容增长的 Range 承担；它们达到上限后，剩余最小尺寸缺口按权重补足，继续冻结和重新分配。首选尺寸不会成为权重轨道的硬下限。固定尺寸或数值上限使要求无法满足时，仍按实际区域裁剪条目。详见 [M5 跨格布局](docs/13-spanning-layout.md) 和 [M6 Range 约定](docs/14-range-layout.md)。

## Range 轨道

```cpp
auto bounded_content = axisly::Track::range(axisly::Track::fixed(80), axisly::Track::fixed(160));
auto flexible = axisly::Track::range(axisly::Track::fixed(100), axisly::Track::weighted(2));
auto measured_minimum = axisly::Track::range(axisly::Track::content(), axisly::Track::weighted());
auto capped_content = axisly::Track::range(axisly::Track::content(), axisly::Track::fixed(240));
```

Fixed 下界提供数值最小值；Content 下界采用单格测量 minimum 加 margin 的最大贡献。Fixed 上界限制按内容增长的尺寸，Content 上界以测量 preferred 为目标，Weighted 上界按权重分配容器空间。

例如 `range(fixed(80), fixed(160))` 的首选宽度为 120 时占用 120，为 240 时占用 160，没有内容时占用 80。它不会自动填满到上限。`range(fixed(100), weighted(2))` 则与 `weighted(2, 100)` 等价。

固定下界不会被更小的内容首选值降低，也不会因更大的单格内容 minimum 自动提高；需要动态最小值时使用 Content 下界。固定上限可裁剪更大的内容 minimum。跨格 minimum 仍可按 M5 规则在上限内提高计算下限。完整六种组合、冲突处理与验证见 [M6 Range 轨道](docs/14-range-layout.md)。

## Lambda 与嵌套接入

捕获式回调可直接传入，原来的函数指针加 `void*` 接口继续可用：

```cpp
int calls = 0;
auto result = axisly::compute_grid(spec, items, {640, 360},
    [&](axisly::ItemId, const axisly::MeasureInput&) -> axisly::MeasureResult {
        ++calls;
        return {{40, 20}, {120, 30}};
    });
```

回调同步借用，不复制或保存，支持只可移动的函数对象。异常原样传播，成功时仍为每条目两次测量。

嵌套示例位于 [`examples/nested_grid_adapter.hpp`](examples/nested_grid_adapter.hpp)，由宿主持有网格、条目和子网格指针。适配器递归测量子网格，父级确定最终对齐矩形后，再用该矩形的尺寸排列子网格。返回的条目通过 `/2/2/1` 这样的完整路径区分，坐标统一相对根网格；不同子网格可复用相同 ID，也可共享子网格描述。

适配器检测祖先循环，默认限制深度 32、单次核心计算次数 4096。失败时不返回部分几何，宿主只在整次成功后应用结果。这个文件是可移植的宿主参考实现，不随核心库安装，也不是持久化 UI 树。有限空间测量策略、所有权和限制见 [M7 嵌套接入](docs/15-nested-grid-integration.md)。

## 可选完整布局缓存

需要跨帧复用时，由宿主长期持有一个 `GridCache`。构造时复制输入，后续用更新接口修改；每帧只传容器尺寸和外部内容版本：

```cpp
axisly::GridCache cache(spec, items);
auto first = cache.compute({640, 360});
auto same = cache.compute({640, 360}); // same.cache_hit == true
const auto& layout = same.result;     // 借用结果，不复制矩形数组

auto updated = cache.items()[0];
updated.margin.left = 12;
cache.set_item(0, updated);           // 通过输入下标更新，自动失效
auto changed = cache.compute({640, 360});
```

`set_spec`、`set_items` 和 `set_measure` 也自动失效；设置相同值保持缓存。文本、字体、DPI 或其他外部测量数据变化时，递增 `compute(size, content_version)` 的版本，或调用 `invalidate()`。更新接口只在输入变动时调用，避免每帧扫描／复制全部输入。

`CachedGridResult::measurement_count` 表示本次调用次数，命中为 0；`result.measurement_count` 保留生成该布局时的次数。结果是借用引用，应在下一次计算／修改或缓存销毁前消费，需要长期保留时显式复制。回调绑定保存函数指针和非拥有的 context，宿主负责其有效期。错误结果不缓存。所有权、异常、示例和性能边界见 [完整布局缓存](docs/20-layout-cache.md)。

## 数据模型约定

- `Unit = float`；坐标单位由宿主决定。`Insets` 顺序为左、上、右、下。
- 行列索引从 0 开始；span 默认 1；两轴默认 Stretch。`ItemId = 0` 合法，同一次计算中的 ID 必须唯一，ID 与 UI 对象的映射由宿主持有。
- `preferred_size` 的两个分量必须有限且非负，包括使用 Stretch 时。非法值返回 `InvalidPreferredSize`；margin 不支持负数。
- 固定轨道尺寸必须有限且非负；权重必须有限且大于零。权重轨道 minimum 必须有限且非负，maximum 可缺省，指定时须有限且不小于 minimum。非法工厂参数抛出 `std::invalid_argument`。
- `Track::range(Track::fixed(100), Track::weighted(1))` 表达最小尺寸和后续增长方式；下界支持 Fixed/Content/Percentage，上界支持 Fixed/Content/Percentage/Weighted，不允许嵌套 Range。同单位上界不得小于下界；混合单位解析后以下界为准。Weighted 上界使用不带额外数值约束的形式。
- `Track::definition()` 只读暴露有类型的定义；轨道值在复制后互不影响。运行时约束保存在库内部，不修改公共轨道定义。
- `MeasureInput::known_width/known_height` 的缺省值表示未知，数值 0 表示明确的零约束。minimum／preferred 的四个分量必须有限、非负，每轴 minimum 不大于 preferred；否则返回 `InvalidMeasurement` 和条目 ID。
- `compute_grid` 的回调及 context 仅在同步计算期间使用，调用者不得在回调中修改本次输入。可选 `GridCache::set_measure` 会保存非拥有的回调绑定。回调抛出的异常和内存分配异常向调用者传播。
- 默认 `GridResult` 为 `NotComputed`；仅 `GridErrorCode::None` 表示成功。失败时返回零尺寸和空条目列表，不暴露部分几何结果；`measurement_count` 保留实际调用次数，在 Debug／Release 中均可读取。
- 计算入口校验容器、轨道、padding、gap、放置、span、margin、alignment、preferred_size 和 ID。当前公共类型可表达的合法轨道和跨度均可计算；非法参数和数值溢出有独立错误码，`UnsupportedFeature` 枚举保留。
- 输出条目顺序与输入一致；不同 ID 的显式条目可以占据同一个单元格，自动条目避让占用。`compute_grid` 不修改输入，也不保存跨调用状态；可选 `GridCache`／`MeasurementCache`／`GridTree` 各自持有状态。源代码和 ABI 版本约定见 [API 契约](docs/23-api-contract.md)。

## SDL3 图形演示

Windows：

```powershell
./scripts/build-msvc.ps1 -Preset playground-release
./build/playground-release/axisly_playground.exe
```

其他平台或已初始化编译器的终端：

```text
cmake --preset playground-release
cmake --build --preset playground-release
ctest --preset playground-release
```

该预设显式开启 `AXISLY_BUILD_PLAYGROUND` 和 `AXISLY_FETCH_SDL3`，首次下载校验过的 SDL3、SDL_ttf 和 FreeType 源码并静态构建。已有开发包时，可单独启用 `AXISLY_BUILD_PLAYGROUND=ON`，保持下载选项关闭，通过 `CMAKE_PREFIX_PATH` 查找 SDL3 3.4+ 和 SDL_ttf 3.2+。

十个场景覆盖固定、两种权重比例、混合、Content、跨列、跨行、全部 16 种对齐组合、Range 和嵌套。

- `1..0` 或左侧点击：切换场景；左右键：前后切换。
- `G` / `B` / `M`：网格线、条目边框、数值标注开关，顶部按钮也可点击。
- 鼠标悬停或上下键：检查 ID 路径、放置、跨度、cell、item 和 preferred。
- `[` / `]`：调整画布宽度，观察最小宽度溢出；拖动窗口边缘实时重排。
- `R`：恢复窗口和画布大小；`Esc`：退出。

Windows 默认使用 Consolas，与 VS Code 的默认编程字体一致；缺少时寻找其他本机等宽字体。使用抗锯齿和按输出像素密度栅格化，不再使用 SDL 的像素调试字体。可通过 `AXISLY_FONT` 环境变量指定 `.ttf` / `.otf` 文件；不打包个人字体文件。详见 [M9 图形演示](docs/17-sdl3-playground.md)。

## 目录与文档

```text
include/axisly/      公共头文件
src/                轨道定义、运行时约束、输入校验、轨道／跨度分配与条目对齐
tests/              契约、轨道／跨度／对齐／测量／嵌套、接入测试与头文件检查
tests/reference/    可选 C++／Taffy 适配器、Python 对照器和 JSON 用例
examples/           控制台示例、宿主适配器、SDL3 与 GeeyoouUI 演示
examples/playground/ 可选 SDL3 窗口、十个场景、字体和检查面板
benchmarks/         可选布局耗时、分配计数和确定性场景
scripts/            Windows MSVC 工具链发现、构建和测试入口
cmake/              安装与包配置
docs/               产品、架构、路线图和初始化决策
.github/workflows/   MSVC / GCC / Clang 构建测试配置
```

- [范围与非目标](docs/01-scope.md)
- [架构](docs/02-architecture.md)
- [开发路线图](docs/03-development-roadmap.md)
- [第三方 UI 接入](docs/04-third-party-ui-integration.md)
- [SDL3 演示规划](docs/05-sdl3-visual-demo.md)
- [测试策略](docs/06-testing-and-validation.md)
- [公共 API 提案](docs/07-public-api-proposal.md)
- [M0 初始化决策](docs/08-initialization.md)
- [M1 固定布局行为与验证](docs/09-fixed-layout.md)
- [M2 权重布局行为与验证](docs/10-weighted-layout.md)
- [M3 对齐、边距与示例启动](docs/11-alignment-and-margin.md)
- [M4 测量回调与 Content 轨道](docs/12-measurement-and-content.md)
- [M5 跨行、跨列与尺寸贡献](docs/13-spanning-layout.md)
- [M6 Range 轨道、上下界与重新分配](docs/14-range-layout.md)
- [M7 嵌套网格、递归保护与 Lambda 接入](docs/15-nested-grid-integration.md)
- [M8 Taffy 共同子集与差异重放](docs/16-reference-validation.md)
- [M9 SDL3 图形演示与等宽字体](docs/17-sdl3-playground.md)
- [M10 性能基准、竞品口径与本机结果](docs/18-performance-benchmarks.md)
- [axisly／Taffy／Yoga 同场景性能对比](docs/19-comparative-benchmarks.md)
- [完整布局缓存、失效契约与优化后对比](docs/20-layout-cache.md)
- [工作区复用、预处理缓存与同场景复测](docs/21-workspace-optimization.md)
