# CSS 布局的内存、结果发布与批量执行

## 跨临时树复用内存

```cpp
#include <axisly/css_tree.hpp>

auto memory = std::make_shared<axisly::css::LayoutMemory>(32 * 1024 * 1024);
{
    axisly::css::LayoutTree tree({}, memory);
    const auto root = tree.create_node();
    const auto& result = tree.compute(root, {});
    // 使用 result；树销毁时，其大块内部存储返回 memory。
}
axisly::css::LayoutTree next_tree({}, memory);
// 不再需要保留空闲块时：
memory->release_unused();
```

资源的默认空闲缓存上限为 64 MiB，只复用至少 4 KiB、尺寸与对齐匹配的块。
`LayoutTree` 持有资源的共享所有权；主程序提前释放自己的 `shared_ptr` 不会使树失效。
资源通过互斥量支持不同树并发分配，单棵树仍需要独占访问。

接入范围包括节点页、查询缓存 arena、根几何、盒模型与位移记录池、Flex/Block 工作缓冲、
Grid 放置数组和主要条目工作缓冲。公开结果的 `std::vector`、样式自身的容器和宿主
回调的内存仍遵循各自的分配方式。`stats()` 统计经过该资源的请求，不代表整个进程内存。

`cache_limit()` 限制的是已经归还的空闲字节；活跃树的内存由实际规模和 `TreeLimits`
约束。`set_cache_limit(0)` 禁止后续保留，并释放现有空闲块。
`release_unused()` 不改变活跃树或已经发布的布局结果。统计提供活跃/缓存字节、
活跃字节峰值、系统分配次数和复用次数。

## 局部更新的坐标发布

引擎在共享的不可变子树上记录上一份已成功发布结果中的范围。只有子树仍是同一份、
发布代次匹配，并且每一步坐标变换的浮点位模式与顺序完全一致时，才复制整个已验证
坐标范围。其余子树照常展开和检查；复制或修改子树时失效该记录。

这减少重复遍历与坐标变换，完整输出的内存复制仍随输出节点数增长。
`LayoutResult::published_nodes_reused` 可观察这一复用量。
它与布局查询缓存命中数不同；所有节点、滚动范围与 Grid 轨道报告仍完整发布。
回调异常保留上一份完整结果，失败的布局尝试不会发布可复用代次。

### 按节点读取已发布结果

`LayoutTree::find_layout(NodeId)` 返回最近一次发布结果中的节点指针，缺失时返回
`nullptr`。首次读取按需建立索引；后续发布仍保持节点位置时直接复用索引。
重排、换根和槽位复用会通过完整 NodeId 校验，必要时重建索引。
索引是树内的惰性状态；同一树的查询与其他访问需要串行或由宿主同步。
树修改本身不改变已发布快照，计算失败则发布空结果；回调异常保留原快照。

索引不参与 `compute()`，不使用该接口时没有索引分配或逐帧维护成本。
密集节点约需每个槽位 `sizeof(size_t)` 字节；存在空洞或只计算部分子树时，
空间按该快照中最高槽位计算。首次索引构建需要遍历输出和初始化槽位数组。
`LayoutResult::find()` 保持无索引的线性查找，适合只读取少量节点的独立快照。

这是已发布结果的按需读取接口；当前 `compute()` 仍生成完整输出。

## 同步计算独立树

```cpp
#include <axisly/css_batch.hpp>
#include <array>

axisly::css::LayoutTree first, second;
const auto a = first.create_node(), b = second.create_node();
axisly::css::LayoutBatch batch(4); // 包括调用线程；另外创建三个常驻线程。
std::array requests{
    axisly::css::LayoutRequest{&first, a, {}},
    axisly::css::LayoutRequest{&second, b, {}},
};
const auto results = batch.compute(requests, axisly::css::BatchExecution::Parallel);
for (const auto& result : results) {
    if (result.exception) std::rethrow_exception(result.exception);
    if (result.succeeded()) { /* 使用 result.layout */ }
}
```

省略执行模式时使用 `CallerThread`，所有测量回调都留在调用线程。
显式选择 `Parallel` 表示宿主允许回调在工作线程执行，并允许不同树的回调同时运行。
线程绑定的 UI/字体对象需要由宿主安排相容的测量方式。

每个请求必须是不同的树，提交期间树和回调状态需要独占或相应同步。
空指针和重复树在执行任何请求之前被拒绝；同一执行器的并发或重入调用被拒绝。
所有任务完成后才返回。某一请求的回调异常会被该项的 `exception` 捕获，其他独立
请求仍执行并正常发布；引擎自身的布局错误继续放在 `layout->error` 中。

结果 span 保留到下一次批量调用或执行器销毁。布局指针仍由各自的树持有，遵循普通
`compute()` 的生命周期。线程数为 1 时不创建工作线程；小任务可直接调用树或使用
`CallerThread`，避免调度与同步成本。批量吞吐量收益取决于独立树数量和每棵树的工作量。

## 可选 Clang PGO

默认 `AXISLY_CLANG_PGO=OFF`。Clang / clang-cl 可选择以下流程：

1. 在独立 Release 构建目录配置 `AXISLY_CLANG_PGO=GENERATE`，
   `AXISLY_PROFILE_PATH` 指向收集目录。运行代表性的实际业务负载。
2. 使用同版本 LLVM 的 `llvm-profdata merge -output=axisly.profdata <收集目录>` 合并。
3. 在另一构建目录设置 `AXISLY_CLANG_PGO=USE`，
   `AXISLY_PROFILE_PATH` 指向合并文件，重新构建并验证。

生成阶段使用原子计数，支持训练并行场景。USE 阶段不需要采集运行库。
过期 profile 的诊断作为错误处理；修改 profile 文件会触发使用它的对象重新编译。
PGO 与 sanitizer 使用不同目录。MSVC 默认构建不读取 Clang profile。
训练效果须由未参与训练的负载验证；项目不默认启用特定 CPU 的 AVX 指令集。

流程依据 [Clang PGO 文档](https://clang.llvm.org/docs/UsersManual.html#profile-guided-optimization)。

## CSS 像素取整

```cpp
axisly::css::LayoutTree tree;
const auto root = tree.create_node();
const axisly::css::AvailableSize available{
    axisly::css::AvailableSpace::definite(800),
    axisly::css::AvailableSpace::definite(600)};
const auto& logical = tree.compute(root, available);
const auto& pixels = tree.compute(root, available, axisly::PixelRounding{1.5F});
```

两参数调用保持逻辑坐标；三参数调用按每逻辑单位对应的设备像素数取整。
比例为 1、原点为 0 时，采用锁定的 Taffy 0.14 取整语义：局部位置分别取整，
宽高由未取整累计坐标的两端取整后相减；负半像素向正方向取整。
输出仍为根相对坐标。边框、内边距、滚动条、滚动范围、内容盒和基线同步处理。
margin 与 Grid 轨道诊断继续保留逻辑值，不用于反向推导已取整的节点矩形。

`origin_x / origin_y` 指定累计边缘在设备网格上的逻辑原点；根位置减去已取整原点，
子节点局部位置继续遵循上述 Taffy 规则。嵌套节点的位置按局部坐标取整，
尺寸则由累计坐标的两端取整后相减，不能简单地对根相对矩形逐字段取整。

布局和测量缓存始终保留未取整值；改变比例、原点或切回逻辑坐标时，重新从原始
片段发布，避免累计漂移。相同输入及取整参数直接复用已发布结果。
所有 `compute()` 重载共享结果生命周期；需要同时持有两份结果时应复制快照。
非法比例或原点在计算前抛出 `invalid_argument`；数值溢出发布空错误结果；
宿主测量回调抛异常时保留上一份已发布结果。

批量请求可设置 `LayoutRequest::rounding`；未设置时保持逻辑坐标。
