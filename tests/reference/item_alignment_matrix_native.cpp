#include "css_reference_output.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace axisly;
using namespace axisly::css;

constexpr std::array positions{
    ItemPosition::Start, ItemPosition::End, ItemPosition::FlexStart,
    ItemPosition::FlexEnd, ItemPosition::SelfStart, ItemPosition::SelfEnd,
    ItemPosition::Center, ItemPosition::Baseline, ItemPosition::Stretch,
};
constexpr std::array directions{
    FlexDirection::Row, FlexDirection::RowReverse,
    FlexDirection::Column, FlexDirection::ColumnReverse,
};

void run_case(std::size_t scenario, std::size_t mode, std::size_t alignment,
              std::size_t parent_rtl, std::size_t child_rtl) {
    const bool flex = mode < 8;
    const bool row = flex && mode % 4 < 2;
    LayoutTree tree;
    Style parent_style;
    parent_style.display = flex ? Display::Flex : Display::Grid;
    parent_style.box.size = {Length::length(120), Length::length(100)};
    parent_style.box.direction = parent_rtl ? Direction::Rtl : Direction::Ltr;
    parent_style.align_items = {ItemPosition::End};
    parent_style.justify_items = {ItemPosition::End};
    if (flex) {
        parent_style.flex_direction = directions.at(mode % 4);
        parent_style.flex_wrap = mode >= 4 ? FlexWrap::WrapReverse : FlexWrap::NoWrap;
    } else {
        parent_style.grid.template_columns = {TrackSizing::fixed(80)};
        parent_style.grid.template_rows = {TrackSizing::fixed(60)};
        parent_style.align_content = {ContentPosition::Start};
        parent_style.justify_content = {ContentPosition::Start};
    }
    const auto parent = tree.create_node(parent_style);
    Style child_style;
    child_style.box.direction = child_rtl ? Direction::Rtl : Direction::Ltr;
    child_style.flex_shrink = 0;
    const ItemAlignment value{positions.at(alignment % positions.size()),
        alignment < positions.size() ? AlignmentSafety::Unsafe : AlignmentSafety::Safe};
    if (flex) child_style.align_self = value;
    else if (mode == 8) {
        child_style.justify_self = value;
        child_style.align_self = {ItemPosition::Start};
    } else {
        child_style.align_self = value;
        child_style.justify_self = {ItemPosition::Start};
    }
    if (scenario < 2) {
        child_style.box.size = {Length::length(scenario == 0 ? Unit{40} : Unit{140}),
            Length::length(scenario == 0 ? Unit{20} : Unit{110})};
    } else if (flex) {
        if (row) child_style.box.size.width = Length::length(40);
        else child_style.box.size.height = Length::length(20);
    }
    const auto child = tree.create_node(child_style, [](const MeasureRequest& request) {
        return Measurement{{request.known_content_width.value_or(Unit{40}),
            request.known_content_height.value_or(Unit{20})}, {}, {}, Size{40, 20}};
    });
    tree.append_child(parent, child);
    const auto name = "item-alignment-" + std::to_string(scenario) + '-'
        + std::to_string(mode) + '-' + std::to_string(alignment) + '-'
        + std::to_string(parent_rtl) + '-' + std::to_string(child_rtl);
    reference::write_case(std::cout, name, tree.compute(parent,
        {AvailableSpace::definite(120), AvailableSpace::definite(100)}),
        {{1, parent}, {2, child}}, true);
}
} // namespace

int main() {
    try {
        std::cout << "{\"protocol\":\"AXISLY_ITEM_ALIGNMENT_MATRIX_V1\",\"cases\":[";
        bool first = true;
        for (std::size_t scenario = 0; scenario < 3; ++scenario)
        for (std::size_t mode = 0; mode < 10; ++mode)
        for (std::size_t alignment = 0; alignment < 16; ++alignment)
        for (std::size_t parent_rtl = 0; parent_rtl < 2; ++parent_rtl)
        for (std::size_t child_rtl = 0; child_rtl < 2; ++child_rtl) {
            if (!first) std::cout << ',';
            first = false;
            run_case(scenario, mode, alignment, parent_rtl, child_rtl);
        }
        std::cout << "]}\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
