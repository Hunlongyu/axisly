#include "css_reference_output.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace axisly;
using namespace axisly::css;

constexpr std::array displays{Display::Block, Display::Flex, Display::Grid};

AvailableSpace axis_space(std::size_t profile, bool width) {
    if (profile == 1 && width) return AvailableSpace::max_content();
    if (profile == 2 && !width) return AvailableSpace::max_content();
    if (profile == 3) return AvailableSpace::max_content();
    if (profile == 4) return AvailableSpace::min_content();
    return AvailableSpace::definite(width ? Unit{200} : Unit{160});
}

void run_case(std::size_t display, std::size_t available,
              std::size_t sized_axis, std::size_t box_sizing,
              std::size_t bounds, std::size_t direction) {
    LayoutTree tree;
    Style root_style;
    root_style.display = displays[display];
    root_style.box.direction = direction ? Direction::Rtl : Direction::Ltr;
    root_style.box.box_sizing = box_sizing
        ? BoxSizing::ContentBox : BoxSizing::BorderBox;
    root_style.box.aspect_ratio = 1.5F;
    if (sized_axis == 0) root_style.box.size.width = Length::length(80);
    if (sized_axis == 1) root_style.box.size.height = Length::length(60);
    if (bounds == 1 || bounds == 3) {
        root_style.box.min_size.width = Length::length(100);
    }
    if (bounds == 2 || bounds == 3) {
        root_style.box.max_size.height = Length::length(40);
    }
    root_style.box.padding = {Length::length(7), Length::length(5),
                              Length::length(7), Length::length(5)};
    root_style.box.border = {Length::length(2), Length::length(3),
                             Length::length(2), Length::length(3)};
    root_style.align_items = {ItemPosition::Start};
    root_style.justify_items = {ItemPosition::Start};
    if (display == 2) {
        root_style.grid.template_columns = {TrackSizing::automatic()};
        root_style.grid.template_rows = {TrackSizing::automatic()};
    }
    const auto root = tree.create_node(root_style);
    const auto child = tree.create_node({}, [](const MeasureRequest& request) {
        return Measurement{{request.known_content_width.value_or(Unit{42}),
            request.known_content_height.value_or(Unit{28})}, {}, {}, Size{42, 28}};
    });
    tree.append_child(root, child);

    const auto name = std::string("root-ratio-matrix-")
        + std::to_string(display) + '-' + std::to_string(available) + '-'
        + std::to_string(sized_axis) + '-' + std::to_string(box_sizing) + '-'
        + std::to_string(bounds) + '-' + std::to_string(direction);
    reference::write_case(std::cout, name, tree.compute(root,
        {axis_space(available, true), axis_space(available, false)}),
        {{1, root}, {2, child}}, true);
}
} // namespace

int main() {
    try {
        std::cout << "{\"protocol\":\"AXISLY_ROOT_RATIO_MATRIX_V1\",\"cases\":[";
        bool first = true;
        for (std::size_t display = 0; display < 3; ++display)
        for (std::size_t available = 0; available < 5; ++available)
        for (std::size_t sized_axis = 0; sized_axis < 3; ++sized_axis)
        for (std::size_t box_sizing = 0; box_sizing < 2; ++box_sizing)
        for (std::size_t bounds = 0; bounds < 4; ++bounds)
        for (std::size_t direction = 0; direction < 2; ++direction) {
            if (!first) std::cout << ',';
            first = false;
            run_case(display, available, sized_axis, box_sizing, bounds, direction);
        }
        std::cout << "]}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
