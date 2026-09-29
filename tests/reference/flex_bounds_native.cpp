#include "css_reference_output.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace axisly;
using namespace axisly::css;

constexpr std::array directions{
    FlexDirection::Row, FlexDirection::RowReverse,
    FlexDirection::Column, FlexDirection::ColumnReverse,
};
constexpr std::array wraps{
    FlexWrap::NoWrap, FlexWrap::Wrap, FlexWrap::WrapReverse,
    FlexWrap::Balance, FlexWrap::BalanceReverse,
};
constexpr std::array main_sizes{42.0F, 30.0F, 25.0F, 18.0F};
constexpr std::array cross_sizes{18.0F, 24.0F, 16.0F, 20.0F};

void set_bounds(Style& style, std::size_t variant, std::size_t item, bool row) {
    auto& minimum = row ? style.box.min_size.width : style.box.min_size.height;
    auto& maximum = row ? style.box.max_size.width : style.box.max_size.height;
    if ((variant == 0 || variant == 2) && item == 0) {
        maximum = Length::length(variant == 0 ? 20.0F : 28.0F);
    }
    if (variant == 1 && item == 0) minimum = Length::length(58);
    if (variant == 2 && item == 1) minimum = Length::length(45);
    if (variant == 3 && item == 2) maximum = Length::length(12);
    if (variant == 3 && item == 3) minimum = Length::length(26);
}

void run_case(std::size_t scenario, std::size_t direction, std::size_t wrap,
              std::size_t variant, std::size_t rtl) {
    const bool row = direction < 2;
    LayoutTree tree;
    Style root_style;
    root_style.flex_direction = directions[direction];
    root_style.flex_wrap = wraps[wrap];
    if (wrap >= 3) root_style.flex_line_count = 2;
    root_style.align_items = {ItemPosition::Start};
    root_style.box.direction = rtl ? Direction::Rtl : Direction::Ltr;
    if (scenario == 0) {
        root_style.box.size = {Length::length(row ? 72.0F : 68.0F),
            Length::length(row ? 68.0F : 72.0F)};
    } else if (scenario == 2) {
        (row ? root_style.box.max_size.width : root_style.box.max_size.height)
            = Length::length(40);
    } else if (scenario == 3) {
        (row ? root_style.box.min_size.width : root_style.box.min_size.height)
            = Length::length(80);
    }
    root_style.box.padding = {Length::length(3), Length::length(3),
        Length::length(3), Length::length(3)};
    root_style.gap = {Length::length(4), Length::length(6)};
    const auto root = tree.create_node(root_style);
    std::vector<reference::IdentifiedNode> nodes{{1, root}};
    for (std::size_t index = 0; index < main_sizes.size(); ++index) {
        Style child_style;
        child_style.box.size = {Length::length(row ? main_sizes[index] : cross_sizes[index]),
            Length::length(row ? cross_sizes[index] : main_sizes[index])};
        set_bounds(child_style, variant, index, row);
        if (variant == 4 && index == 3) {
            auto& margin_start = row ? child_style.box.margin.left : child_style.box.margin.top;
            auto& margin_end = row ? child_style.box.margin.right : child_style.box.margin.bottom;
            auto& padding_start = row ? child_style.box.padding.left : child_style.box.padding.top;
            auto& padding_end = row ? child_style.box.padding.right : child_style.box.padding.bottom;
            auto& border_start = row ? child_style.box.border.left : child_style.box.border.top;
            auto& border_end = row ? child_style.box.border.right : child_style.box.border.bottom;
            margin_start = margin_end = Length::length(-9);
            padding_start = padding_end = Length::length(5);
            border_start = border_end = Length::length(1);
        }
        const auto child = tree.create_node(child_style);
        tree.append_child(root, child);
        nodes.emplace_back(static_cast<int>(index) + 2, child);
    }
    const auto name = "flex-bounds-" + std::to_string(scenario) + '-'
        + std::to_string(direction) + '-'
        + std::to_string(wrap) + '-' + std::to_string(variant) + '-'
        + std::to_string(rtl);
    reference::write_case(std::cout, name,
        tree.compute(root, {AvailableSpace::definite(row ? 72.0F : 68.0F),
            AvailableSpace::definite(row ? 68.0F : 72.0F)}), nodes);
}
} // namespace

int main() {
    try {
        std::cout << "{\"protocol\":\"AXISLY_FLEX_BOUNDS_MATRIX_V1\",\"cases\":[";
        bool first = true;
        for (std::size_t scenario = 0; scenario < 4; ++scenario)
        for (std::size_t direction = 0; direction < directions.size(); ++direction)
        for (std::size_t wrap = 0; wrap < wraps.size(); ++wrap)
        for (std::size_t variant = 0; variant < 5; ++variant)
        for (std::size_t rtl = 0; rtl < 2; ++rtl) {
            if (!first) std::cout << ',';
            first = false;
            run_case(scenario, direction, wrap, variant, rtl);
        }
        std::cout << "]}\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
