#include <axisly/css_tree.hpp>

#include <array>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <tuple>

namespace {
using namespace axisly;
using namespace axisly::css;
const AvailableSize intrinsic{AvailableSpace::max_content(), AvailableSpace::max_content()};

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
bool near(Unit a, Unit b) { return std::isfinite(a) && std::abs(a - b) < .01f; }
Length calc(Unit fixed, Unit fraction) {
    return Length::calc(Calculation::add(Calculation::length(fixed), Calculation::percent(fraction)));
}
MeasureFunction raw_content(Size size) {
    return [size](const MeasureRequest&) { return Measurement{size, {}, {}, size}; };
}
bool same_geometry(const LayoutResult& a, const LayoutResult& b) {
    return a.size == b.size && a.nodes.size() == b.nodes.size()
        && a.grid_tracks.size() == b.grid_tracks.size()
        && std::equal(a.nodes.begin(), a.nodes.end(), b.nodes.begin(), [](const auto& x, const auto& y) {
            return std::tie(x.node, x.parent, x.border_box, x.content_box, x.margin, x.padding,
                x.border, x.overflow_origin_border, x.scrollbar, x.first_baseline, x.last_baseline,
                x.scrollable_overflow_rect, x.overflow_coordinate_width)
                == std::tie(y.node, y.parent, y.border_box, y.content_box, y.margin, y.padding,
                    y.border, y.overflow_origin_border, y.scrollbar, y.first_baseline, y.last_baseline,
                    y.scrollable_overflow_rect, y.overflow_coordinate_width);
        })
        && std::equal(a.grid_tracks.begin(), a.grid_tracks.end(), b.grid_tracks.begin(), [](const auto& x, const auto& y) {
            return std::tie(x.node, x.row_origin, x.column_origin, x.explicit_rows, x.explicit_columns,
                x.rows, x.columns, x.row_positions, x.column_positions, x.row_line_names, x.column_line_names, x.items)
                == std::tie(y.node, y.row_origin, y.column_origin, y.explicit_rows, y.explicit_columns,
                    y.rows, y.columns, y.row_positions, y.column_positions, y.row_line_names, y.column_line_names, y.items);
        });
}

void late_flex_item_basis() {
    LayoutTree tree;
    Style root_style;
    root_style.gap.width = calc(3, .1f);
    const auto root = tree.create_node(root_style);
    Style block;
    block.display = Display::Block;
    block.box.padding.left = calc(2, .02f);
    block.box.padding.top = calc(1, .01f);
    block.box.border.left = Length::calc(Calculation::length(1));
    const auto parent = tree.create_node(block);
    tree.append_child(root, parent);
    block.box = {};
    block.box.size = {calc(10, .5f), calc(15, .25f)};
    block.box.margin.left = calc(2, .01f);
    const auto child = tree.create_node(block);
    tree.append_child(parent, child);
    block.box.size = {calc(10, .6f), calc(10, 0)};
    block.box.margin = {};
    block.box.aspect_ratio = 2;
    const auto leaf = tree.create_node(block, raw_content({70, 20}));
    tree.append_child(child, leaf);
    block.box = {};
    block.box.size = {calc(10, .05f), calc(8, 0)};
    const auto tail = tree.create_node(block, raw_content({10, 8}));
    tree.append_child(root, tail);
    const auto result = tree.compute(root, intrinsic);
    // Intrinsic widths are 70 + 10 + 3. The selected 83px main size
    // supplies the Block's flow edges, but its collected item edges are zero.
    require(result.succeeded() && near(result.size.width, 83)
        && near(result.size.height, 19.631f)
        && result.find(parent)->padding == Insets{}
        && result.find(parent)->border == Insets{}
        && near(result.find(child)->border_box.x, 7.3134f)
        && near(result.find(child)->border_box.y, 1.83f)
        && near(result.find(child)->border_box.width, 42.67f)
        && near(result.find(leaf)->border_box.width, 35.602f),
        "Flex late main basis must be independent of collected item edges");
}

void intrinsic_known_height() {
    LayoutTree tree;
    Style grid;
    grid.display = Display::Grid;
    grid.box.size = {Length::length(160), Length::length(35)};
    const auto root = tree.create_node(grid);
    Style block;
    block.display = Display::Block;
    block.box.size = {Length::min_content(), calc(15, .25f)};
    const auto parent = tree.create_node(block);
    tree.append_child(root, parent);
    block.box.size = {Length::automatic(), Length::calc(Calculation::maximum(
        Calculation::length(0), Calculation::subtract(Calculation::length(15),
            Calculation::percent(.05f))))};
    block.box.aspect_ratio = 2;
    const auto leaf = tree.create_node(block, raw_content({70, 20}));
    tree.append_child(parent, leaf);
    const auto& result = tree.compute(root, intrinsic);
    // The known 23.75px height resolves the leaf height to 13.8125px;
    // the ratio supplies 27.625px before the intrinsic width query.
    require(result.succeeded() && near(result.find(parent)->border_box.width, 27.625f)
        && near(result.find(parent)->border_box.height, 23.75f)
        && near(result.find(leaf)->border_box.width, 27.625f)
        && near(result.find(leaf)->border_box.height, 13.8125f),
        "Block intrinsic width must preserve independently known child dimensions");
}

void query_stage_cache() {
    LayoutTree tree;
    Style block;
    block.display = Display::Block;
    const auto root = tree.create_node(block);
    block.box.padding.left = calc(2, .02f);
    block.box.padding.top = calc(1, .01f);
    block.box.border.left = Length::calc(Calculation::length(1));
    const auto parent = tree.create_node(block);
    tree.append_child(root, parent);
    Style grid;
    grid.display = Display::Grid;
    grid.box.size = {calc(10, .5f), calc(15, .25f)};
    grid.box.margin.left = calc(2, .01f);
    const auto child = tree.create_node(grid);
    tree.append_child(parent, child);
    block.box = {};
    block.box.size.width = calc(30, .75f);
    block.box.aspect_ratio = 2;
    const auto leaf = tree.create_node(block, raw_content({70, 20}));
    tree.append_child(child, leaf);
    block.box = {};
    block.box.size = {calc(10, .05f), calc(8, 0)};
    const auto tail = tree.create_node(block, raw_content({10, 8}));
    tree.append_child(root, tail);
    const auto initial = tree.compute(root, intrinsic);
    require(initial.succeeded() && near(initial.size.width, 70)
        && near(initial.size.height, 55.6375f)
        && near(initial.find(child)->border_box.width, 42.8f)
        && near(initial.find(leaf)->border_box.width, 91.875f),
        "Grid full reflow must not replace an earlier size-query result");
    require(tree.compute(root, intrinsic).cache_hit, "Unchanged calc tree must hit its root cache");
    tree.mark_dirty(leaf);
    const auto& refreshed = tree.compute(root, intrinsic);
    require(refreshed.succeeded() && same_geometry(refreshed, initial),
        "Warm caches and leaf invalidation must preserve query-stage geometry");
}

void numeric_recovery() {
    for (const auto display : {Display::Block, Display::Flex, Display::Grid}) {
        LayoutTree tree;
        Style style;
        style.display = display;
        style.box.size = {Length::length(100), Length::length(40)};
        const auto root = tree.create_node(style);
        Style leaf_style;
        leaf_style.box.size = {Length::length(20), Length::length(10)};
        const auto leaf = tree.create_node(leaf_style, raw_content({20, 10}));
        tree.append_child(root, leaf);
        const auto valid = tree.compute(root, intrinsic);
        require(valid.succeeded(), "Initial recovery tree must succeed");
        auto overflow = style;
        overflow.box.padding.left = calc(std::numeric_limits<Unit>::max(), 0);
        overflow.box.padding.right = calc(std::numeric_limits<Unit>::max(), 0);
        tree.set_style(root, overflow);
        const auto& failed = tree.compute(root, {AvailableSpace::definite(100), AvailableSpace::definite(40)});
        require(failed.error.code == LayoutErrorCode::NumericOverflow && failed.nodes.empty()
            && failed.grid_tracks.empty(), "Finite inputs whose sums overflow must publish no partial result");
        tree.set_style(root, style);
        const auto& restored = tree.compute(root, intrinsic);
        require(restored.succeeded() && same_geometry(restored, valid),
            "A rejected extreme tree must not corrupt subsequent layout or caches");
        leaf_style.box.size = {calc(0, 0), calc(0, 0)};
        tree.set_style(leaf, leaf_style);
        const auto& zero = tree.compute(root, {AvailableSpace::definite(0), AvailableSpace::definite(0)});
        require(zero.succeeded() && zero.find(leaf)->border_box.width == 0
            && zero.find(leaf)->border_box.height == 0,
            "Zero available space is a definite calc basis");
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected calc test name");
        const std::string_view name = argv[1];
        if (name == "late_flex_item_basis") late_flex_item_basis();
        else if (name == "intrinsic_known_height") intrinsic_known_height();
        else if (name == "query_stage_cache") query_stage_cache();
        else if (name == "numeric_recovery") numeric_recovery();
        else throw std::runtime_error("Unknown calc test name");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
