#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <axisly/axisly.hpp>

namespace axisly::playground {

struct SceneInfo { std::string_view title; std::string_view description; std::string_view hint; };
inline constexpr std::array<SceneInfo, 10> scenes{{
    {"Fixed", "Three fixed columns. Two fixed rows.", "Fixed tracks keep their size when the window shrinks."},
    {"Weighted 1:1", "Two columns share the available width equally.", "Resize the window: both columns grow at the same rate."},
    {"Weighted 1:2:3", "Three columns share width in a 1:2:3 ratio.", "Padding and gaps are removed before weights are applied."},
    {"Fixed + weighted", "A fixed sidebar beside two flexible columns.", "The first column stays at 180; the others share space 1:2."},
    {"Content labels", "Synthetic labels contribute minimum and preferred sizes.", "Shrink the window to remeasure wrapped labels at their final width."},
    {"Column span", "One header spans three columns, including their gaps.", "A span is one cell rectangle with one aligned item inside it."},
    {"Row span", "One sidebar spans two rows, including the row gap.", "Neighboring items still occupy their own independent cells."},
    {"All alignments", "Columns: horizontal S/C/E/F. Rows: vertical S/C/E/F.", "S = Start, C = Center, E = End, F = Stretch. All 16 pairs."},
    {"Range limits", "Column 0: 120..220. Column 1: 160..weighted. Column 2: 1fr.", "Below the minima the grid overflows; above the cap space redistributes."},
    {"Nested grid", "A host-owned child grid is arranged inside its final parent rectangle.", "Paths scope IDs. Child coordinates are translated into the root canvas."}
}};

struct VisualItem {
    std::string path;
    std::string label;
    GridItem input;
    Rect rect;
    Rect cell;
    Size preferred;
    std::size_t depth{};
    bool has_preferred{true};
};

struct Frame {
    Size available;
    Size occupied;
    std::vector<VisualItem> items;
    std::vector<Unit> columns;
    std::vector<Unit> rows;
    std::vector<std::string> column_rules;
    std::vector<std::string> row_rules;
    std::size_t measurements{};
    std::size_t layouts{};
    std::string error;
    [[nodiscard]] bool succeeded() const noexcept { return error.empty(); }
};

// Host-only scene construction and metadata. Uses installed public APIs only.
[[nodiscard]] Frame arrange_scene(std::size_t index, Size available);

} // namespace axisly::playground
