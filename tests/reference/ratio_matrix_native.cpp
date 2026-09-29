#include "css_reference_output.hpp"

#include <array>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

namespace {
using namespace axisly;
using namespace axisly::css;

constexpr std::array displays{Display::Block, Display::Flex, Display::Grid};

void run_case(std::size_t scenario, std::size_t parent_display,
              std::size_t child_display, std::size_t known_axis,
              std::size_t box_sizing, std::size_t bound, std::size_t direction,
              std::optional<std::size_t> scrollbar_mode, bool details) {
    LayoutTree tree;
    Style outer_style;
    outer_style.display = displays[parent_display];
    outer_style.box.direction = direction == 0 ? Direction::Ltr : Direction::Rtl;
    if (scenario != 2) outer_style.box.size.width = Length::length(160);
    if (scenario != 1) outer_style.box.size.height = Length::length(120);
    outer_style.align_items = {ItemPosition::Start};
    outer_style.justify_items = {ItemPosition::Start};
    if (parent_display == 2) {
        outer_style.grid.template_columns = {TrackSizing::fixed(120)};
        outer_style.grid.template_rows = {TrackSizing::automatic()};
    }
    const auto outer = tree.create_node(outer_style);
    Style inner_style;
    inner_style.display = displays[child_display];
    inner_style.box.direction = outer_style.box.direction;
    inner_style.box.box_sizing = box_sizing == 0
        ? BoxSizing::BorderBox : BoxSizing::ContentBox;
    inner_style.box.aspect_ratio = 2.0F;
    if (known_axis == 0) inner_style.box.size.width = Length::length(60);
    else inner_style.box.size.height = Length::length(40);
    inner_style.box.padding = {Length::length(10), Length::length(5),
                               Length::length(10), Length::length(5)};
    inner_style.flex_shrink = 0;
    inner_style.align_items = {ItemPosition::Start};
    inner_style.justify_items = {ItemPosition::Start};
    if (scrollbar_mode) {
        inner_style.scrollbar_width = 8;
        if (*scrollbar_mode != 1) inner_style.overflow_x = Overflow::Scroll;
        if (*scrollbar_mode != 0) inner_style.overflow_y = Overflow::Scroll;
    }
    switch (bound) {
    case 0: break;
    case 1: inner_style.box.min_size.width = Length::length(80); break;
    case 2: inner_style.box.max_size.width = Length::length(40); break;
    case 3: inner_style.box.min_size.height = Length::length(50); break;
    case 4: inner_style.box.max_size.height = Length::length(20); break;
    case 5:
        inner_style.box.min_size.width = Length::length(80);
        inner_style.box.max_size.height = Length::length(20);
        break;
    default: throw std::invalid_argument("Unknown aspect-ratio bound");
    }
    if (child_display == 2) {
        inner_style.grid.template_columns = {TrackSizing::automatic()};
        inner_style.grid.template_rows = {TrackSizing::automatic()};
    }
    const auto inner = tree.create_node(inner_style);
    tree.append_child(outer, inner);
    const auto leaf = tree.create_node({}, [](const MeasureRequest& request) {
        return Measurement{{request.known_content_width.value_or(Unit{40}),
            request.known_content_height.value_or(Unit{30})}, {}, {}, Size{40, 30}};
    });
    tree.append_child(inner, leaf);
    const AvailableSpace width = scenario == 2
        ? AvailableSpace::max_content() : AvailableSpace::definite(160);
    const AvailableSpace height = scenario == 1
        ? AvailableSpace::max_content() : AvailableSpace::definite(120);
    auto name = std::string{scrollbar_mode ? "ratio-scrollbar-matrix-" : "ratio-matrix-"}
        + std::to_string(scenario) + '-'
        + std::to_string(parent_display) + '-' + std::to_string(child_display)
        + '-' + std::to_string(known_axis) + '-' + std::to_string(box_sizing)
        + '-' + std::to_string(bound) + '-' + std::to_string(direction);
    if (scrollbar_mode) name += '-' + std::to_string(*scrollbar_mode);
    reference::write_case(std::cout, name, tree.compute(outer, {width, height}),
        {{1, outer}, {2, inner}, {3, leaf}}, details);
}
} // namespace

int main(int argc, char** argv) {
    try {
        const bool details = argc == 2 && std::string(argv[1]) == "--details";
        const bool scrollbar = argc == 2 && std::string(argv[1]) == "--scrollbar";
        if (argc != 1 && !details && !scrollbar) {
            throw std::invalid_argument("Expected --details, --scrollbar or no argument");
        }
        std::cout << "{\"protocol\":\""
            << (scrollbar ? "AXISLY_RATIO_SCROLLBAR_MATRIX_V1"
                : "AXISLY_RATIO_MATRIX_V1") << "\",\"cases\":[";
        bool first = true;
        for (std::size_t scenario = 0; scenario < (scrollbar ? 1U : 3U); ++scenario)
        for (std::size_t parent_display = 0; parent_display < 3; ++parent_display)
        for (std::size_t child_display = 0; child_display < 3; ++child_display)
        for (std::size_t known_axis = 0; known_axis < 2; ++known_axis)
        for (std::size_t box_sizing = 0; box_sizing < 2; ++box_sizing)
        for (std::size_t bound = 0; bound < 6; ++bound)
        for (std::size_t direction = 0; direction < 2; ++direction) {
            if (scrollbar && bound != 0 && bound != 1 && bound != 3) continue;
            for (std::size_t mode = 0; mode < (scrollbar ? 3U : 1U); ++mode) {
                if (!first) std::cout << ',';
                first = false;
                run_case(scenario, parent_display, child_display, known_axis,
                    box_sizing, bound, direction,
                    scrollbar ? std::optional<std::size_t>{mode} : std::nullopt,
                    details);
            }
        }
        std::cout << "]}\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
