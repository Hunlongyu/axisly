# Axisly Flexbox 规范矩阵与开发状态

## 2026-09-28 状态

`compute_flex` 当前只接收**确定的有限容器尺寸**。已加入行／列及
reverse、wrap／wrap-reverse、LTR／RTL 和纵向轴映射、固定与百分比
尺寸、grow／shrink 冻结迭代、auto margin、gap、主交叉轴对齐、
宽度确定后的测量、绝对定位子项的静态位置及基本 offset 几何、
`visibility:collapse` 的两轮重排。GeeyoouUI 的 BoxLayout 实际
排布已使用 Flex；Overlay／Anchor 是单独的非 CSS 布局 API。

仍未完成的验收项包括：不确定容器尺寸下的完整 intrinsic sizing、
fragmentainer／break 传播与子项续片、正交书写模式下的完整基线与
尺寸推导、替换元素自动最小尺寸的所有分支、绝对定位的全部过约束
规则、从 WPT 抽取的逐项几何对照，以及 Grid／Flex／Overlay
混合保留树。当前测试只证明覆盖到的用例，不能据此宣称完整
CSS Flexbox Level 1 兼容。

## 验收含义

目标是 CSS Flexbox Level 1 **已定义的布局几何语义**，包括少见边界。
输入为 C++ 类型化的盒与约束，输出为几何、溢出、分片和必要的顺序信息。
CSS 文本、选择器、级联、DOM、字体绘制和命中测试由宿主负责。
不能把「Taffy 也得到相同矩形」当成规范符合性的唯一证据。

版本基准：[CSS Flexible Box Layout Module Level 1](https://www.w3.org/TR/css-flexbox-1/)
当前公开稿。实现时锁定所依据的规范日期，并记录后来规范变动。

## Flexbox 章节映射

| 章节 | 布局输入／行为 | 输出与验证 |
| --- | --- | --- |
| 2–3 盒与 flex container | 主轴／交叉轴、content／padding／border／margin、flex 与 inline-flex 的内布局 | 容器几何；外部 inline 格式化由宿主给定包含块约束 |
| 4 Flex items | 正常流项目、绝对定位子项、auto margin、padding、自动最小尺寸、collapsed strut、匿名项目的规范化 | 项目盒、绝对定位 static-position rectangle、collapse 后重新换行；绘制与层叠由宿主消费顺序元数据 |
| 5 顺序和方向 | 四种 flex-direction、三种 flex-wrap、order、LTR／RTL、纵向 writing modes | 稳定排序、所有轴映射、反向与换行反向的坐标 |
| 6 Flex lines | 单／多行收集、不可拆分项目、尺寸不确定时的行构成 | 行列表、行项目归属和溢出 |
| 7 伸缩 | flex-basis 的 auto／content／长度／百分比／固有值，grow、shrink、因子总和小于 1、边界冻结 | base／hypothetical／target main size；规范迭代结果 |
| 8 对齐 | auto margin、justify-content、align-items、align-self、align-content、基线组和溢出对齐 | 主／交叉轴最终位置、行交叉尺寸、基线位置 |
| 9.1–9.2 初始与行长 | 项目生成、order、基准尺寸、可用空间、确定与不确定尺寸 | flex base size、hypothetical size、行分组 |
| 9.3 主轴尺寸 | 正／负剩余空间、冻结、min／max 限制及 scaled shrink factor | 每项最终主轴尺寸；溢出不被静默截断 |
| 9.4 交叉轴尺寸 | 重新测量、stretch、baseline、行交叉尺寸、交叉轴 min／max | 每项最终交叉尺寸和行尺寸 |
| 9.5–9.6 最终对齐 | 主轴 margin／justify 和交叉轴 align；reverse／wrap-reverse | 每项最终逻辑与物理矩形 |
| 9.7–9.9 长度和 intrinsic | definite／indefinite、自动最小尺寸、aspect ratio 转移、min-content／max-content、单／多行 intrinsic | 独立内在尺寸查询和与分配尺寸一致的结果 |
| 10 分片 | fragmentainer、break-before／after／inside、跨片项目／行、断点传播 | 每片矩形、续片关系与占用范围 |
| 附录 A 轴映射 | writing-mode 与 direction 组合 | 水平、垂直及混合书写方向测试 |

附录 B 的旧 `-webkit-` 属性是 CSS 别名语法。Axisly 没有属性解析器，
由类型化输入表达其布局含义，不提供这些字符串别名。

## 必需的相邻规范与宿主边界

| 来源 | Axisly 需要的几何规则 | 宿主提供的内容 |
| --- | --- | --- |
| [CSS Box Sizing 3](https://www.w3.org/TR/css-sizing-3/)／4 | content／border box、min／max、百分比、intrinsic、aspect ratio | 文本／图像／控件的内容尺寸探针 |
| [CSS Box Alignment 3](https://www.w3.org/TR/css-align-3/) | gap、safe／unsafe、基线组、对齐回退 | 基线度量与逻辑方向 |
| [CSS Writing Modes 4](https://www.w3.org/TR/css-writing-modes-4/) | 逻辑到物理轴、纵向书写、正交测量 | 文本布局及其书写模式 |
| [CSS Fragmentation 3](https://www.w3.org/TR/css-break-3/) | 断点类别、传播、片段容器约束 | fragmentainer、项目可分片性、内容断点 |
| CSS 定位布局 | 绝对定位子项的静态位置及定位几何 | 包含块和定位偏移；绘制由宿主承担 |

宿主需把控件表示为已规范化的盒：文字节点、替换元素、表格 wrapper
等的内容排版不是 Flex 算法。没有对应宿主度量时，应返回明确的不支持
或测量错误，不能用 preferred size 冒充 min-content／max-content。

## 测量接口必须区分的状态

- 主／交叉轴的 **已知尺寸、未知尺寸和已知零**；确定与不确定的包含块。
- min-content、max-content、在指定宽／高下的内容尺寸与基线。
- content box、padding box、border box、margin box；auto margin 单独
  表示，不能等同零固定 margin。
- aspect ratio、替换元素固有尺寸、正交 writing mode 的度量。
- 分片时的剩余空间、可用断点、产生的 fragment 与 continuation。
- 回调的同步借用、异常传播、重入限制和同一次布局内可复用的测量键。

现有 `MeasureResult{minimum, preferred, baseline}` 继续服务 Grid 兼容
入口；Flex 使用新契约，不能通过字段改名假装完成扩展。

## 规范本身未给出唯一算法的地方

Flexbox 9.9.1.2 的 web-compatible intrinsic sizing 小节仍是占位文本。
10.1 分片是 *informative* 示例，而不是唯一强制算法。此类情况应
记录采用的确定性规则、浏览器互操作证据、偏差测试和版本，不标成
「已证明与所有浏览器像素一致」。标准后续修订时复核。

## 验证策略

1. 每个矩阵行至少有规范示例、边界用例、反向／纵向组合和嵌套用例。
2. 从 [WPT Flexbox](https://wpt.fyi/results/css/css-flexbox/) 抽取可由
   类型化盒输入表达的几何断言，记录不能抽取的绘制／DOM 断言原因。
3. 与 Taffy 和浏览器只比较共同、已定义的输入；差异按规范判定，
   不用多数实现投票覆盖规范文本。
4. 单元测试之外覆盖 Grid／Flex 混合树、宿主测量、GeeyoouUI
   窗口、DPI、Debug／Release／ASan 与安装消费。
5. 所有矩阵行达到验收后才能宣称 Flexbox 首版完成；阶段性实现应在
   文档和示例中明确标注未覆盖项。整个过程不发布。
