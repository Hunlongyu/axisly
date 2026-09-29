#include "css_reference_output.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
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

void run_case(std::size_t scenario, std::size_t columns, std::size_t rows,
              std::size_t justify, std::size_t align, std::size_t rtl) {
    LayoutTree tree;
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
        tree.compute(root, scenario == 0
            ? AvailableSize{AvailableSpace::definite(120), AvailableSpace::definite(90)}
            : scenario == 1
                ? AvailableSize{AvailableSpace::max_content(), AvailableSpace::max_content()}
                : AvailableSize{AvailableSpace::min_content(), AvailableSpace::min_content()}),
        nodes);
}
} // namespace

int main() {
    try {
        std::cout << "{\"protocol\":\"AXISLY_GRID_MATRIX_V1\",\"cases\":[";
        bool first = true;
        for (std::size_t scenario = 0; scenario < 3; ++scenario)
        for (std::size_t columns = 0; columns < 4; ++columns)
        for (std::size_t rows = 0; rows < 4; ++rows)
        for (std::size_t justify = 0; justify < positions.size(); ++justify)
        for (std::size_t align = 0; align < positions.size(); ++align)
        for (std::size_t rtl = 0; rtl < 2; ++rtl) {
            if (!first) std::cout << ',';
            first = false;
            run_case(scenario, columns, rows, justify, align, rtl);
        }
        std::cout << "]}\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
