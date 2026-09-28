# M3 对齐、边距与示例启动

本文记录 M3 的 `0.0.4` 验收快照。M3 在已解析的固定／权重单元格内完成条目布局，不改变轨道分配规则。当前版本的测量回调与 Content 支持见 [M4 文档](12-measurement-and-content.md)。

## 首选尺寸与结果

```cpp
GridItem item{
    .id = 7,
    .margin = {10, 5, 20, 15},
    .horizontal_alignment = HorizontalAlignment::Center,
    .vertical_alignment = VerticalAlignment::End,
    .preferred_size = {50, 20}
};
```

M3 的验收前提是已知单元格和条目首选尺寸。现有接口尚无测量入口，因此增加静态 `GridItem::preferred_size`，默认 `{0, 0}`；M4 再实现测量回调和 Content 轨道。

`ItemLayout::cell_rect` 是轨道分配的原始单元格，`rect` 是应用边距、首选尺寸和对齐后的结果。两者都只包含几何值，不持有宿主 UI 对象。添加这些字段后，消费工程须同步头文件并重新编译，预览版本不保证旧二进制兼容。

## 每个轴的规则

横纵两轴独立执行。以下用水平方向说明：

```text
leading   = min(cell.width, margin.left)
inner_x   = cell.x + leading
inner_w   = max(0, cell.width - leading - margin.right)

Stretch: width = inner_w
其他:    width = min(preferred_size.width, inner_w)

Start / Stretch: x = inner_x
Center:          x = inner_x + (inner_w - width) / 2
End:             x = inner_x + inner_w - width
```

- Stretch 不使用该轴的首选尺寸，填满边距内的空间。
- Start／Center／End 使用首选尺寸，过大时缩小到边距内的空间。
- 未提供首选尺寸时，非 Stretch 轴的尺寸为零，仍按对应位置对齐。
- 超大 margin 不产生负尺寸。先消耗起始边距，起点最多移动到单元格末端；空间用尽后，各种对齐都得到同一个零尺寸位置。
- margin 必须非负，不支持负边距溢出。条目在单元格内布局；单元格本身仍可能按照 M1／M2 规则超出容器。
- margin 和 preferred_size 不增加固定／权重轨道尺寸。`GridResult::size` 继续表示网格、gap 和 padding 的占用范围。

例如单元格 `{10, 20, 100, 80}`，首选尺寸 `{30, 10}`、无边距时：

| 对齐（两轴相同） | 最终 rect |
| --- | --- |
| Start | `{10, 20, 30, 10}` |
| Center | `{45, 55, 30, 10}` |
| End | `{80, 90, 30, 10}` |
| Stretch | `{10, 20, 100, 80}` |

每一轴可以单独选择以上任意规则，共 16 种组合。

## 校验与数值边界

preferred_size 的两个分量必须有限且非负，即使条目使用 Stretch 也会校验；非法值返回新增的 `InvalidPreferredSize` 和相应 item_id。负数／NaN／无穷 margin、无效 alignment 枚举继续分别返回 `InvalidMargin`、`InvalidAlignment`。

所有条目先完成输入校验再计算，失败时仍返回空 items 和零尺寸。重复 ID、无效放置、非法 span 的规则保持不变。Content／Range 轨道和大于 1 的有效 span 暂未实现，继续返回 `UnsupportedFeature`。

内部轨道保留 `double` 精度到对齐完成，再转为公共 `Unit`。超大 margin 在加法前受单元格约束，不会因边距之和溢出而生成无穷。最大有限坐标的最终转换处理累计舍入误差，次正规尺寸不会被主动取整。公共结果仍受 float 表示精度影响，不提供像素舍入功能。

## 控制台示例

这三个 example 打印布局结果，尚不是 SDL3 图形演示：

- `gridly_api_example`：固定布局。
- `gridly_weighted_example`：三个容器宽度下的固定／权重布局。
- `gridly_alignment_example`：四种对齐的 cell_rect 和 rect。

此前双击运行后窗口立即关闭，是输出完成后退出；本机检查旧固定／权重示例均返回退出码 0，未复现崩溃。

M3 增加仅用于示例的 `examples/console_session.hpp`：Windows 上检测到程序独占交互式控制台时，打印结束后等待回车；已有终端、输入／输出重定向及非 Windows 运行按正常命令行方式结束。库本身仍不依赖 Win32。成功和普通错误返回路径都能保留独立控制台中的输出。

## 验证记录

2026-09-24，Windows x64，MSVC 19.51.36257，CMake 4.4.2，Ninja：

| 验证项 | 结果 |
| --- | --- |
| Debug 构建与测试 | 无编译告警，72/72 通过 |
| Release 构建与测试 | 无编译告警，72/72 通过 |
| 16 种横纵对齐组合 | 黄金矩形全部通过 |
| 随机条目边界约束 | 500 组固定种子用例通过 |
| 原 M2 独立求解对比 | 500 组用例继续通过 |
| 7 个公共头文件分别编译 | 两种配置均通过 |
| 安装包／子目录接入 | 分别 1/1 通过，含真实 margin／alignment／cell_rect 调用 |
| 对齐示例在命令行运行 | 输出 4 个单元格与最终矩形，退出码 0 |
| 三个 Release 示例独立控制台启动 | 隐藏窗口中确认输出及回车提示；输入回车后均以 0 退出 |

新增的 22 项对齐测试还覆盖非对称和超大边距、超大／零首选尺寸、零单元格、小数、最大有限／次正规几何、权重缩放后的条目位置、非法输入和输入不变性。独立控制台验证只操作本次创建的隐藏子进程，并在验证后关闭，不留下运行中的示例。

上述验证不构成 SDL3 图形界面、真实宿主控件或跨平台运行验证。下一阶段是 M4 测量回调和 Content 轨道。
