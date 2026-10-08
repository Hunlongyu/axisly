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
    {"CSS fr 1:1", "Two fr columns share the available width equally.", "CSS automatic minima can keep content from shrinking."},
    {"CSS fr 1:2:3", "Three fr columns divide available space in a 1:2:3 ratio.", "Intrinsic minimum contributions participate in CSS track sizing."},
    {"Fixed + fr", "A fixed sidebar beside two flexible columns.", "The first column stays at 180; the others share space 1:2."},
    {"CSS auto labels", "Measured labels contribute intrinsic CSS sizes.", "Shrink the window to remeasure wrapped labels at their final width."},
    {"Column span", "One header spans three columns, including their gaps.", "A span is one cell rectangle with one aligned item inside it."},
    {"Row span", "One sidebar spans two rows, including the row gap.", "Neighboring items still occupy their own independent cells."},
    {"All alignments", "Columns: horizontal S/C/E/F. Rows: vertical S/C/E/F.", "S = Start, C = Center, E = End, F = Stretch. All 16 pairs."},
    {"CSS minmax", "Columns: minmax(120px,220px), minmax(160px,1fr), 1fr.", "Below the minima the grid overflows; flexible tracks use remaining space."},
    {"Nested CSS Grid", "A CSS Grid node is nested in the same layout tree.", "The engine publishes all rectangles relative to the layout root."}
}};

struct VisualItem {
    std::string path;
    std::string label;
    css::GridItemPlacement input;
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
