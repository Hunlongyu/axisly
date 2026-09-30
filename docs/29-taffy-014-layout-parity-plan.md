# Axisly 对齐 Taffy 0.14.0 布局能力：实施计划

2026-09-29 已确定兼容路线：保留现有 `compute_grid`／`GridTree` 合法输入的
语义，新增 CSS 兼容布局入口。目标是逐阶段达到 Taffy 0.14.0 布局能力全覆盖；
每阶段只公布已验收能力。当前实施与缺口以
[字段矩阵](30-taffy-014-capability-matrix.md)为准。

## 目标与边界

目标是让 Axisly 用类型化 C++20 输入覆盖 Taffy **0.14.0 的布局能力**：
Block／FlowRoot（含浮动）、Flexbox（含 balance）、Grid，以及这些算法使用的
盒模型、尺寸、对齐、方向、定位、溢出、`calc()` 和嵌套树。验收对象是可观察的
节点几何、固有尺寸、基线、溢出和必要诊断；不要求复制 Taffy 的 Rust API 或内部结构。

此目标不包括完整 CSS 样式表解析、选择器／级联、DOM、文字塑形、绘制、事件和滚动
控件。Taffy 的可选 `parse` 特性解析部分样式值；它不是完整样式表引擎。
宿主继续负责内容排版与绘制。已有的 [Flexbox 规范矩阵](28-flexbox-conformance-matrix.md)
包含一些超出 Taffy 0.14 能力的长期目标；本计划不取消它们，但要单独标注，
不能将“对齐 Taffy”当成“完整 CSS／浏览器兼容”。

参考基准固定为 [Taffy 0.14.0 文档](https://docs.rs/taffy/0.14.0/taffy/)、
[`Style` 字段](https://docs.rs/taffy/0.14.0/taffy/struct.Style.html)和
[`TaffyTree` 行为](https://docs.rs/taffy/0.14.0/taffy/struct.TaffyTree.html)。
先冻结版本与测试输入；以后升级 Taffy 必须重新评估矩阵。

## 当前基线（2026-09-28）

- 当前源码为 `axisly` 0.1.0；`compute_grid`、`GridCache`、`GridTree`、
  `MeasurementCache`、像素取整及 Grid 宿主适配已经存在。
- 当前 Release 构建目录的 267 项 CTest 全部通过；这是已有构建的运行结果，
  本次尚未做重新编译、规范符合性或完整 Taffy 差分验收。
- 当前对照工具把 Taffy 0.14.0 的默认特性关闭，只启用 Grid、树和诊断；
  原有 780＋913 个差分用例覆盖经过准入筛选的 Grid 共同子集。
- `GridTree` 只能保存 Grid 容器和叶子；`MeasureResult` 只有
  `minimum/preferred/baseline`，不能表达所有 CSS 固有尺寸查询。

| 差距 | 当前实现 | 达标需要 |
| --- | --- | --- |
| 布局算法 | 原生 Grid | Flexbox、Block／FlowRoot、Float／Clear、混合树 |
| Grid 轨道算法 | Fixed／Weighted／Content／Range／Percentage | 独立的 CSS `fr`、`auto`、min/max-content、minmax、fit-content、repeat 及各阶段跨格贡献 |
| Grid 放置 | 数字起点、跨度、自动／Dense、统一隐式模板 | 网格线起止、负索引、具名线／区域、重复模板、隐式轨道模式 |
| 盒与对齐 | 容器 padding／gap、非负数字 margin、条目四向对齐、竖直首基线 | border、box-sizing、auto／负 margin、容器对齐、更多基线和安全对齐 |
| 尺寸与测量 | 有限容器尺寸、最小／首选测量、确定宽度后的高度重测 | definite／indefinite、min/max-content、逐节点 min/max、百分比、纵横比、替换元素、基线查询 |
| 跨算法规则 | 原生 Grid 嵌套、输出取整与缓存 | display:none、position／inset、direction、overflow／scrollbar、contain、calc、跨算法失效 |

上表描述的是能力缺口，不宣称现有代码有未复现的计算错误。已有 Grid 的
`Weighted`、`Content`、`Range` 是已定义的原生语义；不能通过改名或放宽差分
准入条件把它们当成 CSS 同名概念。现有用例全部通过也不能证明超出准入范围的行为。

### Taffy `Style` 输入清单

Taffy 0.14.0 的 `Style` 文档列出 44 个字段。其中 `dummy` 是泛型编译辅助字段，
不代表布局能力；其余 43 个字段必须逐项获得实现、映射或有证据的“不影响几何”结论。
下表是本轮的初始分组，P0 要将每个字段再展开成取值和组合用例。

| 字段组 | Taffy 字段 | Axisly 当前 |
| --- | --- | --- |
| 算法与内容角色 | `display`、`item_is_table`、`item_is_replaced` | 仅有 Grid 容器／叶子类型；无通用 display 和替换元素语义 |
| 盒与环境 | `box_sizing`、`direction`、`overflow`、`scrollbar_width`、`contain` | 未实现 |
| 流与定位 | `float`、`clear`、`position`、`inset` | 未实现 |
| 节点尺寸 | `size`、`min_size`、`max_size`、`aspect_ratio` | 仅有 `preferred_size` 回退和轨道约束；没有等效的节点样式 |
| 间距 | `margin`、`padding`、`border`、`gap` | 有 Grid 非负数字 margin、容器 padding／gap；无 border／auto margin |
| 对齐 | `align_items`、`align_self`、`justify_items`、`justify_self`、`align_content`、`justify_content`、`text_align` | 只有 Grid 条目四向对齐及竖直首基线的部分能力 |
| Flexbox | `flex_direction`、`flex_wrap`、`flex_line_count`、`flex_basis`、`flex_grow`、`flex_shrink` | 未实现 |
| Grid 轨道 | `grid_template_rows`、`grid_template_columns`、`grid_auto_rows`、`grid_auto_columns`、`grid_auto_flow` | 有原生显式／隐式轨道和自动流，但类型与语义范围较窄 |
| Grid 命名与放置 | `grid_template_areas`、`grid_template_column_names`、`grid_template_row_names`、`grid_row`、`grid_column` | 仅支持数字起点、跨度和自动放置 |
| 内部辅助 | `dummy` | 不列入功能对齐 |

除了字段存在，还须覆盖其值类型：长度／百分比／auto／固有尺寸关键字／
`calc`、Flex 换行类型、Grid 的 `fr`／minmax／fit-content／repeat、
网格线和命名区域，以及不同 `Display` 之间的嵌套组合。

### 现有算法需要深化的具体位置

| 代码位置 | 现行规则 | 对齐任务 |
| --- | --- | --- |
| `src/detail/track_sizing.cpp` | Content 在最小／首选值之间按共同系数收缩；Weighted 按权重分配剩余空间并冻结数值上下界 | 新 CSS Grid 路径实现规范轨道尺寸各阶段与 `fr` 规则，保留旧路径结果 |
| `src/detail/span_resolution.cpp` | 跨格缺额先让 Content 增长，再提高 Weighted 下限；Fixed 不随内容增长 | 用规范的跨格固有贡献、增长上限与弹性轨道阶段覆盖所有组合 |
| `src/compute_grid.cpp` | 非负数字 margin；宽度确定后只重测高度；竖直首基线按起始行分组 | 加入 auto／负 margin、宽高及固有约束探针、完整基线与方向行为 |
| `include/axisly/tree.hpp` | 子 Grid 用有限空间、零高度、零宽度探测最低尺寸 | 新通用树显式查询 min-content／max-content；不把原生探测结果当成 CSS 固有尺寸 |

这些是**语义覆盖不足或有意采用原生规则**的证据，不自动认定为现有契约下的缺陷。
性能优化必须在对应语义的正确性验收之后，以真实热点和同任务基准驱动。

## API 与兼容性原则

现有 `compute_grid`／`GridSpec`／`GridItem` 的合法输入语义应先作为兼容基线。
CSS 兼容布局通过新的类型化样式与计算入口表达，共用可复用的几何、树和缓存机制；
不把旧 `Track::weighted` 悄悄改成 CSS `fr`，也不把旧 `Content` 改成 CSS `auto`。
采用并行 API，不在旧类型上重新解释 `Weighted`／`Content` 等原生规则。

建议建立 `LayoutStyle` 与通用 `LayoutTree`，容器使用 `Display` 选择算法。
`GridTree` 的现有入口保留为兼容层或可迁移入口。新测量契约区分已知、未知和
已知零尺寸，以及 definite／min-content／max-content 等查询；现有回调继续服务旧 API。
回调的所有权、异常、重入保护和失败时无部分几何的约定应继续保持。

### 实现结构（2026-09-29 补充）

新能力采用独立的 CSS 布局管线，不向 `compute_grid.cpp`、
`track_sizing.cpp` 或 `GridTree` 连续添加模式分支：

1. **样式值层**：类型化长度、百分比、`calc`、Grid 轨道／网格线及对齐值，
   负责输入合法性和确定／未定基准的解析。
2. **共用盒层**：统一 content／padding／border／margin、min/max、纵横比、
   固有尺寸探针、方向与溢出；算法只消费解析后的盒约束。
3. **算法层**：Block、Flex、CSS Grid 各自实现独立计算过程；共享测量和
   盒模型，不共享不相同的轨道／伸缩规则。
   CSS Grid 的轨道尺寸和 Block 的浮动区域分别位于独立内部模块。
4. **树与输出层**：同一节点模型调度三种算法，统一缓存、失效、基线、
   相对／绝对定位、诊断与像素取整；错误时不发布部分布局。
5. **兼容入口**：旧 `compute_grid`／`GridTree` 保留原生规则和回归测试，
   只通过显式适配器复用确实相同的底层工具，不以旧实现作为 CSS 算法主体。

参考验证同样分离：原 V1/V2 固定 Grid 共同子集；新的树形工具有独立
可执行程序、输入协议和用例。待新原生树可用时才做混合算法差分，
未支持输入应报告未支持，不能算作通过。

## 实施顺序与每阶段交付

### P0：冻结能力矩阵和参考工具

1. 枚举 Taffy 0.14.0 的 `Display`、全部布局相关 `Style` 字段及取值，
   逐项标记 Axisly 的“未支持／部分支持／等效支持／有意不同”。
2. 新建独立的树形差分协议，允许混合 Block／Flex／Grid、叶节点测量与
   definite／indefinite 可用空间；保留现有 Grid V1／V2 工具与失败重放。
3. 建立可复现的规范／WPT／Taffy 用例索引，标明预期来自何处。
   Taffy 与规范或浏览器不一致时记录差异，不能修改准入规则使问题消失。

已建立[规范／WPT／Taffy 用例索引](32-css-normative-case-index.md)，记录
已裁决差异；两条 Flex WPT 的布局几何已移植为独立断言、Taffy
差分与 Chrome 重放。原始 reftest 绘制验收及更多字段的独立规范断言
仍须补齐。

**验收**：矩阵有精确版本和证据；每个现有与缺失能力都有可执行或待实现用例；
旧 Grid 对照与 267 项本库测试保持通过。

### P1：共用盒模型、尺寸与测量契约

加入长度与百分比、类型化 `calc` 表达式、content／border box、margin 的数值与
`auto`、padding、border、min/max、纵横比、方向、基线及固有尺寸查询。
定义通用节点身份、错误与结果，支持按节点局部失效。新 API 必须能表达
Taffy 0.14.0 布局输入；无法由宿主测量的内容明确报错，不以首选尺寸冒充固有尺寸。

**验收**：盒模型和测量的单节点、嵌套、零尺寸、百分比未定、极端数值、
异常与缓存键测试；旧 API 几何逐项回归。

### P2：Flexbox 与 Grid／Flex 混合树

实现主轴／交叉轴映射、flex-basis、grow/shrink 冻结、min/max、自动最小尺寸、
单／多行、wrap／wrap-reverse／balance、gap、auto margin、各类对齐、基线、
绝对定位子项和嵌套测量。先提供独立无状态计算，再接入通用树与缓存。
Taffy 0.14 未实现而现有 Flexbox 规范计划要求的 `order`、纵向 writing-mode、
分片等，作为独立扩展目标保留，不混入“Taffy 全覆盖”完成判断。

**验收**：每项 Flex 输入及重要组合有固定、边界和树形用例；通过固定版本
Taffy 差分，并用规范／WPT 裁决差异；Grid 中 Flex、Flex 中 Grid 均验证。

### P3：CSS Grid 语义补齐

在新 CSS Grid 入口实现规范的轨道尺寸阶段、固有贡献和跨格分配、`fr`、
`auto`、min/max-content、minmax、fit-content、repeat（含 auto-fill/fit）、
具名线与区域、负网格线、起止线、隐式轨道序列、自动／Dense 放置、
容器与项目对齐、基线及绝对定位 Grid 子项。旧原生 Grid 入口继续回归。

**验收**：每类轨道组合在空间充足、不足、未定空间、百分比循环、跨格与
嵌套时有断言；测试不仅比条目矩形，也比实际轨道、放置和占用范围。

### P4：Block／FlowRoot、浮动和跨算法规则

实现 Block 格式化、外边距折叠、FlowRoot、浮动与 clear；统一
display:none、相对／绝对定位、overflow 与滚动条空间、Taffy 已实现的
layout／paint containment、方向和
与各算法相关的盒模型规则。Taffy 不负责文字塑形，宿主仍提供叶子测量。

**验收**：Block／Float 与 Flex／Grid 互相嵌套，包含相对／绝对定位、
溢出、方向切换、隐藏节点、动态插删和宿主文字测量的组合用例。

已加入 56 组 `Visible`／`Clip`／`Hidden`／`Scroll` 双轴与自动最小尺寸
对照：三种根布局均检验子树溢出传播，Flex／Grid 检验 `Clip` 不将
自动最小尺寸置零。更深层嵌套和复杂定位组合继续按矩阵验收。
另有 28 组 `None`／`Layout`／`Paint`／`Content` 对照，覆盖子树溢出、
跨算法基线与 Block 浮动包含；独立数值断言固定三种效果的差别。
滚动条对照现覆盖 144 组 Block／Flex／Grid、LTR／RTL、两种盒尺寸、
六种双轴 overflow 组合，以及百分比正常流和绝对定位子项。
它修正了 ContentBox 指定尺寸重复加入滚动条宽度、RTL 内容起点、
绝对定位静态对齐和 Grid 定位区域的共用计算。
另有 648 组纵横比、两种盒尺寸与滚动轴交叉差分，覆盖 Block／Flex／Grid
父子组合和单轴指定尺寸；仍需验证更深层循环与极限数值。
四层树的 2,592 组差分已覆盖三种根／中间／内层算法、中间层单轴指定或
自动尺寸、内层双轴百分比、两种盒尺寸、四种滚动轴组合和 LTR／RTL，
逐节点比较几何、盒边、滚动范围及 Grid 轨道。它区分 Block 子项预解析
与子树实际包含块、已知尺寸的确定性，以及 Grid 自动轨道的比例传递。
更深树、替换元素及极端数值仍未验收。

### P5：性能与真实宿主收尾

对已达到正确性要求的输入，分别比较首次计算、尺寸变化、输入不变、
局部内容变化及混合树；记录时间、分配、峰值内存和测量次数。先定位
回退热点，再优化缓存、失效、轨道工作区和输出构造，优化后重跑几何验证。
在 GeeyoouUI 使用真实窗口、字体和 DPI 验证代表性 Grid／Flex／Block 页面。

**验收**：Debug／Release、ASan、安装消费、参考对照与宿主检查通过；
所有性能结论对应相同输入、相同计时边界及保存的原始数据。

当前已建立 [CSS 混合树保留布局基准](31-css-retained-benchmarks.md)，
保存首次计算、输入不变、尺寸变化和局部编辑的时间／分配原始数据；
修复了输入不变时重复构造已发布结果的问题，并由已安装库的消费程序
验证新 CSS 树入口。基准现有合成与 DirectWrite 真实字体两种回调；
[GeeyoouUI 的新 CSS 混合树示例](host-validation/geeyoou-css-2026-09-30/README.md)
已在真实窗口与文字路径下验收代表性 Grid／Flex／Block 控件，
更多页面和实际跨显示器 DPI 仍需验收；
Grid/Flex 固有尺寸探测与每次变更时的输出构造仍是已测得的热点。
随后 9,216 组混合树差分促成 Grid 交叉轴贡献重测，并加入有界的
多约束布局片段与宿主测量缓存；局部编辑的测量和耗时已明显下降，
首次布局仍因重复片段构造而回退。两代输入相同的原始基准、分配统计和
二进制哈希均保存在 [CSS 混合树保留布局基准](31-css-retained-benchmarks.md)。
随后将尺寸查询、基线查询和完整几何输出分层，Grid／Flex 的固有尺寸
探测在尺寸确定后返回；完整结果仍分别输出节点和轨道。384 叶合成首次
布局从 9666.15 µs 降至 8154.49 µs，申请次数从 47164 降至 40316；
DirectWrite 96 叶首次从 4978.93 µs 降至 3680.68 µs。
[第三代同输入原始记录](31-css-retained-benchmarks.md#固有尺寸查询的输出分层)
保留批次和二进制哈希。Release／Debug 参考差分与 MSVC ASan 通过；
首次计算仍慢于旧共同子集基准，需继续审视 Grid 轨道求解次数与工作区。
四层纵横比矩阵修正后，合成 384 叶首次布局为 8222.41 µs、40188 次
C++ 申请；DirectWrite 96 叶首次为 4442.64 µs、10572 次申请。
[第四代同输入原始记录](31-css-retained-benchmarks.md#四层纵横比与百分比尺寸对照后)
显示真实文字路径耗时上升，尚待归因。参考 Release／Debug 各通过
345/345、MSVC ASan 通过 344/344，宿主三种配置各通过 296/296；
实际跨显示器 DPI 和更多页面仍未完成。

## 完成判据

- Taffy 0.14.0 **布局相关**特性矩阵没有“未支持”或未解释的“部分支持”；
  每一项都有 API、行为测试、参考输入及已记录的差异结论。
- 新旧 API 的各自语义明确；旧合法输入的几何、错误及测量调用约定经回归确认。
- 多算法混合树、缓存命中、局部失效、真实内容测量、取整和数值边界均经过验证。
- 对照只证明锁定输入与版本的行为；不以 Taffy 一致性代替 CSS 规范判断。
- 未达上述条件时，按阶段公布已完成范围，不宣称“完整对齐 Taffy”。
