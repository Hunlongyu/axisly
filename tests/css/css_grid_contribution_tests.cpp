#include <axisly/css_tree.hpp>

#include <algorithm>
#include <cmath>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
using namespace axisly;
using namespace axisly::css;
const AvailableSize available{AvailableSpace::max_content(), AvailableSpace::max_content()};

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

const GridTracks& tracks(const LayoutResult& result, NodeId node) {
    const auto found = std::find_if(result.grid_tracks.begin(), result.grid_tracks.end(),
        [node](const GridTracks& value) { return value.node == node; });
    require(found != result.grid_tracks.end(), "Expected Grid track report");
    return *found;
}

MeasureFunction words(Unit word_width, Unit count, bool vertical = false) {
    return [=](const MeasureRequest& request) {
        const auto known = vertical ? request.known_content_height : request.known_content_width;
        const auto space = vertical ? request.available_height : request.available_width;
        const Unit limit = known.value_or(space.kind == AvailableKind::MinContent ? word_width
            : space.definite_value().value_or(word_width * count));
        const Unit per_line = std::max(Unit{1}, std::floor(limit / word_width));
        const Unit line_count = std::ceil(count / per_line);
        const Unit inline_size = std::min(count, per_line) * word_width;
        const Size size = vertical ? Size{line_count * 10, inline_size} : Size{inline_size, line_count * 10};
        return Measurement{size, {}, {}, size};
    };
}

void percentage_grid_areas() {
    LayoutTree tree;
    Style style;
    style.display = Display::Grid;
    style.box.size.width = Length::length(200);
    style.grid.template_columns = {TrackSizing::automatic(), TrackSizing::automatic()};
    style.grid.template_rows = {TrackSizing::fixed(50), TrackSizing::fixed(50)};
    const auto root = tree.create_node(style);
    style = {};
    style.box.size = {Length::percent(1), Length::length(50)};
    const auto first = tree.create_node(style);
    style.grid.row.start = GridLine::index(2);
    style.grid.column.start = GridLine::index(2);
    const auto second = tree.create_node(style);
    tree.append_child(root, first);
    tree.append_child(root, second);
    const auto& result = tree.compute(root, available);
    require(result.succeeded() && tracks(result, root).columns == std::vector<Unit>{100, 100}
        && result.find(first)->border_box.width == 100 && result.find(second)->border_box.width == 100
        && result.find(root)->scrollable_overflow_rect.width == 200,
        "Cyclic item percentages must not use the entire container as their intrinsic grid area");

    LayoutTree nested;
    style = {};
    style.display = Display::Grid;
    style.box.size = {Length::percent(1), Length::length(40)};
    style.justify_items = ItemAlignment{ItemPosition::Center, AlignmentSafety::Unsafe};
    const auto outer = nested.create_node(style);
    style = {};
    style.box.size.height = Length::percent(1);
    const auto parent = nested.create_node(style);
    style.box.aspect_ratio = 1.0f;
    const auto child = nested.create_node(style);
    nested.append_child(outer, parent);
    nested.append_child(parent, child);
    const auto& reflow = nested.compute(outer, available);
    require(reflow.succeeded() && reflow.find(outer)->border_box == Rect{0, 0, 40, 40}
        && reflow.find(child)->border_box == Rect{0, 0, 40, 40},
        "Full Grid layout reflows percentage height and ratio after the first intrinsic size query");
}

void intrinsic_growth_limits() {
    for (const bool percentage : {false, true}) {
        LayoutTree tree;
        Style style;
        style.display = Display::Grid;
        style.grid.template_columns = {TrackSizing::minmax(TrackBreadth::automatic(),
            percentage ? TrackBreadth::percent(0.2f) : TrackBreadth::length(10))};
        style.grid.template_rows = {TrackSizing::fixed(40)};
        const auto root = tree.create_node(style);
        const auto child = tree.create_node({}, words(20, 2));
        tree.append_child(root, child);
        const auto& result = tree.compute(root, available);
        require(result.succeeded() && result.find(root)->border_box.width == (percentage ? 40 : 10)
            && result.find(child)->border_box.width == (percentage ? 8 : 10),
            "Fixed maximum track functions cap automatic content minima after percentage resolution");
    }
    for (const Unit limit : {Unit{10}, Unit{30}}) {
        for (const bool hidden : {false, true}) {
            LayoutTree tree;
            Style style;
            style.display = Display::Grid;
            style.grid.template_columns = {TrackSizing::min_content(), TrackSizing::fit_content_length(limit)};
            style.grid.template_rows = {TrackSizing::fixed(40)};
            const auto root = tree.create_node(style);
            style = {};
            style.grid.column.start = GridLine::index(1);
            style.grid.row.start = GridLine::index(1);
            tree.append_child(root, tree.create_node(style));
            style.grid.column.start = GridLine::index(2);
            tree.append_child(root, tree.create_node(style));
            style.grid.column = {GridLine::index(1), GridLine::span(2)};
            if (hidden) style.overflow_x = style.overflow_y = Overflow::Hidden;
            tree.append_child(root, tree.create_node(style, words(40, 2)));
            const auto& result = tree.compute(root, available);
            const Unit expected = hidden ? 40 + limit : limit == 10 ? 40 : 50;
            require(result.succeeded() && result.find(root)->border_box.width == expected,
                "Spanning max-content growth respects fit-content caps while retaining intrinsic track bases");
        }
    }
}

void baseline_track_sizes() {
    LayoutTree tree;
    Style style;
    style.display = Display::Grid;
    style.box.size = {Length::length(100), Length::length(100)};
    style.align_items = ItemAlignment{ItemPosition::Baseline, AlignmentSafety::Unsafe};
    style.grid.template_columns = {TrackSizing::automatic(), TrackSizing::automatic()};
    const auto root = tree.create_node(style);
    style = {};
    style.display = Display::Grid;
    style.box.size = {Length::length(50), Length::length(50)};
    tree.append_child(root, tree.create_node(style));
    const auto shifted = tree.create_node(style);
    tree.append_child(root, shifted);
    style.box.size.height = Length::length(10);
    tree.append_child(shifted, tree.create_node(style));
    style.box.size.height = Length::length(20);
    const auto lower = tree.create_node(style);
    tree.append_child(root, lower);
    style.box.size.height = Length::length(10);
    tree.append_child(lower, tree.create_node(style));
    style.box.size.height = Length::length(20);
    tree.append_child(root, tree.create_node(style));
    const auto& result = tree.compute(root, available);
    require(result.succeeded() && tracks(result, root).rows == std::vector<Unit>{90, 30}
        && result.find(shifted)->border_box.y == 40
        && result.find(lower)->border_box.y == 100
        && result.find(root)->scrollable_overflow_rect.height == 120,
        "Baseline shims must participate in intrinsic row sizes before final positioning");
}

void cross_axis_measurement() {
    LayoutTree tree;
    Style style;
    style.display = Display::Grid;
    style.grid.template_columns = {TrackSizing::min_content()};
    style.grid.template_rows = {TrackSizing::fixed(40)};
    const auto root = tree.create_node(style);
    const auto vertical = tree.create_node({}, words(20, 7, true));
    const auto horizontal = tree.create_node({}, words(20, 3));
    tree.append_child(root, vertical);
    tree.append_child(root, horizontal);
    const auto& result = tree.compute(root, available);
    require(result.succeeded() && result.find(root)->border_box == Rect{0, 0, 40, 60}
        && result.find(horizontal)->border_box == Rect{0, 40, 40, 20},
        "The first inline query must use definite row estimates and the next row query must see provisional columns");
}
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected test case name");
        const std::string_view name = argv[1];
        if (name == "percentage_grid_areas") percentage_grid_areas();
        else if (name == "intrinsic_growth_limits") intrinsic_growth_limits();
        else if (name == "baseline_track_sizes") baseline_track_sizes();
        else if (name == "cross_axis_measurement") cross_axis_measurement();
        else throw std::runtime_error("Unknown test case");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
