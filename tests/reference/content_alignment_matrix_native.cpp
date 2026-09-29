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
    ContentPosition::Start, ContentPosition::End,
    ContentPosition::FlexStart, ContentPosition::FlexEnd,
    ContentPosition::Center, ContentPosition::Stretch,
    ContentPosition::SpaceBetween, ContentPosition::SpaceEvenly,
    ContentPosition::SpaceAround,
};
constexpr std::array directions{
    FlexDirection::Row, FlexDirection::RowReverse,
    FlexDirection::Column, FlexDirection::ColumnReverse,
};
constexpr std::array wraps{FlexWrap::NoWrap, FlexWrap::Wrap, FlexWrap::WrapReverse};

void run_case(std::size_t mode, std::size_t alignment,
              std::size_t scenario, std::size_t rtl) {
    const bool flex = mode < 24;
    const std::size_t axis = flex ? mode % 2 : mode - 24;
    const std::size_t direction = flex ? mode / 2 % 4 : 0;
    const bool row = direction < 2;
    Size size{};
    if (flex) {
        size = row ? Size{Unit{90}, scenario == 1 ? Unit{35} : Unit{120}}
                   : Size{scenario == 1 ? Unit{55} : Unit{120}, Unit{50}};
    } else {
        size = {scenario == 1 ? Unit{80} : Unit{120},
                scenario == 1 ? Unit{50} : Unit{100}};
    }
    LayoutTree tree;
    Style parent_style;
    parent_style.display = flex ? Display::Flex : Display::Grid;
    parent_style.box.size = {Length::length(size.width), Length::length(size.height)};
    parent_style.box.direction = rtl ? Direction::Rtl : Direction::Ltr;
    parent_style.gap = {Length::length(4), Length::length(4)};
    parent_style.align_items = {ItemPosition::Start};
    parent_style.justify_items = {ItemPosition::Start};
    const ContentAlignment value{positions.at(alignment % positions.size()),
        alignment < positions.size() ? AlignmentSafety::Unsafe : AlignmentSafety::Safe};
    parent_style.align_content = axis == 0 ? value
        : ContentAlignment{ContentPosition::Stretch};
    parent_style.justify_content = axis == 1 ? value
        : ContentAlignment{ContentPosition::Start};
    if (flex) {
        parent_style.flex_direction = directions.at(direction);
        parent_style.flex_wrap = wraps.at(mode / 8);
    } else {
        const std::size_t count = scenario == 2 ? 1 : 3;
        for (std::size_t index = 0; index < count; ++index) {
            parent_style.grid.template_columns.push_back(TrackSizing::fixed(
                index == 0 ? Unit{40} : index == 1 ? Unit{30} : Unit{20}));
            parent_style.grid.template_rows.push_back(TrackSizing::fixed(20));
        }
    }
    const auto parent = tree.create_node(parent_style);
    const std::size_t count = flex ? 4 : scenario == 2 ? 1 : 3;
    std::vector<reference::IdentifiedNode> nodes{{1, parent}};
    for (std::size_t index = 0; index < count; ++index) {
        Style child_style;
        Unit child_width = flex ? Unit{40} : Unit{10};
        Unit child_height = flex ? Unit{20} : Unit{10};
        if (flex && scenario == 2 && index == 0) {
            if (row) child_width = 110;
            else child_height = 65;
        }
        child_style.box.size = {Length::length(child_width),
                                Length::length(child_height)};
        child_style.flex_shrink = 0;
        const auto child = tree.create_node(child_style);
        tree.append_child(parent, child);
        nodes.emplace_back(static_cast<int>(index + 2), child);
    }
    const auto name = "content-alignment-" + std::to_string(mode) + '-'
        + std::to_string(alignment) + '-' + std::to_string(scenario)
        + '-' + std::to_string(rtl);
    reference::write_case(std::cout, name, tree.compute(parent,
        {AvailableSpace::definite(size.width), AvailableSpace::definite(size.height)}),
        nodes, true);
}
} // namespace

int main() {
    try {
        std::cout << "{\"protocol\":\"AXISLY_CONTENT_ALIGNMENT_MATRIX_V1\",\"cases\":[";
        bool first = true;
        for (std::size_t mode = 0; mode < 26; ++mode)
        for (std::size_t alignment = 0; alignment < 14; ++alignment)
        for (std::size_t scenario = 0; scenario < 3; ++scenario)
        for (std::size_t rtl = 0; rtl < 2; ++rtl) {
            if (!first) std::cout << ',';
            first = false;
            run_case(mode, alignment, scenario, rtl);
        }
        std::cout << "]}\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
