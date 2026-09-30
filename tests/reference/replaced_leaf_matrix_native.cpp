#include "css_reference_output.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace axisly;
using namespace axisly::css;

constexpr std::array displays{Display::Block, Display::Flex, Display::Grid};

void run_case(std::size_t parent_display, std::size_t replaced,
              std::size_t ratio, std::size_t box_sizing,
              std::size_t sized_axis, std::size_t bound,
              std::size_t alignment, std::size_t direction) {
    LayoutTree tree;
    Style root_style;
    root_style.display = displays[parent_display];
    root_style.box.size = {Length::length(120), Length::length(100)};
    root_style.box.direction = direction ? Direction::Rtl : Direction::Ltr;
    if (alignment) {
        root_style.align_items = {ItemPosition::Start};
        root_style.justify_items = {ItemPosition::Start};
    }
    if (parent_display == 2) {
        root_style.grid.template_columns = {TrackSizing::automatic()};
        root_style.grid.template_rows = {TrackSizing::automatic()};
    }
    const auto root = tree.create_node(root_style);

    Style leaf_style;
    leaf_style.box.replaced = replaced != 0;
    leaf_style.box.direction = root_style.box.direction;
    leaf_style.box.box_sizing = box_sizing
        ? BoxSizing::ContentBox : BoxSizing::BorderBox;
    if (ratio) leaf_style.box.aspect_ratio = 2.0F;
    if (sized_axis == 1) leaf_style.box.size.width = Length::length(36);
    if (sized_axis == 2) leaf_style.box.size.height = Length::length(24);
    if (bound == 1) leaf_style.box.min_size.width = Length::length(50);
    if (bound == 2) leaf_style.box.max_size.width = Length::length(30);
    if (bound == 3) leaf_style.box.min_size.height = Length::length(26);
    if (bound == 4) leaf_style.box.max_size.height = Length::length(16);
    leaf_style.box.padding = {Length::length(4), Length::length(3),
                              Length::length(4), Length::length(3)};
    leaf_style.box.border = {Length::length(1), Length::length(1),
                             Length::length(1), Length::length(1)};
    const auto leaf = tree.create_node(leaf_style, [](const MeasureRequest& request) {
        const Unit width = request.known_content_width.value_or(
            request.available_width.kind == AvailableKind::MinContent
                ? Unit{20} : Unit{40});
        const Unit height = request.known_content_height.value_or(Unit{10});
        return Measurement{{width, height}, {}, {}, Size{40, 10}};
    });
    tree.append_child(root, leaf);

    const auto name = std::string("replaced-leaf-matrix-")
        + std::to_string(parent_display) + '-' + std::to_string(replaced) + '-'
        + std::to_string(ratio) + '-' + std::to_string(box_sizing) + '-'
        + std::to_string(sized_axis) + '-' + std::to_string(bound) + '-'
        + std::to_string(alignment) + '-' + std::to_string(direction);
    reference::write_case(std::cout, name, tree.compute(root,
        {AvailableSpace::definite(120), AvailableSpace::definite(100)}),
        {{1, root}, {2, leaf}}, true);
}
} // namespace

int main() {
    try {
        std::cout << "{\"protocol\":\"AXISLY_REPLACED_LEAF_MATRIX_V1\",\"cases\":[";
        bool first = true;
        for (std::size_t parent_display = 0; parent_display < 3; ++parent_display)
        for (std::size_t replaced = 0; replaced < 2; ++replaced)
        for (std::size_t ratio = 0; ratio < 2; ++ratio)
        for (std::size_t box_sizing = 0; box_sizing < 2; ++box_sizing)
        for (std::size_t sized_axis = 0; sized_axis < 3; ++sized_axis)
        for (std::size_t bound = 0; bound < 5; ++bound)
        for (std::size_t alignment = 0; alignment < 2; ++alignment)
        for (std::size_t direction = 0; direction < 2; ++direction) {
            if (!first) std::cout << ',';
            first = false;
            run_case(parent_display, replaced, ratio, box_sizing,
                sized_axis, bound, alignment, direction);
        }
        std::cout << "]}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
