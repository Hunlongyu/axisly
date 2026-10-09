<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/brand/axisly-logo-dark.svg">
    <source media="(prefers-color-scheme: light)" srcset="docs/brand/axisly-logo.svg">
    <img src="docs/brand/axisly-logo.svg" alt="Axisly" width="360">
  </picture>
</p>

<p align="center">
  <a href="README.md">English</a> · <strong>简体中文</strong>
</p>

<p align="center">
  <strong>面向原生 UI 的 C++20 布局库</strong><br>
  Block · Flexbox · Grid
</p>

<p align="center">
  <a href="CHANGELOG.md"><img src="https://img.shields.io/badge/version-0.2.0%20dev-087F73?style=flat&amp;labelColor=263B3A" alt="开发版本 0.2.0"></a>
  <a href="CMakeLists.txt"><img src="https://img.shields.io/badge/C%2B%2B-20-087F73?style=flat&amp;labelColor=263B3A" alt="C++20"></a>
  <a href="docs/building.md"><img src="https://img.shields.io/badge/CMake-3.25%2B-087F73?style=flat&amp;labelColor=263B3A" alt="CMake 3.25 及以上"></a>
  <a href="CMakeLists.txt"><img src="https://img.shields.io/badge/core-STL%20only-087F73?style=flat&amp;labelColor=263B3A" alt="核心仅依赖 C++ 标准库"></a>
  <a href="https://github.com/Hunlongyu/axisly/actions/workflows/ci.yml"><img src="https://img.shields.io/badge/CI-workflow-607D78?style=flat&amp;labelColor=263B3A" alt="查看 CI 工作流"></a>
</p>

<p align="center">
  <a href="#快速开始">快速开始</a> ·
  <a href="docs/css-layout.md">布局指南</a> ·
  <a href="#示例">示例</a> ·
  <a href="benchmarks/comparison/README.md">性能基准</a> ·
  <a href="CHANGELOG.md">变更记录</a>
</p>

---

Axisly 将**布局树、样式和内容尺寸**转换为控件的**位置与大小**。
宿主提供窗口、控件、文字测量与绘制，Axisly 负责布局计算，可接入现有 UI 框架或自绘界面。

## 核心特点

- **框架无关**：核心仅使用 C++ 标准库，以静态库交付，普通构建无需下载第三方依赖。
- **布局组合**：Block、Flexbox 和 Grid 可以嵌套在同一棵树中，共用尺寸、测量与对齐规则。
- **增量更新**：保留布局树，通过脏标记、测量缓存与布局缓存复用未变化的结果。
- **接入灵活**：既可由 Axisly 管理布局树，也可借用宿主已有的树结构；支持按节点查询、内存复用与独立树批量计算。

<a id="功能"></a>

## 布局能力

| 布局 | 主要能力 |
| --- | --- |
| **Block / FlowRoot** | 块级流、浮动与 clear、独立格式化上下文 |
| **Flexbox** | 行列方向、伸缩、换行、反向排列、基线对齐与均衡换行 |
| **Grid** | 显式与隐式轨道、自动放置、跨格、具名线、模板区域、minmax 与自动重复 |

三种布局共用固定尺寸、百分比、min/max-content、类型化 calc、盒模型与纵横比约束，
并提供相对／绝对定位、双向对齐、滚动范围、滚动条空间预留和可选设备像素取整。
具体字段及行为边界见 [布局指南](docs/css-layout.md)和[兼容性说明](docs/css-compatibility.md)。

## 快速开始

需要支持 **C++20** 的编译器和 **CMake 3.25+**。

<a id="接入项目"></a>

### 1. 接入 CMake

将源码放入项目，例如 `third_party/axisly`，然后链接导出目标：

```cmake
add_subdirectory(third_party/axisly)
target_link_libraries(your_app PRIVATE axisly::axisly)
```

作为子项目引入时，测试和示例默认关闭。

<details>
<summary>使用已安装的 CMake 包</summary>

将安装位置加入 `CMAKE_PREFIX_PATH`，再链接同一个目标：

```cmake
find_package(axisly CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE axisly::axisly)
```

完整安装步骤见 [构建指南](docs/building.md#安装与消费)。

</details>

### 2. 描述布局并计算

`Style::display` 默认是 `Flex`，默认主轴为横向；需要块布局时显式设置 `Display::Block`。

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

输出的每一行依次是 `x y width height`：

```text
0 0 156 24
164 0 156 24
```

### 3. 应用到宿主界面

- 将返回的矩形应用到对应控件；示例中的坐标相对布局根。
- 文字、图片等内容通过 `create_node(style, measure)` 提供测量回调。
- 内容变化后调用 `mark_dirty(node)`；样式变化通过 `set_style(node, style)` 自动失效。

计算结果由树持有，后续计算会更新它；需要跨帧保留时复制结果。

完整测量约定、错误处理与宿主接入见 [CSS 布局指南](docs/css-layout.md)。

## 选择接入方式

| 接入方式 | 适用场景 | 使用的接口 |
| --- | --- | --- |
| **由 Axisly 管理布局树** | 希望由库统一管理节点、样式、缓存与增量失效 | [`LayoutTree`](include/axisly/css_tree.hpp) |
| **借用宿主已有树结构** | 已有控件树和样式系统，希望直接接入布局计算 | [`LayoutSource` / `LayoutSession`](include/axisly/css_source.hpp) |

两种接入方式使用同一套 CSS Block／Flexbox／Grid 语义。Grid 统一使用 `Style::grid`、
`TrackSizing` 和 `GridLine`；嵌套 Grid 直接作为同一布局树中的子节点。
布局树与样式接口位于 `axisly::css` 命名空间，可分别包含所需头文件，也可使用总入口 `<axisly/axisly.hpp>`。

## 示例

| 示例 | 可以了解什么 | 入口 |
| --- | --- | --- |
| **控制台示例** | 轨道、测量、跨度、对齐、嵌套、缓存与取整 | [源码](examples/css_grid.cpp) |
| **SDL3 Playground** | 调整画布尺寸，查看网格线、条目边框和最终矩形 | [运行方式](docs/examples.md#sdl3-playground) |
| **GeeyoouUI Showcase** | Windows 控件宿主接入，覆盖表单、表格、主题等 19 个页面 | [运行方式](docs/examples.md#geeyoouui-showcasewindows) |

图形示例是可选开发目标，其 UI 和渲染依赖不会进入核心库。
接入其他框架时，可按[宿主接入步骤](docs/examples.md#接入自己的-ui)完成测量、失效与矩形应用。

## 范围与兼容性

CSS 布局以 **Taffy 0.14.0** 为版本兼容参照，使用固定用例、组合测试和上游语料做差分验证。
已知的 Taffy 与浏览器规范差异单独记录在 [兼容性说明](docs/css-compatibility.md) 中。

库接收类型化 C++ 样式，不包含 CSS 字符串解析、选择器、级联、DOM、文字塑形或绘制。
完整浏览器排版中的内联格式化、Table 布局、垂直书写与分页等也不属于当前实现范围。
滚动范围是布局输出，滚动交互和裁剪由宿主实现。

> 当前开发版本为 **0.2.0**，暂不承诺跨版本源码或二进制兼容。
> 升级时请重新编译所有使用方，并核对 [变更与迁移记录](CHANGELOG.md)。

## 文档与示例

| 文档 | 适合查阅的内容 |
| --- | --- |
| [构建指南](docs/building.md) | 编译、安装、CMake 选项 |
| [CSS 布局指南](docs/css-layout.md) | 样式、测量、更新和结果生命周期 |
| [执行资源](docs/css-execution-resources.md) | 内存复用、批量执行、取整与 PGO |
| [兼容性说明](docs/css-compatibility.md) | 布局字段映射和规范边界 |
| [规范差异记录](docs/css-conformance.md) | 独立规范用例、浏览器重放与已知差异 |
| [示例](docs/examples.md) | 控制台、SDL3 和 GeeyoouUI |
| [测试指南](docs/testing.md) | 契约测试、上游重放与 Sanitizer |
| [性能基准](benchmarks/comparison/README.md) | 同输入的 Axisly／Taffy／Yoga 对比方法 |

核心源码位于 [src](src/)，公共接口位于 [include/axisly](include/axisly/)。
参考实现、基准工具与图形示例均为可选开发依赖。
