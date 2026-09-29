#include "css_reference_output.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace axisly;
using namespace axisly::css;

constexpr std::array positions{
    ItemPosition::Start, ItemPosition::Center, ItemPosition::End,
};
constexpr std::array content_positions{
    ContentPosition::Start, ContentPosition::Center, ContentPosition::End,
};

Style parent_style(std::size_t mode, std::size_t alignment, bool rtl) {
    Style style;
    style.display = mode == 0 ? Display::Block : mode == 1 ? Display::FlowRoot
        : mode == 5 ? Display::Grid : Display::Flex;
    style.box.direction = rtl ? Direction::Rtl : Direction::Ltr;
    style.box.size = {Length::length(120), Length::length(80)};
    style.box.padding = {Length::length(5), Length::length(7),
                         Length::length(6), Length::length(8)};
    style.box.border = {Length::length(2), Length::length(2),
                        Length::length(2), Length::length(2)};
    style.gap = {Length::length(4), Length::length(3)};
    style.align_items = {positions.at(alignment)};
    style.justify_items = {positions.at(alignment)};
    style.align_content = {content_positions.at(alignment)};
    style.justify_content = {content_positions.at(alignment)};
    if (mode == 2) style.flex_direction = FlexDirection::Row;
    else if (mode == 3) style.flex_direction = FlexDirection::RowReverse;
    else if (mode == 4 || mode == 8) style.flex_direction = FlexDirection::Column;
    else if (mode == 6) style.flex_direction = FlexDirection::ColumnReverse;
    if (mode >= 7) style.flex_wrap = FlexWrap::WrapReverse;
    if (mode == 5) {
        style.grid.template_columns = {TrackSizing::fixed(30), TrackSizing::fixed(40)};
        style.grid.template_rows = {TrackSizing::fixed(20), TrackSizing::fixed(20)};
    }
    return style;
}

Style positioned_style(std::size_t scenario, bool rtl) {
    Style style;
    style.display = Display::Block;
    style.position = Position::Absolute;
    style.box.direction = rtl ? Direction::Rtl : Direction::Ltr;
    style.box.size = {Length::length(20), Length::length(10)};
    switch (scenario) {
    case 1:
    case 5:
        style.inset.left = scenario == 5 ? Length::percent(0.25F) : Length::length(10);
        style.inset.top = scenario == 5 ? Length::percent(0.5F) : Length::length(5);
        style.box.margin.left = Length::length(3);
        style.box.margin.top = Length::length(4);
        break;
    case 2:
    case 6:
        style.inset.right = scenario == 6 ? Length::percent(0.1F) : Length::length(10);
        style.inset.bottom = scenario == 6 ? Length::percent(0.25F) : Length::length(5);
        style.box.margin.right = Length::length(5);
        style.box.margin.bottom = Length::length(6);
        break;
    case 3:
    case 4:
    case 7:
    case 8:
    case 9:
        style.inset = {Length::length(10), Length::length(5),
                       Length::length(20), Length::length(15)};
        if (scenario == 4) {
            style.box.size = {};
        } else if (scenario == 7) {
            style.box.margin = {Length::automatic(), Length::automatic(),
                                Length::automatic(), Length::automatic()};
        } else if (scenario == 8) {
            style.box.margin.left = Length::automatic();
            style.box.margin.top = Length::automatic();
        } else if (scenario == 9) {
            style.box.margin = {Length::length(-3), Length::length(-4),
                                Length::length(-5), Length::length(-6)};
        }
        break;
    case 10:
        style.box.size = {};
        style.box.margin.left = Length::length(3);
        style.box.margin.top = Length::length(4);
        break;
    case 11:
    case 12:
        style.grid.column = {GridLine::index(2), GridLine::index(3)};
        style.grid.row = {GridLine::index(2), GridLine::index(3)};
        if (scenario == 12) {
            style.box.size = {};
            style.inset.left = Length::percent(0.1F);
            style.inset.top = Length::percent(0.25F);
        }
        break;
    case 13:
    case 14:
        style.box.margin = {Length::automatic(), Length::automatic(),
                            Length::automatic(), Length::automatic()};
        if (scenario == 14) {
            style.inset.left = Length::length(10);
            style.inset.top = Length::length(5);
        }
        break;
    default: break;
    }
    return style;
}

void run_case(std::size_t mode, std::size_t alignment,
              std::size_t parent_rtl, std::size_t child_rtl,
              std::size_t scenario) {
    LayoutTree tree;
    const auto parent = tree.create_node(parent_style(mode, alignment, parent_rtl != 0));
    Style before_style;
    before_style.display = Display::Block;
    before_style.box.size = {Length::length(25), Length::length(12)};
    before_style.flex_shrink = 0;
    const auto before = tree.create_node(before_style);
    const auto absolute = tree.create_node(positioned_style(scenario, child_rtl != 0));
    Style inner_style;
    inner_style.display = Display::Block;
    inner_style.box.size = {Length::length(12), Length::length(6)};
    const auto inner = tree.create_node(inner_style);
    Style after_style;
    after_style.display = Display::Block;
    after_style.box.size = {Length::length(20), Length::length(9)};
    after_style.flex_shrink = 0;
    const auto after = tree.create_node(after_style);
    tree.append_child(parent, before);
    tree.append_child(parent, absolute);
    tree.append_child(absolute, inner);
    tree.append_child(parent, after);
    const auto name = "position-matrix-" + std::to_string(mode) + '-'
        + std::to_string(alignment) + '-' + std::to_string(parent_rtl) + '-'
        + std::to_string(child_rtl) + '-' + std::to_string(scenario);
    reference::write_case(std::cout, name, tree.compute(parent,
        {AvailableSpace::definite(120), AvailableSpace::definite(80)}),
        {{1, parent}, {2, before}, {3, absolute}, {4, inner}, {5, after}}, true);
}
} // namespace

int main() {
    try {
        std::cout << "{\"protocol\":\"AXISLY_POSITION_MATRIX_V1\",\"cases\":[";
        bool first = true;
        for (std::size_t mode = 0; mode < 9; ++mode)
        for (std::size_t alignment = 0; alignment < 3; ++alignment)
        for (std::size_t parent_rtl = 0; parent_rtl < 2; ++parent_rtl)
        for (std::size_t child_rtl = 0; child_rtl < 2; ++child_rtl)
        for (std::size_t scenario = 0; scenario < 15; ++scenario) {
            if (mode != 5 && (scenario == 11 || scenario == 12)) continue;
            if (!first) std::cout << ',';
            first = false;
            run_case(mode, alignment, parent_rtl, child_rtl, scenario);
        }
        std::cout << "]}\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
