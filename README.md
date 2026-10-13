<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/brand/axisly-logo-dark.svg">
    <source media="(prefers-color-scheme: light)" srcset="docs/brand/axisly-logo.svg">
    <img src="docs/brand/axisly-logo.svg" alt="Axisly" width="360">
  </picture>
</p>

<p align="center">
  <strong>English</strong> · <a href="README.zh-CN.md">简体中文</a>
</p>

<p align="center">
  <strong>A C++20 layout library for native UI</strong><br>
  Block · Flexbox · Grid
</p>

<p align="center">
  <a href="https://github.com/Hunlongyu/axisly/releases/tag/v0.2.0"><img src="https://img.shields.io/badge/version-0.2.0-087F73?style=flat&amp;labelColor=263B3A" alt="Version 0.2.0"></a>
  <a href="CMakeLists.txt"><img src="https://img.shields.io/badge/C%2B%2B-20-087F73?style=flat&amp;labelColor=263B3A" alt="C++20"></a>
  <a href="docs/building.md"><img src="https://img.shields.io/badge/CMake-3.25%2B-087F73?style=flat&amp;labelColor=263B3A" alt="CMake 3.25 or newer"></a>
  <a href="CMakeLists.txt"><img src="https://img.shields.io/badge/core-STL%20only-087F73?style=flat&amp;labelColor=263B3A" alt="Core uses only the C++ standard library"></a>
  <a href="https://github.com/Hunlongyu/axisly/actions/workflows/ci.yml"><img src="https://img.shields.io/badge/CI-workflow-607D78?style=flat&amp;labelColor=263B3A" alt="View CI workflow"></a>
</p>

<p align="center">
  <a href="#quick-start">Quick start</a> ·
  <a href="docs/css-layout.md">Layout guide</a> ·
  <a href="#examples">Examples</a> ·
  <a href="benchmarks/comparison/README.md">Benchmarks</a> ·
  <a href="CHANGELOG.md">Changelog</a>
</p>

---

Axisly turns **layout trees, styles, and content measurements** into **widget positions and sizes**.
Your host provides windows, widgets, text measurement, and rendering. Axisly computes the geometry
for an existing UI framework or a custom interface.

## Core features

- **Framework independent**: the core uses only the C++ standard library and ships as a static library. A normal build downloads no third-party dependencies.
- **Composable layouts**: nest Block, Flexbox, and Grid in one tree with shared sizing, measurement, and alignment rules.
- **Incremental updates**: retain the tree and reuse unchanged results through dirty tracking and measurement and layout caches.
- **Flexible integration**: let Axisly manage the tree or adapt an existing host tree, with per-node queries, reusable memory, and batch computation of independent trees.

## Layout capabilities

| Layout | Capabilities |
| --- | --- |
| **Block / FlowRoot** | Block flow, floats and clear, independent formatting contexts |
| **Flexbox** | Rows and columns, grow and shrink, wrapping, reverse directions, baseline alignment, and balanced wrapping |
| **Grid** | Explicit and implicit tracks, auto-placement, spans, named lines, template areas, minmax, and automatic repetition |

All three layouts share fixed and percentage sizes, min/max-content sizing, typed calc values,
box-model and aspect-ratio constraints, relative and absolute positioning, bidirectional alignment,
scroll extents, reserved scrollbar space, and optional device-pixel rounding.
See the [layout guide](docs/css-layout.md) and [compatibility matrix](docs/css-compatibility.md)
for individual fields and behavior limits.

## Quick start

Requires a **C++20** compiler and **CMake 3.25+**.

Download the source ZIP or tar.gz from [GitHub Releases](https://github.com/Hunlongyu/axisly/releases/tag/v0.2.0).
Each release includes SHA-256 checksums. Build the library with the compiler and configuration used by your application.

### 1. Add Axisly to your CMake project

Place the source in your project, for example at `third_party/axisly`, and link the exported target:

```cmake
add_subdirectory(third_party/axisly)
target_link_libraries(your_app PRIVATE axisly::axisly)
```

Tests and examples are disabled by default when Axisly is included as a subproject.

<details>
<summary>Use an installed CMake package</summary>

Add the installation prefix to `CMAKE_PREFIX_PATH` and link the same target:

```cmake
find_package(axisly CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE axisly::axisly)
```

See the [build guide](docs/building.md#安装与消费) for installation steps.

</details>

### 2. Describe and compute a layout

`Style::display` defaults to `Flex` with a horizontal main axis. Set `Display::Block` explicitly for block layout.

This example creates two flexible items in a row, in a container of width 320 with a gap of 8:

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

Each output line contains `x y width height`:

```text
0 0 156 24
164 0 156 24
```

### 3. Apply the result to your UI

- Apply each returned rectangle to its widget. Coordinates in the example are relative to the layout root.
- Supply a measurement callback through `create_node(style, measure)` for text, images, and other content.
- Call `mark_dirty(node)` after content changes. `set_style(node, style)` automatically invalidates affected cached results.

The tree owns the result, and subsequent computations update it. Copy results that need to survive across frames.

See the [CSS layout guide](docs/css-layout.md) for measurement contracts, error handling, and host integration.

## Choose an integration

| Approach | When to use it | API |
| --- | --- | --- |
| **Axisly owns the layout tree** | Let the library manage nodes, styles, caches, and incremental invalidation | [`LayoutTree`](include/axisly/css_tree.hpp) |
| **Adapt an existing host tree** | Connect an existing widget tree and style system to the layout engine | [`LayoutSource` / `LayoutSession`](include/axisly/css_source.hpp) |

Both approaches share the same CSS Block, Flexbox, and Grid semantics. Grid uses `Style::grid`,
`TrackSizing`, and `GridLine`; nested grids are ordinary child nodes in the same tree.
Layout-tree and style APIs live in `axisly::css`. Include individual headers as needed,
or use the umbrella header `<axisly/axisly.hpp>`.

## Examples

| Example | What it demonstrates | Entry point |
| --- | --- | --- |
| **Console example** | Tracks, measurement, spans, alignment, nesting, caching, and rounding | [Source](examples/css_grid.cpp) |
| **SDL3 Playground** | Resize the canvas and inspect grid lines, item borders, and final rectangles | [Run guide](docs/examples.md#sdl3-playground) |
| **GeeyoouUI Showcase** | Windows host integration across 19 pages, including forms, tables, and themes | [Run guide](docs/examples.md#geeyoouui-showcasewindows) |

Graphical examples are optional development targets; their UI and rendering dependencies stay outside the core library.
Follow the [host integration steps](docs/examples.md#接入自己的-ui) to connect measurement, invalidation,
and rectangle application in another framework.

## Scope and compatibility

CSS layout uses **Taffy 0.14.0** as its versioned compatibility reference, with differential checks
against fixed cases, property combinations, and the upstream corpus.
Known differences between Taffy and browser specifications are documented in the
[compatibility guide](docs/css-compatibility.md).

The library accepts typed C++ styles. CSS string parsing, selectors, the cascade, DOM, text shaping,
and rendering are outside its scope, as are browser features such as inline formatting,
table layout, vertical writing, and pagination. Scroll extents are layout output;
the host implements scrolling and clipping.

> The current release is **0.2.0**. Source and binary compatibility across versions is not guaranteed.
> Rebuild all consumers when upgrading and review the [changelog and migration notes](CHANGELOG.md).

## Documentation

Detailed guides and the changelog are currently available in Simplified Chinese.

| Guide | Contents |
| --- | --- |
| [Build guide](docs/building.md) | Compilation, installation, and CMake options |
| [CSS layout guide](docs/css-layout.md) | Styles, measurement, updates, and result lifetime |
| [Execution resources](docs/css-execution-resources.md) | Memory reuse, batch execution, rounding, and PGO |
| [Compatibility matrix](docs/css-compatibility.md) | Layout field mapping and scope |
| [Specification differences](docs/css-conformance.md) | Independent specification cases, browser replays, and known differences |
| [Examples](docs/examples.md) | Console, SDL3, and GeeyoouUI examples |
| [Testing](docs/testing.md) | Contract tests, upstream replay, and sanitizers |
| [Benchmarks](benchmarks/comparison/README.md) | Comparing Axisly, Taffy, and Yoga with identical input |

Core implementation lives in [src](src/) and public interfaces in [include/axisly](include/axisly/).
Reference implementations, benchmark tools, and graphical examples are optional development dependencies.
