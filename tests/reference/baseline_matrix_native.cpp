#include "css_reference_output.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace axisly;
using namespace axisly::css;

constexpr std::array directions{
    FlexDirection::Row, FlexDirection::RowReverse,
    FlexDirection::Column, FlexDirection::ColumnReverse,
};
constexpr std::array wraps{FlexWrap::NoWrap, FlexWrap::Wrap, FlexWrap::WrapReverse};
constexpr std::array alignments{
    std::array{ItemPosition::Baseline, ItemPosition::Baseline},
    std::array{ItemPosition::Baseline, ItemPosition::Start},
    std::array{ItemPosition::Start, ItemPosition::Baseline},
    std::array{ItemPosition::Baseline, ItemPosition::Stretch},
};

NodeId measured_child(LayoutTree& tree, Unit width, Unit height, Unit baseline,
                      ItemPosition alignment, bool rtl) {
    Style style;
    style.box.direction = rtl ? Direction::Rtl : Direction::Ltr;
    style.align_self = {alignment};
    style.flex_shrink = 0;
    return tree.create_node(style, [=](const MeasureRequest& request) {
        const Unit measured_width = request.known_content_width.value_or(width);
        const Unit measured_height = request.known_content_height.value_or(height);
        return Measurement{{measured_width, measured_height}, baseline, baseline,
            Size{width, height}};
    });
}

void run_case(std::size_t direction, std::size_t wrap, std::size_t parent_rtl,
              std::size_t child_rtl, std::size_t dimensions, std::size_t alignment) {
    const bool row = direction < 2;
    const Size size = dimensions == 0 ? Size{120, 100}
        : dimensions == 1 ? (row ? Size{70, 100} : Size{120, 38})
        : (row ? Size{120, 25} : Size{50, 100});
    LayoutTree tree;
    Style parent_style;
    parent_style.display = Display::Flex;
    parent_style.box.size = {Length::length(size.width), Length::length(size.height)};
    parent_style.box.direction = parent_rtl ? Direction::Rtl : Direction::Ltr;
    parent_style.flex_direction = directions.at(direction);
    parent_style.flex_wrap = wraps.at(wrap);
    parent_style.align_items = {ItemPosition::End};
    parent_style.gap = {Length::length(3), Length::length(3)};
    const auto parent = tree.create_node(parent_style);
    const auto first = measured_child(tree, 40, 20, 12,
        alignments.at(alignment)[0], false);
    const auto second = measured_child(tree, 45, 30, 7,
        alignments.at(alignment)[1], child_rtl != 0);
    tree.append_child(parent, first);
    tree.append_child(parent, second);
    const auto name = "baseline-matrix-" + std::to_string(direction) + '-'
        + std::to_string(wrap) + '-' + std::to_string(parent_rtl) + '-'
        + std::to_string(child_rtl) + '-' + std::to_string(dimensions)
        + '-' + std::to_string(alignment);
    reference::write_case(std::cout, name, tree.compute(parent,
        {AvailableSpace::definite(size.width), AvailableSpace::definite(size.height)}),
        {{1, parent}, {2, first}, {3, second}});
}
} // namespace

int main() {
    try {
        std::cout << "{\"protocol\":\"AXISLY_BASELINE_MATRIX_V1\",\"cases\":[";
        bool first = true;
        for (std::size_t direction = 0; direction < 4; ++direction)
        for (std::size_t wrap = 0; wrap < 3; ++wrap)
        for (std::size_t parent_rtl = 0; parent_rtl < 2; ++parent_rtl)
        for (std::size_t child_rtl = 0; child_rtl < 2; ++child_rtl)
        for (std::size_t dimensions = 0; dimensions < 3; ++dimensions)
        for (std::size_t alignment = 0; alignment < 4; ++alignment) {
            if (!first) std::cout << ',';
            first = false;
            run_case(direction, wrap, parent_rtl, child_rtl, dimensions, alignment);
        }
        std::cout << "]}\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
