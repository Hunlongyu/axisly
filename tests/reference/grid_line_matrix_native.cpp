#include "css_reference_output.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace axisly;
using namespace axisly::css;

GridLine line(std::size_t index) {
    switch (index) {
    case 0: return GridLine::automatic();
    case 1: return GridLine::index(1);
    case 2: return GridLine::index(2);
    case 3: return GridLine::index(-1);
    case 4: return GridLine::named("x");
    case 5: return GridLine::named("x", 2);
    case 6: return GridLine::span(1);
    case 7: return GridLine::named_span("x", 2);
    case 8: return GridLine::named("missing");
    case 9: return GridLine::named("x", -1);
    case 10: return GridLine::named_span("x", 3);
    case 11: return GridLine::named_span("missing", 2);
    default: throw std::invalid_argument("Unknown Grid line endpoint");
    }
}

void run_case(std::size_t start, std::size_t end, std::size_t axis,
              std::size_t rtl, std::size_t flow, std::size_t second_placement,
              std::size_t sizing, std::size_t auto_tracks) {
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Grid;
    root_style.box.size = {Length::length(100), Length::length(100)};
    root_style.box.padding = {Length::length(5), Length::length(5),
        Length::length(5), Length::length(5)};
    root_style.gap = {Length::length(3), Length::length(3)};
    root_style.box.direction = rtl ? Direction::Rtl : Direction::Ltr;
    constexpr std::array flows{GridAutoFlow::Row, GridAutoFlow::Column,
        GridAutoFlow::RowDense, GridAutoFlow::ColumnDense};
    root_style.grid.auto_flow = flows.at(flow);
    root_style.grid.template_columns = {TrackSizing::fixed(20),
        TrackSizing::fixed(20), TrackSizing::fixed(20)};
    root_style.grid.template_rows = root_style.grid.template_columns;
    if (auto_tracks) {
        root_style.grid.auto_columns = {TrackSizing::fixed(11), TrackSizing::fixed(17)};
        root_style.grid.auto_rows = {TrackSizing::fixed(13), TrackSizing::fixed(29)};
    }
    root_style.grid.column_line_names = {{"x"}, {"x"}, {"x"}, {"x"}};
    root_style.grid.row_line_names = root_style.grid.column_line_names;
    root_style.justify_items = {sizing ? ItemPosition::Stretch : ItemPosition::Start};
    root_style.align_items = {sizing ? ItemPosition::Stretch : ItemPosition::Start};
    const auto root = tree.create_node(root_style);

    Style first_style;
    if (!sizing) {
        first_style.box.size = {Length::length(10), Length::length(10)};
    }
    const GridPlacement placement{line(start), line(end)};
    if (axis == 0) first_style.grid.column = placement;
    else first_style.grid.row = placement;
    const auto first = tree.create_node(first_style);
    tree.append_child(root, first);

    Style second_style;
    second_style.box.size = {Length::length(10), Length::length(10)};
    auto& second_primary = flow == 0 || flow == 2
        ? second_style.grid.column : second_style.grid.row;
    auto& second_secondary = flow == 0 || flow == 2
        ? second_style.grid.row : second_style.grid.column;
    if (second_placement == 1 || second_placement == 3) {
        second_secondary.start = GridLine::index(2);
    }
    if (second_placement == 2 || second_placement == 3) {
        second_primary.start = GridLine::index(2);
    }
    const auto second = tree.create_node(second_style);
    tree.append_child(root, second);
    Style third_style;
    third_style.box.size = {Length::length(10), Length::length(10)};
    const auto third = tree.create_node(third_style);
    tree.append_child(root, third);

    const auto name = std::string("grid-line-matrix-")
        + std::to_string(start) + '-' + std::to_string(end)
        + '-' + std::to_string(axis) + '-' + std::to_string(rtl)
        + '-' + std::to_string(flow) + '-' + std::to_string(second_placement)
        + '-' + std::to_string(sizing) + '-' + std::to_string(auto_tracks);
    reference::write_case(std::cout, name, tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::definite(100)}),
        {{1, root}, {2, first}, {3, second}, {4, third}});
}
} // namespace

int main(int argc, char** argv) {
    try {
        std::size_t start_begin = 0;
        std::size_t start_end = 12;
        if (argc == 3 && std::string(argv[1]) == "--start-line") {
            start_begin = std::stoul(argv[2]);
            if (start_begin >= 12) throw std::invalid_argument("Unknown start line");
            start_end = start_begin + 1;
        } else if (argc != 1) {
            throw std::invalid_argument("Expected --start-line INDEX or no argument");
        }
        std::cout << "{\"protocol\":\"AXISLY_GRID_LINE_MATRIX_V1\",\"cases\":[";
        bool first = true;
        for (std::size_t start = start_begin; start < start_end; ++start)
        for (std::size_t end = 0; end < 12; ++end)
        for (std::size_t axis = 0; axis < 2; ++axis)
        for (std::size_t rtl = 0; rtl < 2; ++rtl)
        for (std::size_t flow = 0; flow < 4; ++flow)
        for (std::size_t second_placement = 0; second_placement < 4; ++second_placement)
        for (std::size_t sizing = 0; sizing < 2; ++sizing)
        for (std::size_t auto_tracks = 0; auto_tracks < 2; ++auto_tracks) {
            if (!first) std::cout << ',';
            first = false;
            run_case(start, end, axis, rtl, flow, second_placement, sizing, auto_tracks);
        }
        std::cout << "]}\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
