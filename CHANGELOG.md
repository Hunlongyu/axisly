# 变更记录

开发期版本暂不承诺跨版本源码或二进制兼容。升级后重新编译全部使用方。
本文件从当前评审核对开始维护，不替代此前全部 Git 历史。

## 未发布

### 0.2.0：统一 Grid 语义

BREAKING CHANGE：删除原生 `compute_grid`、`GridCache`、`GridTree` 及配套的
`Track`／`GridSpec`／`GridItem`／测量与结果类型，不保留别名或兼容层。
Grid 统一通过 `axisly::css::Style`、`TrackSizing`、`GridLine` 和布局树计算。
保留 `LayoutTree` 与 `LayoutSource/LayoutSession` 两种接入方式，共用同一个 CSS 内核。
控制台示例合并为 `css_grid.cpp`；GeeyoouUI、SDL3 Playground、安装后消费者及基准均已迁移。
旧原生算法专属测试和参考驱动随实现移除，CSS 测试与全量上游差分保留。

BREAKING CHANGE：删除 `css::LayoutTree::remove` 重复入口，原调用直接改为
`remove_subtree`，递归删除行为不变。仅删除节点并保留其子节点时使用 `remove_node`。

### 评审修复

- 批量 `set_children` 只扫描一次祖先链，并按旧父节点统一移除迁移项；相同子节点列表直接复用。
- 叶子发布数组预留根槽位；隐藏子树的尺寸/基线查询保留深度检查，省去后代几何构造。
- 减少 Block 的布局盒拷贝和重复父节点查找；仅切换 Grid 轨道报告时保留无关分支缓存。
- 借用模式的已发布几何增加惰性排序索引；浮动子树检测跨计算复用，并保留深度与失效检查。
- 像素取整复用上下文相同的不可变子树输出；按需输出复用已完成的取整数值检查。
- Flex 基线查询仅对选定项目执行最终布局，保留行测量、反向排列与定位语义。
- 增加查询、Block、取整及基线专项基准和分配探针，结果见 `benchmarks/hotpath-optimization.md`。
- 优化 `set_style` 重复提交：相同样式跳过复制与重复校验，保留布局缓存；
  实际变更先校验并准备新样式，分配失败不影响旧样式和已发布结果。
  参数由按值传递改为 `const Style&`／`Style&&` 重载；普通调用方式不变，
  使用成员函数指针的调用方需显式选择重载，全部使用方须重新编译。
- 修复测量回调的 `std::overflow_error`／`std::length_error` 被误转为布局错误的问题；
  所有回调异常保留类型和值向外传播，并保留上一份完整结果。
- 均衡 Flex 换行复用临时数组和连续动态规划表，保留原来的划分语义和工作量预算。
- 三方基准默认包含 `build`，默认 3 轮、每轮 9 批；增加探索性 95% 区间和不确定性标记。
- 补充默认 Flex 行布局、共享测量选择和各错误码的边界说明。

## 0.1.0 开发期迁移提示

以下接口调整已经存在于本轮修改前的 `main`，不是本轮新引入的破坏性修改：

- `mark_dirty(node)` 仅失效当前节点和祖先；依赖旧有后代失效行为的调用改用
  `mark_subtree_dirty(node)`。逐叶内容变化继续使用 `mark_dirty`。
- `NodeLayout` 的 `overflow_origin_border` 和 `overflow_coordinate_width` 已移入私有算法状态；
  宿主使用公开的 `border` 和 `scrollable_overflow_rect`。
- `compute` 第三个参数显式写 `PixelRounding{...}` 或 `LayoutOptions{...}`，避免裸 `{}` 的重载歧义。
- `Length`／`Style` 对象布局曾调整；不得混用旧头文件和新库，升级后完整重编译。

## 旧名称迁移

旧的私有开发名称为 `gridly`。头文件、命名空间、CMake 包／目标和构建选项前缀
分别改为 `axisly/`、`axisly::`、`axisly`／`axisly::axisly` 和 `AXISLY_`。
旧名称没有保留公开别名，已有宿主需要更新引用并重新构建。
