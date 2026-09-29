#include "css_reference_output.hpp"

#include <array>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace axisly;
using namespace axisly::css;

constexpr std::array item_counts{0, 1, 2, 4};
struct SizeScenario {
    AvailableSpace available;
    std::optional<Unit> preferred;
    std::optional<Unit> minimum;
    std::optional<Unit> maximum;
};
const std::array size_scenarios{
    SizeScenario{AvailableSpace::definite(95), 95.0F, std::nullopt, std::nullopt},
    SizeScenario{AvailableSpace::definite(35), 35.0F, std::nullopt, std::nullopt},
    SizeScenario{AvailableSpace::max_content(), std::nullopt, std::nullopt, 95.0F},
    SizeScenario{AvailableSpace::max_content(), std::nullopt, 95.0F, std::nullopt},
    SizeScenario{AvailableSpace::max_content(), std::nullopt, std::nullopt, std::nullopt},
    SizeScenario{AvailableSpace::definite(95), std::nullopt, std::nullopt, std::nullopt},
    SizeScenario{AvailableSpace::definite(35), std::nullopt, std::nullopt, std::nullopt},
    SizeScenario{AvailableSpace::definite(35), std::nullopt, std::nullopt, 95.0F},
    SizeScenario{AvailableSpace::definite(35), std::nullopt, 95.0F, std::nullopt},
    SizeScenario{AvailableSpace::definite(95), std::nullopt, std::nullopt, 35.0F},
};

std::vector<TrackSizing> pattern(std::size_t kind) {
    switch (kind) {
    case 0: return {TrackSizing::fixed(20)};
    case 1: return {TrackSizing::minmax(
        TrackBreadth::length(10), TrackBreadth::fraction(1))};
    case 2: return {TrackSizing::minmax(
        TrackBreadth::min_content(), TrackBreadth::length(20))};
    case 3: return {TrackSizing::percent(0.2F)};
    case 4: return {TrackSizing::fixed(20), TrackSizing::fixed(10)};
    case 5: return {TrackSizing::minmax(
        TrackBreadth::length(10), TrackBreadth::max_content())};
    default: throw std::invalid_argument("Unknown repeat pattern");
    }
}

void run_case(std::size_t axis, std::size_t repeat_kind, std::size_t track,
              std::size_t constraint, std::size_t gap, std::size_t children,
              std::size_t rtl, bool details) {
    const bool columns = axis == 0;
    const auto& scenario = size_scenarios[constraint];
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Grid;
    root_style.box.direction = rtl ? Direction::Rtl : Direction::Ltr;
    root_style.box.padding = {Length::length(3), Length::length(3),
        Length::length(3), Length::length(3)};
    root_style.box.border = {Length::length(1), Length::length(1),
        Length::length(1), Length::length(1)};
    root_style.justify_items = {ItemPosition::Start};
    root_style.align_items = {ItemPosition::Start};
    root_style.grid.auto_flow = columns ? GridAutoFlow::Row : GridAutoFlow::Column;
    const auto repeated = GridRepeat{
        repeat_kind == 0 ? RepeatKind::AutoFill : RepeatKind::AutoFit,
        1, pattern(track), {}};
    if (columns) {
        root_style.grid.template_columns = {repeated};
        root_style.grid.template_rows = {TrackSizing::fixed(20)};
        root_style.box.size.height = Length::length(60);
    } else {
        root_style.grid.template_rows = {repeated};
        root_style.grid.template_columns = {TrackSizing::fixed(20)};
        root_style.box.size.width = Length::length(60);
    }
    const Length gap_value = gap == 0 ? Length::length(0)
        : gap == 1 ? Length::length(5) : Length::percent(0.1F);
    if (columns) root_style.gap.width = gap_value;
    else root_style.gap.height = gap_value;
    auto& preferred = columns ? root_style.box.size.width : root_style.box.size.height;
    auto& minimum = columns ? root_style.box.min_size.width
        : root_style.box.min_size.height;
    auto& maximum = columns ? root_style.box.max_size.width
        : root_style.box.max_size.height;
    if (scenario.preferred) preferred = Length::length(*scenario.preferred);
    if (scenario.minimum) minimum = Length::length(*scenario.minimum);
    if (scenario.maximum) maximum = Length::length(*scenario.maximum);
    const auto root = tree.create_node(root_style);
    std::vector<reference::IdentifiedNode> nodes{{1, root}};
    for (int index = 0; index < item_counts[children]; ++index) {
        Style child_style;
        child_style.box.size = {Length::length(10), Length::length(10)};
        const auto child = tree.create_node(child_style);
        tree.append_child(root, child);
        nodes.emplace_back(index + 2, child);
    }
    const auto available = columns
        ? AvailableSize{scenario.available, AvailableSpace::definite(60)}
        : AvailableSize{AvailableSpace::definite(60), scenario.available};
    const auto name = "grid-repeat-matrix-" + std::to_string(axis) + '-'
        + std::to_string(repeat_kind) + '-' + std::to_string(track) + '-'
        + std::to_string(constraint) + '-' + std::to_string(gap) + '-'
        + std::to_string(children) + '-' + std::to_string(rtl);
    reference::write_case(std::cout, name, tree.compute(root, available),
        nodes, details);
}
} // namespace

int main(int argc, char** argv) {
    try {
        const bool details = argc == 2 && std::string_view{argv[1]} == "--details";
        if (argc > 2 || (argc == 2 && !details)) {
            throw std::invalid_argument("Expected optional --details");
        }
        std::cout << "{\"protocol\":\"AXISLY_GRID_REPEAT_MATRIX_V1\",\"cases\":[";
        bool first = true;
        for (std::size_t axis = 0; axis < 2; ++axis)
        for (std::size_t repeat_kind = 0; repeat_kind < 2; ++repeat_kind)
        for (std::size_t track = 0; track < 6; ++track)
        for (std::size_t constraint = 0; constraint < size_scenarios.size(); ++constraint)
        for (std::size_t gap = 0; gap < 3; ++gap)
        for (std::size_t children = 0; children < item_counts.size(); ++children)
        for (std::size_t rtl = 0; rtl < 2; ++rtl) {
            if (!first) std::cout << ',';
            first = false;
            run_case(axis, repeat_kind, track, constraint, gap, children,
                rtl, details);
        }
        std::cout << "]}\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
