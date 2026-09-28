# 20 — 完整布局缓存

`0.0.12` 增加可选 `gridly::GridCache`，用于宿主持久保存一个网格的最后一次成功布局。`compute_grid` 继续保持无状态；缓存层仍只依赖 C++20／STL，无第三方库。

`0.0.13` 进一步复用重新布局所需的工作区，并缓存不变输入的校验／跨格排序。接口与测量语义不变；新结果见 [工作区优化](21-workspace-optimization.md)。本文下方的 `0.0.12` 实测表保留为历史记录。

## 已实现的边界

- 构造时复制 `GridSpec` 和条目，内部只读暴露。调用者修改原始对象不会悄悄改变缓存输入，必须通过更新接口提交。
- `set_spec`、`set_items` 比较新旧值，仅在变化时失效；`set_item(index, item)` 通过输入下标在 O(1) 内更新一个条目。条目增删、顺序、ID、跨度、margin、alignment 和 preferred_size 都参与比较。
- `compute(available, content_version)` 比较容器的宽、高和内容版本。命中时返回同一个 `GridResult` 的只读引用，不扫描条目、不分配内存、不复制结果、不调用测量。
- `set_measure` 绑定原始函数指针和非拥有的 context；更换任意一项都会失效。空回调忽略 context，回退到条目的 preferred_size。
- 外部文本、字体、字号、DPI、图片固有尺寸等发生变化时，宿主必须更改内容版本或调用 `invalidate()`。不会通过对象地址推断内容是否变化。
- 只保留一个尺寸／版本的结果。尺寸从 A 到 B 再回到 A 会重新计算，不无限累积历史布局。

默认模式解决输入不变的重复调用。未启用测量缓存时，尺寸或内容改变后仍按每条目两次测量；`0.0.13` 复用工作区和不变的校验／跨格排序。从 `0.1.0` 起可调用 `enable_measurement_cache()`，通过 `invalidate_item(id)` 只清除指定条目的测量；完整约束必须相等才可复用。`GridTree` 提供嵌套脏标记传播和无关子树复用，参见 [新功能契约](23-api-contract.md)。发生失效的单个网格仍重新执行轨道分配，因为共享轨道可能影响其他条目；不承诺同一网格内仅重算一个矩形。

## 使用方法与生命周期

```cpp
#include <gridly/gridly.hpp>
#include <array>
#include <cstdint>

struct TextMetrics { gridly::Size preferred{120, 24}; } metrics;
gridly::GridSpec spec{
    .columns = {gridly::Track::content()},
    .rows = {gridly::Track::content()}
};
const std::array items{gridly::GridItem{.id = 7}};
gridly::GridCache cache(spec, items);
cache.set_measure(+[](gridly::ItemId, const gridly::MeasureInput&, void* context) {
    return gridly::MeasureResult{{}, static_cast<TextMetrics*>(context)->preferred};
}, &metrics);
std::uint64_t content_version = 0;

auto first = cache.compute({640, 360}, content_version); // 本次测量 2 次
auto same = cache.compute({640, 360}, content_version);  // 命中，本次测量 0 次
if (same.result.succeeded()) {
    const auto& rectangles = same.result.items;          // 立即消费借用的矩形
    // apply_to_widgets(rectangles);
}

metrics.preferred = {180, 28};
++content_version;
auto changed = cache.compute({640, 360}, content_version); // 内容变化后重新计算
```

缓存对象需跨帧保留；每帧重建对象或重新设置全部条目会抵消收益。全量更新接口本身仍需要比较／复制数据，只应在宿主输入变动时调用。

`CachedGridResult::measurement_count` 是本次 API 调用的实际回调次数。命中时为 0；`CachedGridResult::result.measurement_count` 是生成所借用布局时的次数，保持不变。例如首次 2 次、随后命中时分别为 `0` 和 `2`。

返回引用应在下一次计算／修改或缓存销毁前消费；若要同时保存多个帧的结果，显式复制 `GridResult`，复制成本由宿主承担。`GridCache` 不允许复制／移动，不允许在临时对象上调用 `compute`。`0.0.12` 只保留输入和结果；从 `0.0.13` 起还保留工作区容量，缩小输入或 `invalidate()` 不主动归还这些容量，销毁实例时释放。

与同步借用任意 lambda 的 `compute_grid` 不同，`set_measure` 会保留回调绑定。这里使用原始函数指针＋context，宿主负责函数和 context 在可能重新测量期间有效；不接管控件或测量对象的生命周期。回调依赖的所有外部数据都应纳入内容版本。缓存实例不支持并发访问，需要由宿主串行化。

## 错误、异常与重入

- 校验和数值错误保持核心错误码，结果为空，不缓存失败。相同参数的下一次调用仍会重试。
- 回调异常原样传播，旧缓存失效；异常后可继续使用同一实例。
- 回调中递归计算或修改同一个缓存抛出 `std::logic_error`，防止改动正在使用的输入。使用其他缓存或无状态 `compute_grid` 是允许的。
- `set_item` 下标越界抛出 `std::out_of_range`，原输入和缓存仍然有效。更新输入的内存分配失败也保留原状态。

## 可复现验证

```powershell
./scripts/build-msvc.ps1 -Preset debug
./scripts/build-msvc.ps1 -Preset comparison-release
./build/comparison-release/gridly_cache_allocations.exe
python benchmarks/comparison/compare.py `
  --native build/comparison-release/gridly_comparison_native.exe `
  --taffy build/comparison-release/benchmarks/comparison/cargo/x86_64-pc-windows-msvc/release/gridly-taffy-bench.exe `
  --output build/cache-comparison-results
```

完整比较包含 `gridly`、`gridly_cached`、Taffy 0.14.0、Yoga 3.2.1，使用相同的 11 个场景及 17 种尺寸；Yoga 仍只参与单轴场景。缓存版本除了首次和变尺寸的布局，还逐项检查每个尺寸的缓存命中结果。

以下历史数据的计时范围延续 [历史比较](19-comparative-benchmarks.md)：适配器构造和输入快照复制在计时外，计时包含布局调用及首／中／末三个矩形的消费。`GridCache` 未命中时在 `compute` 内释放旧结果，这部分计入缓存版本的时间；无状态版本的旧结果在该历史测试中位于计时外。当前比较工具已将重复布局的旧结果释放统一移入计时区，`0.0.13` 使用同一计时工具保留的 `0.0.12` 二进制重新建立基线，详见新报告。宿主遍历所有矩形、应用到控件和绘制的 O(N) 成本没有计入。


## 2026-09-28 本机结果

Windows 11 x64（10.0.26200），Intel i7-11700K（8 核／16 线程），MSVC 19.51.36257，Release `/O2 /Ob2 /DNDEBUG`，无 LTO。Taffy 使用 Rust 1.98.0、opt-level=3、无 LTO。未固定 CPU 亲和性，正式计时期间没有并行构建或其他基准，桌面后台活动仍会影响数据。

11 个场景、39 个引擎／场景组合均通过独立解析几何校验：1,326 组输出、1,938,000 个矩形，最大绝对误差为 0。正式比较串行运行两轮，每轮 9 批，第二轮反转引擎顺序；≤1,000 项每批 340 次，其他场景 34 次。以下均为 18 个批均值的中位数，单位 **µs／次**。

### 输入不变

所有 `GridCache` 场景均低于 1 µs，命中时不测量、不重新生成输出矩形。小于 1 µs 的结果统一记为 `<1`，因为逐次计时接近时钟与包装开销，不能据此精确比较三种缓存的几十纳秒差异。
| 场景 | gridly 无缓存 | GridCache | Taffy（Grid） | Yoga（Flexbox） |
| --- | ---: | ---: | ---: | ---: |
| 100 项固定横排 | 5.34 | <1 | <1 | <1 |
| 100 项固定竖排 | 5.29 | <1 | <1 | <1 |
| 100 项权重横排 | 7.44 | <1 | <1 | <1 |
| 1,000 项权重横排 | 61.78 | <1 | <1 | <1 |
| 1,000 项权重竖排 | 91.77 | <1 | <1 | <1 |
| 100 项内容测量横排 | 8.43 | <1 | <1 | <1 |
| 20×20 固定网格 | 19.69 | <1 | <1 | — |
| 20×20 权重网格 | 21.97 | <1 | <1 | — |
| 100×100 权重网格 | 570.60 | <1 | <1 | — |
| 50×50 固定跨格 | 346.06 | <1 | <1 | — |
| 50×50 权重跨格 | 292.36 | <1 | <1 | — |

### 首次布局

| 场景 | gridly 无缓存 | GridCache | Taffy（Grid） | Yoga（Flexbox） |
| --- | ---: | ---: | ---: | ---: |
| 100 项固定横排 | 5.24 | 5.21 | 30.71 | 40.20 |
| 100 项固定竖排 | 5.65 | 5.26 | 30.41 | 37.63 |
| 100 项权重横排 | 7.70 | 7.75 | 47.11 | 43.72 |
| 1,000 项权重横排 | 61.68 | 61.34 | 553.86 | 447.88 |
| 1,000 项权重竖排 | 60.57 | 61.36 | 674.26 | 593.20 |
| 100 项内容测量横排 | 8.62 | 8.89 | 84.25 | 68.93 |
| 20×20 固定网格 | 21.55 | 21.44 | 158.53 | — |
| 20×20 权重网格 | 23.74 | 23.71 | 436.91 | — |
| 100×100 权重网格 | 753.63 | 744.48 | 19,966.30 | — |
| 50×50 固定跨格 | 338.76 | 361.07 | 1,068.97 | — |
| 50×50 权重跨格 | 284.82 | 279.84 | 3,134.06 | — |

### 连续缩放

| 场景 | gridly 无缓存 | GridCache | Taffy（Grid） | Yoga（Flexbox） |
| --- | ---: | ---: | ---: | ---: |
| 100 项固定横排 | 5.14 | 5.20 | 37.23 | 25.82 |
| 100 项固定竖排 | 5.12 | 5.21 | 34.85 | 24.71 |
| 100 项权重横排 | 7.67 | 7.76 | 47.05 | 40.90 |
| 1,000 项权重横排 | 64.88 | 75.51 | 406.92 | 407.86 |
| 1,000 项权重竖排 | 89.20 | 85.21 | 519.95 | 542.13 |
| 100 项内容测量横排 | 8.23 | 8.48 | 63.59 | 50.55 |
| 20×20 固定网格 | 19.91 | 19.43 | 139.99 | — |
| 20×20 权重网格 | 20.91 | 20.67 | 396.33 | — |
| 100×100 权重网格 | 652.48 | 630.18 | 15,615.01 | — |
| 50×50 固定跨格 | 337.56 | 376.44 | 688.85 | — |
| 50×50 权重跨格 | 292.92 | 313.15 | 2,718.57 | — |

这一轮首次／缩放的批次波动较大，例如 100×100 网格无缓存首次布局的两轮中位数分别为 565.21 和 1,350.25 µs。完整原始数据保留，因此不将细小差异解释为确定的性能提升或退化。

### 未命中追加复测

针对四个重点场景，使用同一 C++ 可执行文件交替运行无缓存与缓存版本，8 轮×3 批（24 个批均值），每轮反转顺序；没有修改核心或挑选最快一轮。仅执行首次布局和连续缩放。下表为缩放的复测结果；首次布局的中位数差异在约 -1.1% 到 +1.1% 之间，完整记录见下方文件。

| 场景 | gridly 无缓存 | GridCache | 缓存版本额外耗时 |
| --- | ---: | ---: | ---: |
| 1,000 项权重横排 | 97.53 | 103.55 | +6.2% |
| 100×100 权重网格 | 574.34 | 582.45 | +1.4% |
| 50×50 固定跨格 | 375.34 | 407.46 | +8.6% |
| 50×50 权重跨格 | 374.44 | 392.60 | +4.9% |

未命中的连续缩放仍然全量计算，本轮复测额外耗时约 1%～9%，其中缓存内部释放旧结果的时间计入，而无缓存路径的旧结果在计时外释放。这个数字是整个 API 路径的差异，未隔离各部分成本，不能全部归因于缓存键比较。输入持续变化的调用尚没有获得本轮缓存优化的收益；下一阶段应评估条目级测量缓存和工作内存复用。

复测命令在前面的完整比较命令后增加以下参数，并换用独立输出目录 `build/cache-miss-recheck`：

```text
--engines gridly gridly_cached --case row_weighted_1000 --case fixed_spans_50x50
--case weighted_spans_50x50 --case weighted_grid_100x100
--mode cold --mode resize --rounds 8 --samples 3
```

### 回调与分配

100 个内容条目的真实回调次数如下。回调仅返回模拟尺寸，不包含字体排版；不能外推为真实文本渲染性能。

| 模式 | gridly 无缓存 | GridCache | Taffy | Yoga |
| --- | ---: | ---: | ---: | ---: |
| 首次布局 | 200 | 200 | 300 | 100 |
| 连续缩放 | 200 | 200 | 0 | 0 |
| 输入不变 | 200 | 0 | 0 | 0 |

独立分配程序在 0、1、100、10,000 个条目上分别测试有／无测量回调，各命中 1,000 次。八组均为 **0 次分配、0 字节申请、0 次测量**，且结果缓冲区地址保持不变。分配探针不参与计时；没有测量三库的保留内存或建树成本。

### 验证与原始记录

- MSVC Debug：227/227 通过。
- MSVC Release 比较预设：234/234 通过，包括缓存测试、分配契约和全量几何冒烟。
- 安装消费工程、默认子目录消费工程：各 1/1 通过；安装内容为 8 个头文件、静态库和 4 个 CMake 配置文件，不含竞品或演示依赖。
- 缓存测试覆盖输入快照、所有更新字段、别名输入、尺寸双轴、内容版本、回调和 context 替换、错误／异常恢复、重入保护、越界更新与 400 步随机修改。

完整比较：[汇总](benchmarks/cache-msvc-2026-09-28/summary.csv)、[分轮记录](benchmarks/cache-msvc-2026-09-28/results.csv)、[每批采样](benchmarks/cache-msvc-2026-09-28/samples.json)、[几何校验](benchmarks/cache-msvc-2026-09-28/geometry.json)、[环境及可执行文件 SHA-256](benchmarks/cache-msvc-2026-09-28/environment.json)、[分配检查](benchmarks/cache-msvc-2026-09-28/cache-allocations.csv)。同目录 fixtures 保存 11 份输入。

未命中复测：[汇总](benchmarks/cache-miss-msvc-2026-09-28/summary.csv)、[每批采样](benchmarks/cache-miss-msvc-2026-09-28/samples.json)、[环境与选择参数](benchmarks/cache-miss-msvc-2026-09-28/environment.json)。此数据单独保留，不替换首轮完整对比。
