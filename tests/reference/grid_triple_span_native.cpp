#include "css_reference_output.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace axisly;
using namespace axisly::css;

std::vector<TemplateTrack> tracks(std::size_t index) {
    switch (index) {
    case 0: return {TrackSizing::fraction(1), TrackSizing::fraction(2),
        TrackSizing::fraction(1)};
    case 1: return {TrackSizing::automatic(), TrackSizing::fraction(1),
        TrackSizing::automatic()};
    case 2: return {
        TrackSizing::minmax(TrackBreadth::length(10), TrackBreadth::fraction(1)),
        TrackSizing::fit_content_length(45), TrackSizing::fraction(2)};
    case 3: return {TrackSizing::fixed(30), TrackSizing::min_content(),
        TrackSizing::fraction(1)};
    case 4: return {TrackSizing::fraction(0.2F), TrackSizing::fraction(0),
        TrackSizing::fraction(0.5F)};
    case 5: return {TrackSizing::minmax(
        TrackBreadth::length(10), TrackBreadth::length(40)),
        TrackSizing::automatic(), TrackSizing::fraction(1)};
    case 6: return {TrackSizing::minmax(
        TrackBreadth::length(20), TrackBreadth::max_content()),
        TrackSizing::fit_content_length(30), TrackSizing::automatic()};
    case 7: return {TrackSizing::percent(0.2F), TrackSizing::minmax(
        TrackBreadth::length(5), TrackBreadth::fraction(1)),
        TrackSizing::percent(0.3F)};
    case 8: return {TrackSizing::minmax(
        TrackBreadth::length(10), TrackBreadth::length(50)),
        TrackSizing::minmax(TrackBreadth::length(5), TrackBreadth::length(35)),
        TrackSizing::automatic()};
    case 9: return {TrackSizing::min_content(), TrackSizing::max_content(),
        TrackSizing::automatic()};
    case 10: return {TrackSizing::fit_content_length(20),
        TrackSizing::fit_content_length(30), TrackSizing::fraction(1)};
    case 11: return {TrackSizing::percent(0.3F), TrackSizing::fraction(1),
        TrackSizing::automatic()};
    default: throw std::invalid_argument("Unknown three-track template");
    }
}

GridPlacement placement(int start, int end) {
    return {GridLine::index(static_cast<std::int16_t>(start)),
        GridLine::index(static_cast<std::int16_t>(end))};
}

void run_case(std::size_t scenario, std::size_t track,
              std::size_t axis, std::size_t rtl, std::size_t order,
              bool details) {
    const bool columns = axis == 0;
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Grid;
    root_style.box.direction = rtl ? Direction::Rtl : Direction::Ltr;
    if (scenario == 0) {
        root_style.box.size = {Length::length(columns ? 160.0F : 100.0F),
            Length::length(columns ? 100.0F : 140.0F)};
    }
    root_style.box.padding = {Length::length(5), Length::length(5),
        Length::length(5), Length::length(5)};
    root_style.gap = {Length::length(4), Length::length(6)};
    root_style.justify_items = {ItemPosition::Start};
    root_style.align_items = {ItemPosition::Start};
    if (columns) {
        root_style.grid.template_columns = tracks(track);
        root_style.grid.template_rows = {TrackSizing::fixed(40), TrackSizing::fixed(40)};
    } else {
        root_style.grid.template_rows = tracks(track);
        root_style.grid.template_columns = {TrackSizing::fixed(40), TrackSizing::fixed(40)};
    }
    const auto root = tree.create_node(root_style);
    std::vector<reference::IdentifiedNode> nodes{{1, root}};
    constexpr std::array main_sizes{80.0F, 75.0F, 30.0F, 22.0F, 120.0F};
    constexpr std::array cross_sizes{20.0F, 25.0F, 15.0F, 15.0F, 18.0F};
    constexpr std::array main_places{
        std::array{1, 3}, std::array{2, 4},
        std::array{1, 2}, std::array{3, 4}, std::array{1, 4},
    };
    constexpr std::array cross_places{
        std::array{1, 2}, std::array{2, 3},
        std::array{2, 3}, std::array{1, 2}, std::array{1, 3},
    };
    constexpr std::array item_orders{
        std::array{0, 1, 2, 3, 4}, std::array{4, 0, 1, 2, 3},
        std::array{1, 4, 0, 3, 2}, std::array{2, 3, 1, 0, 4},
        std::array{4, -1, -1, -1, -1},
        std::array{0, 4, -1, -1, -1},
        std::array{1, 4, -1, -1, -1},
        std::array{0, 1, 4, -1, -1},
        std::array{2, 3, 4, -1, -1},
    };
    for (std::size_t index = 0; index < main_sizes.size(); ++index) {
        const auto source = item_orders[order][index];
        if (source < 0) break;
        Style child;
        child.box.size = {Length::length(columns ? main_sizes[source] : cross_sizes[source]),
            Length::length(columns ? cross_sizes[source] : main_sizes[source])};
        if (source == 4) {
            (columns ? child.box.min_size.width : child.box.min_size.height)
                = Length::length(140);
        }
        const auto main = placement(main_places[source][0], main_places[source][1]);
        const auto cross = placement(cross_places[source][0], cross_places[source][1]);
        child.grid.column = columns ? main : cross;
        child.grid.row = columns ? cross : main;
        const auto id = tree.create_node(child);
        tree.append_child(root, id);
        nodes.emplace_back(static_cast<int>(index) + 2, id);
    }
    const auto available = scenario == 0 || scenario == 3
        ? AvailableSize{AvailableSpace::definite(columns ? 160.0F : 100.0F),
            AvailableSpace::definite(columns ? 100.0F : 140.0F)}
        : scenario == 1
            ? AvailableSize{AvailableSpace::max_content(), AvailableSpace::max_content()}
            : AvailableSize{AvailableSpace::min_content(), AvailableSpace::min_content()};
    const auto name = "grid-triple-span-" + std::to_string(scenario) + '-'
        + std::to_string(track) + '-' + std::to_string(axis) + '-'
        + std::to_string(rtl) + '-' + std::to_string(order);
    reference::write_case(std::cout, name, tree.compute(root, available), nodes, details);
}
} // namespace

int main(int argc, char** argv) {
    try {
        const bool details = argc == 2 && std::string(argv[1]) == "--details";
        if (argc != 1 && !details) throw std::invalid_argument("Expected --details or no argument");
        std::cout << "{\"protocol\":\"AXISLY_GRID_TRIPLE_SPAN_V1\",\"cases\":[";
        bool first = true;
        for (std::size_t scenario = 0; scenario < 4; ++scenario)
        for (std::size_t track = 0; track < 12; ++track)
        for (std::size_t axis = 0; axis < 2; ++axis)
        for (std::size_t rtl = 0; rtl < 2; ++rtl)
        for (std::size_t order = 0; order < 9; ++order) {
            if (!first) std::cout << ',';
            first = false;
            run_case(scenario, track, axis, rtl, order, details);
        }
        std::cout << "]}\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
