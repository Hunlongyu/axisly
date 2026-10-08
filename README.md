# Axisly

面向原生 UI 的 C++20 布局库。输入布局树、样式和内容尺寸，输出控件的位置与大小。

Axisly 提供 Block、Flexbox 和 Grid 布局，核心仅依赖 C++ 标准库。
窗口、控件、文字测量和绘制由宿主负责，可接入现有 UI 框架或自绘界面。

## 功能

- **混合布局**：Block／FlowRoot、Flexbox、CSS Grid 可以在同一棵树中嵌套。
- **尺寸与排版**：固定尺寸、百分比、min/max-content、类型化 calc、盒模型、纵横比、基线和双向对齐。
- **Flexbox**：伸缩、换行、反向排列、间隙和均衡换行。
- **Grid**：显式与隐式轨道、自动放置、跨格、具名线、模板区域、minmax 与自动重复。
- **位置与溢出**：相对／绝对定位、浮动与 clear、滚动范围和滚动条空间预留。
- **持续更新**：保留布局树、脏标记、测量与布局缓存、按节点读取结果，以及可选设备像素取整。
- **宿主接入**：可直接借用宿主的样式和树结构，共用测量器；支持父相对坐标及按需输出。
- **执行资源**：可选内存复用，以及多棵独立布局树的批量计算。

## 接入项目

需要支持 **C++20** 的编译器和 **CMake 3.25+**。库以静态库形式构建，默认不下载第三方依赖。

将源码放入项目，例如 `third_party/axisly`：

```cmake
add_subdirectory(third_party/axisly)
target_link_libraries(your_app PRIVATE axisly::axisly)
```

作为子项目引入时，测试和示例默认关闭。也可以安装后使用 CMake 包：

```cmake
find_package(axisly CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE axisly::axisly)
```

安装步骤、编译选项和各平台命令见 [构建指南](docs/building.md)。

## 快速开始

下面创建一行两个可伸展的元素，容器宽 320，元素间距为 8：

```cpp
#include <axisly/css_tree.hpp>
#include <cstdio>

int main() {
    namespace css = axisly::css;
    css::LayoutTree tree;

    css::Style row;
    row.display = css::Display::Flex;
    row.box.size = {css::Length::length(320), css::Length::length(48)};
    row.gap.width = css::Length::length(8);
    const auto root = tree.create_node(row);

    css::Style item;
    item.box.size = {css::Length::length(80), css::Length::length(24)};
    item.flex_grow = 1;
    tree.append_child(root, tree.create_node(item));
    tree.append_child(root, tree.create_node(item));

    const auto& result = tree.compute(root, {
        css::AvailableSpace::definite(320),
        css::AvailableSpace::definite(48)});
    if (!result.succeeded()) return 1;

    for (const auto& node : result.nodes) {
        if (node.node == root) continue;
        const auto& r = node.border_box;
        std::printf("%.0f %.0f %.0f %.0f\n", r.x, r.y, r.width, r.height);
    }
}
```

输出为 `0 0 156 24` 和 `164 0 156 24`。将这些相对布局根的矩形应用到宿主控件即可。

文字、图片等内容通过 `create_node(style, measure)` 提供测量回调。
内容变化后调用 `mark_dirty(node)`；样式变化通过 `set_style(node, style)` 自动失效。
计算结果由树持有，后续计算会更新它；需要跨帧保留时复制结果。

完整测量约定、错误处理与宿主接入见 [CSS 布局指南](docs/css-layout.md)。

## 选择布局接口

| 接口 | 适用场景 | 特点 |
| --- | --- | --- |
| `axisly::css::LayoutTree` | 使用 CSS 布局规则的界面 | 支持混合树、内容测量、缓存与增量失效 |
| `axisly::compute_grid()` | 直接给定行列、条目和可用空间 | 无状态计算，采用原生 Grid 的轨道与约束规则 |
| `axisly::GridCache`／`GridTree` | 复用原生 Grid 布局 | 缓存、测量失效与嵌套网格 |

原生 Grid 与 CSS Grid 的尺寸分配语义不同，详见 [原生 Grid 指南](docs/native-grid.md)。
公共头文件可以分别包含，也可以使用总入口 `<axisly/axisly.hpp>`。

## 范围与兼容性

CSS 布局以 **Taffy 0.14.0** 为版本兼容参照，使用固定用例、组合测试和上游语料做差分验证。
已知的 Taffy 与浏览器规范差异单独记录在 [兼容性说明](docs/css-compatibility.md) 中。

库接收类型化 C++ 样式，不包含 CSS 字符串解析、选择器、级联、DOM、文字塑形或绘制。
完整浏览器排版中的内联格式化、Table 布局、垂直书写与分页等也不属于当前实现范围。
滚动范围是布局输出，滚动交互和裁剪由宿主实现。

当前版本为 `0.1.0`，处于开发阶段，暂不承诺跨版本二进制兼容。

## 文档与示例

| 文档 | 内容 |
| --- | --- |
| [构建指南](docs/building.md) | 编译、安装、CMake 选项 |
| [CSS 布局指南](docs/css-layout.md) | 样式、测量、更新和结果生命周期 |
| [原生 Grid 指南](docs/native-grid.md) | 轨道、对齐、缓存与嵌套 |
| [执行资源](docs/css-execution-resources.md) | 内存复用、批量执行、取整与 PGO |
| [兼容性说明](docs/css-compatibility.md) | 布局字段映射和规范边界 |
| [示例](docs/examples.md) | 控制台、SDL3 和 GeeyoouUI |
| [测试指南](docs/testing.md) | 契约测试、上游重放与 Sanitizer |
| [性能基准](benchmarks/comparison/README.md) | 同输入的 Axisly／Taffy／Yoga 对比方法 |

核心源码位于 [src](src/)，公共接口位于 [include/axisly](include/axisly/)。
参考实现、基准工具与图形示例均为可选开发依赖。
