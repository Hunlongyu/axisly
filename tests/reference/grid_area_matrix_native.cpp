#include "css_reference_output.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace axisly;
using namespace axisly::css;

constexpr std::array flows{GridAutoFlow::Row, GridAutoFlow::Column,
    GridAutoFlow::RowDense, GridAutoFlow::ColumnDense};

GridPlacement placement(std::size_t kind) {
    switch (kind) {
    case 0: return {GridLine::named("a"), GridLine::named("a")};
    case 1: return {GridLine::named("a-start"), GridLine::named("a-end")};
    case 2: return {GridLine::named("a"), GridLine::automatic()};
    case 3: return {GridLine::automatic(), GridLine::named("a")};
    case 4: return {GridLine::index(1), GridLine::named("a")};
    case 5: return {GridLine::named("a"), GridLine::index(-1)};
    case 6: return {GridLine::named_span("a", 2), GridLine::named("a-end")};
    case 7: return {GridLine::named("b"), GridLine::named("c")};
    default: throw std::invalid_argument("Unknown area placement");
    }
}

void template_tracks(GridStyle& grid, std::size_t kind) {
    if (kind == 0) {
        grid.template_columns = {TrackSizing::fixed(20), TrackSizing::fixed(25)};
        grid.template_rows = grid.template_columns;
        grid.column_line_names = {{"q"}, {"q"}, {"q"}};
        grid.row_line_names = grid.column_line_names;
    } else if (kind == 1) {
        grid.template_columns = {TrackSizing::fixed(20)};
        grid.template_rows = grid.template_columns;
    } else if (kind == 2) {
        const GridRepeat repeated{RepeatKind::Count, 2,
            {TrackSizing::fixed(20)}, {{"q"}, {}}};
        grid.template_columns = {repeated};
        grid.template_rows = grid.template_columns;
        grid.column_line_names = {{"a-start"}, {"a-end"}};
        grid.row_line_names = grid.column_line_names;
    } else if (kind == 3 || kind == 4) {
        const GridRepeat repeated{
            kind == 3 ? RepeatKind::AutoFill : RepeatKind::AutoFit, 1,
            {TrackSizing::fixed(20)}, {{"q"}, {}}};
        grid.template_columns = {repeated};
        grid.template_rows = grid.template_columns;
        grid.column_line_names = {{"a-start"}, {"a-end"}};
        grid.row_line_names = grid.column_line_names;
    } else {
        throw std::invalid_argument("Unknown area track template");
    }
}

void run_case(std::size_t areas, std::size_t tracks, std::size_t endpoints,
              std::size_t axis, std::size_t rtl, std::size_t flow,
              std::size_t sizing, std::size_t query, bool details) {
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Grid;
    if (query == 0) {
        root_style.box.size = {Length::length(120), Length::length(100)};
    }
    root_style.box.padding = {Length::length(4), Length::length(4),
        Length::length(4), Length::length(4)};
    root_style.box.border = {Length::length(1), Length::length(1),
        Length::length(1), Length::length(1)};
    root_style.box.direction = rtl ? Direction::Rtl : Direction::Ltr;
    root_style.gap = {Length::length(3), Length::length(3)};
    root_style.grid.auto_flow = flows.at(flow);
    root_style.grid.template_areas = areas == 0
        ? std::vector<std::vector<std::string>>{{"a", "a"}, {"b", "c"}}
        : areas == 1
            ? std::vector<std::vector<std::string>>{{"a", "b"}, {"a", "c"}}
            : std::vector<std::vector<std::string>>{
                {"a", "a", "."}, {"a", "a", "b"}, {"c", "c", "b"}};
    template_tracks(root_style.grid, tracks);
    root_style.justify_items = {sizing ? ItemPosition::Stretch : ItemPosition::Start};
    root_style.align_items = {sizing ? ItemPosition::Stretch : ItemPosition::Start};
    const auto root = tree.create_node(root_style);

    Style first_style;
    if (!sizing) first_style.box.size = {Length::length(10), Length::length(10)};
    const auto area_a = placement(0);
    first_style.grid.column = axis == 0 ? placement(endpoints) : area_a;
    first_style.grid.row = axis == 1 ? placement(endpoints) : area_a;
    const auto first = tree.create_node(first_style);
    tree.append_child(root, first);

    Style second_style;
    second_style.box.size = {Length::length(10), Length::length(10)};
    const auto second = tree.create_node(second_style);
    tree.append_child(root, second);

    Style third_style;
    third_style.box.size = {Length::length(10), Length::length(10)};
    third_style.grid.column = {GridLine::named("b"), GridLine::named("b")};
    third_style.grid.row = third_style.grid.column;
    const auto third = tree.create_node(third_style);
    tree.append_child(root, third);

    const auto name = "grid-area-matrix-" + std::to_string(areas) + '-'
        + std::to_string(tracks) + '-' + std::to_string(endpoints) + '-'
        + std::to_string(axis) + '-' + std::to_string(rtl) + '-'
        + std::to_string(flow) + '-' + std::to_string(sizing) + '-'
        + std::to_string(query);
    const auto available = query == 0
        ? AvailableSize{AvailableSpace::definite(120), AvailableSpace::definite(100)}
        : query == 1
            ? AvailableSize{AvailableSpace::max_content(), AvailableSpace::max_content()}
            : AvailableSize{AvailableSpace::min_content(), AvailableSpace::min_content()};
    reference::write_case(std::cout, name, tree.compute(root, available),
        {{1, root}, {2, first}, {3, second}, {4, third}}, details);
}
} // namespace

int main(int argc, char** argv) {
    try {
        const bool details = argc == 2 && std::string_view{argv[1]} == "--details";
        if (argc > 2 || (argc == 2 && !details)) {
            throw std::invalid_argument("Expected optional --details");
        }
        std::cout << "{\"protocol\":\"AXISLY_GRID_AREA_MATRIX_V1\",\"cases\":[";
        bool first = true;
        for (std::size_t areas = 0; areas < 3; ++areas)
        for (std::size_t tracks = 0; tracks < 5; ++tracks)
        for (std::size_t endpoints = 0; endpoints < 8; ++endpoints)
        for (std::size_t axis = 0; axis < 2; ++axis)
        for (std::size_t rtl = 0; rtl < 2; ++rtl)
        for (std::size_t flow = 0; flow < flows.size(); ++flow)
        for (std::size_t sizing = 0; sizing < 2; ++sizing)
        for (std::size_t query = 0; query < 3; ++query) {
            if (!first) std::cout << ',';
            first = false;
            run_case(areas, tracks, endpoints, axis, rtl, flow, sizing, query,
                details);
        }
        std::cout << "]}\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
