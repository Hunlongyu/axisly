#include "css_reference_output.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace axisly;
using namespace axisly::css;

constexpr std::array displays{Display::Block, Display::Flex, Display::Grid};

void set_grid_tracks(Style& style) {
    if (style.display != Display::Grid) return;
    style.grid.template_columns = {TrackSizing::automatic()};
    style.grid.template_rows = {TrackSizing::automatic()};
}

void run_case(std::size_t root_kind, std::size_t middle_kind,
              std::size_t inner_kind, std::size_t known_axis,
              std::size_t percentage_axis, std::size_t box_sizing,
              std::size_t overflow, std::size_t direction) {
    LayoutTree tree;
    Style root_style;
    root_style.display = displays[root_kind];
    root_style.box.size = {Length::length(200), Length::length(160)};
    root_style.box.padding = {Length::length(4), Length::length(4),
                              Length::length(4), Length::length(4)};
    root_style.align_items = {ItemPosition::Start};
    root_style.justify_items = {ItemPosition::Start};
    if (root_kind == 2) {
        root_style.grid.template_columns = {TrackSizing::fixed(150)};
        root_style.grid.template_rows = {TrackSizing::automatic()};
    }
    const auto root = tree.create_node(root_style);

    Style middle_style;
    middle_style.display = displays[middle_kind];
    middle_style.box.direction = direction ? Direction::Rtl : Direction::Ltr;
    middle_style.box.box_sizing = box_sizing
        ? BoxSizing::ContentBox : BoxSizing::BorderBox;
    middle_style.box.aspect_ratio = 1.5F;
    if (known_axis == 0) middle_style.box.size.width = Length::length(80);
    else if (known_axis == 1) middle_style.box.size.height = Length::length(60);
    middle_style.box.padding = {Length::length(7), Length::length(5),
                                Length::length(7), Length::length(5)};
    middle_style.flex_shrink = 0;
    middle_style.align_items = {ItemPosition::Start};
    middle_style.justify_items = {ItemPosition::Start};
    middle_style.scrollbar_width = 8;
    if (overflow == 1 || overflow == 3) middle_style.overflow_x = Overflow::Scroll;
    if (overflow == 2 || overflow == 3) middle_style.overflow_y = Overflow::Scroll;
    set_grid_tracks(middle_style);
    const auto middle = tree.create_node(middle_style);
    tree.append_child(root, middle);

    Style inner_style;
    inner_style.display = displays[inner_kind];
    inner_style.box.aspect_ratio = 2.0F;
    if (percentage_axis == 0) inner_style.box.size.width = Length::percent(0.5F);
    else inner_style.box.size.height = Length::percent(0.5F);
    inner_style.box.padding = {Length::length(3), Length::length(3),
                               Length::length(3), Length::length(3)};
    inner_style.flex_shrink = 0;
    inner_style.align_items = {ItemPosition::Start};
    inner_style.justify_items = {ItemPosition::Start};
    set_grid_tracks(inner_style);
    const auto inner = tree.create_node(inner_style);
    tree.append_child(middle, inner);

    const auto leaf = tree.create_node({}, [](const MeasureRequest& request) {
        return Measurement{{request.known_content_width.value_or(Unit{42}),
            request.known_content_height.value_or(Unit{28})}, {}, {}, Size{42, 28}};
    });
    tree.append_child(inner, leaf);

    const auto name = std::string("ratio-deep-matrix-")
        + std::to_string(root_kind) + '-' + std::to_string(middle_kind) + '-'
        + std::to_string(inner_kind) + '-' + std::to_string(known_axis) + '-'
        + std::to_string(percentage_axis) + '-' + std::to_string(box_sizing)
        + '-' + std::to_string(overflow) + '-' + std::to_string(direction);
    reference::write_case(std::cout, name, tree.compute(root,
        {AvailableSpace::definite(200), AvailableSpace::definite(160)}),
        {{1, root}, {2, middle}, {3, inner}, {4, leaf}}, true);
}
} // namespace

int main() {
    try {
        std::cout << "{\"protocol\":\"AXISLY_RATIO_DEEP_MATRIX_V1\",\"cases\":[";
        bool first = true;
        for (std::size_t root = 0; root < 3; ++root)
        for (std::size_t middle = 0; middle < 3; ++middle)
        for (std::size_t inner = 0; inner < 3; ++inner)
        for (std::size_t known = 0; known < 3; ++known)
        for (std::size_t percentage = 0; percentage < 2; ++percentage)
        for (std::size_t box = 0; box < 2; ++box)
        for (std::size_t overflow = 0; overflow < 4; ++overflow)
        for (std::size_t direction = 0; direction < 2; ++direction) {
            if (!first) std::cout << ',';
            first = false;
            run_case(root, middle, inner, known, percentage,
                box, overflow, direction);
        }
        std::cout << "]}\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
