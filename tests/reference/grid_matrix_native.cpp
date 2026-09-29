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

constexpr std::array positions{
    ItemPosition::Start, ItemPosition::End, ItemPosition::Center,
};
constexpr std::array item_sizes{
    Size{15, 10}, Size{30, 20}, Size{25, 25}, Size{10, 15},
};

std::vector<TemplateTrack> tracks(std::size_t index) {
    switch (index) {
    case 0: return {TrackSizing::fixed(30), TrackSizing::fraction(1)};
    case 1: return {TrackSizing::fraction(1), TrackSizing::fraction(2)};
    case 2: return {TrackSizing::automatic(), TrackSizing::automatic()};
    case 3: return {
        TrackSizing::minmax(TrackBreadth::length(10), TrackBreadth::fraction(1)),
        TrackSizing::fit_content_length(50),
    };
    case 4: return {TrackSizing::fraction(0), TrackSizing::fraction(1)};
    case 5: return {TrackSizing::fraction(0.2F), TrackSizing::fraction(0.2F)};
    case 6: return {TrackSizing::fraction(0.5F), TrackSizing::fraction(1)};
    case 7: return {TrackSizing::percent(0.2F), TrackSizing::percent(0.3F)};
    case 8: return {
        TrackSizing::minmax(TrackBreadth::percent(0.1F), TrackBreadth::percent(0.4F)),
        TrackSizing::minmax(TrackBreadth::length(5), TrackBreadth::max_content()),
    };
    default: throw std::invalid_argument("Unknown Grid track pair");
    }
}

std::string case_name(std::size_t scenario, std::size_t columns, std::size_t rows,
                      std::size_t justify, std::size_t align, std::size_t rtl) {
    return "grid-matrix-" + std::to_string(scenario) + '-'
        + std::to_string(columns) + '-'
        + std::to_string(rows) + '-' + std::to_string(justify) + '-'
        + std::to_string(align) + '-' + std::to_string(rtl);
}

Style container_style(std::size_t scenario, std::size_t columns,
                      std::size_t rows, std::size_t rtl) {
    Style root_style;
    root_style.display = Display::Grid;
    root_style.box.direction = rtl ? Direction::Rtl : Direction::Ltr;
    if (scenario == 0) {
        root_style.box.size = {Length::length(120), Length::length(90)};
    }
    root_style.box.padding = {Length::length(5), Length::length(5),
        Length::length(5), Length::length(5)};
    root_style.gap = {Length::length(4), Length::length(6)};
    root_style.grid.template_columns = tracks(columns);
    root_style.grid.template_rows = tracks(rows);
    return root_style;
}

AvailableSize available_size(std::size_t scenario) {
    if (scenario == 0 || scenario == 3) return {
        AvailableSpace::definite(120), AvailableSpace::definite(90)};
    if (scenario == 1) return {
        AvailableSpace::max_content(), AvailableSpace::max_content()};
    return {AvailableSpace::min_content(), AvailableSpace::min_content()};
}

void run_case(std::size_t scenario, std::size_t columns, std::size_t rows,
              std::size_t justify, std::size_t align, std::size_t rtl,
              bool details) {
    LayoutTree tree;
    Style root_style = container_style(scenario, columns, rows, rtl);
    root_style.justify_items = {positions[justify]};
    root_style.align_items = {positions[align]};
    const auto root = tree.create_node(root_style);
    std::vector<reference::IdentifiedNode> nodes{{1, root}};
    for (std::size_t index = 0; index < item_sizes.size(); ++index) {
        Style child_style;
        child_style.box.size = {Length::length(item_sizes[index].width),
            Length::length(item_sizes[index].height)};
        const auto child = tree.create_node(child_style);
        tree.append_child(root, child);
        nodes.emplace_back(static_cast<int>(index) + 2, child);
    }
    reference::write_case(std::cout,
        case_name(scenario, columns, rows, justify, align, rtl),
        tree.compute(root, available_size(scenario)), nodes, details);
}

void run_span_case(std::size_t scenario, std::size_t columns,
                   std::size_t rows, std::size_t mode, std::size_t rtl,
                   bool details) {
    LayoutTree tree;
    Style root_style = container_style(scenario, columns, rows, rtl);
    root_style.justify_items = {ItemPosition::Start};
    root_style.align_items = {ItemPosition::Start};
    const auto root = tree.create_node(root_style);
    std::vector<reference::IdentifiedNode> nodes{{1, root}};
    const std::array sizes = mode == 0
        ? std::array{Size{70, 20}, Size{25, 30}, Size{15, 15}}
        : std::array{Size{20, 70}, Size{30, 25}, Size{15, 15}};
    for (std::size_t index = 0; index < sizes.size(); ++index) {
        Style child_style;
        child_style.box.size = {Length::length(sizes[index].width),
            Length::length(sizes[index].height)};
        if (mode == 0) {
            child_style.grid.column = index == 0
                ? GridPlacement{GridLine::index(1), GridLine::index(3)}
                : GridPlacement{GridLine::index(static_cast<std::int16_t>(index)),
                    GridLine::index(static_cast<std::int16_t>(index + 1))};
            child_style.grid.row = index == 0
                ? GridPlacement{GridLine::index(1), GridLine::index(2)}
                : GridPlacement{GridLine::index(2), GridLine::index(3)};
        } else {
            child_style.grid.column = index == 0
                ? GridPlacement{GridLine::index(1), GridLine::index(2)}
                : GridPlacement{GridLine::index(2), GridLine::index(3)};
            child_style.grid.row = index == 0
                ? GridPlacement{GridLine::index(1), GridLine::index(3)}
                : GridPlacement{GridLine::index(static_cast<std::int16_t>(index)),
                    GridLine::index(static_cast<std::int16_t>(index + 1))};
        }
        const auto child = tree.create_node(child_style);
        tree.append_child(root, child);
        nodes.emplace_back(static_cast<int>(index) + 2, child);
    }
    const auto name = "grid-span-matrix-" + std::to_string(scenario) + '-'
        + std::to_string(columns) + '-' + std::to_string(rows) + '-'
        + std::to_string(mode) + '-' + std::to_string(rtl);
    reference::write_case(std::cout, name,
        tree.compute(root, available_size(scenario)), nodes, details);
}
} // namespace

int main(int argc, char** argv) {
    try {
        bool spanning = false;
        bool details = false;
        for (int argument = 1; argument < argc; ++argument) {
            const std::string_view option{argv[argument]};
            if (option == "--span") spanning = true;
            else if (option == "--details") details = true;
            else throw std::invalid_argument("Expected --span or --details");
        }
        std::cout << "{\"protocol\":\""
            << (spanning ? "AXISLY_GRID_SPAN_MATRIX_V1" : "AXISLY_GRID_MATRIX_V1")
            << "\",\"cases\":[";
        bool first = true;
        for (std::size_t scenario = 0; scenario < 4; ++scenario)
        for (std::size_t columns = 0; columns < 9; ++columns)
        for (std::size_t rows = 0; rows < 9; ++rows)
        for (std::size_t first_axis = 0; first_axis < (spanning ? 2 : positions.size()); ++first_axis)
        for (std::size_t second_axis = 0; second_axis < (spanning ? 1 : positions.size()); ++second_axis)
        for (std::size_t rtl = 0; rtl < 2; ++rtl) {
            if (!first) std::cout << ',';
            first = false;
            if (spanning) run_span_case(scenario, columns, rows, first_axis, rtl, details);
            else run_case(scenario, columns, rows, first_axis, second_axis, rtl, details);
        }
        std::cout << "]}\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
