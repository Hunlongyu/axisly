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
};
constexpr std::array grow_bases{12.0F, 14.0F, 10.0F, 8.0F};
constexpr std::array shrink_bases{32.0F, 28.0F, 20.0F, 15.0F};
constexpr std::array cross_sizes{18.0F, 24.0F, 16.0F, 20.0F};

void configure_item(Style& style, std::size_t variant,
                    std::size_t index, bool row) {
    const bool shrinking = variant == 1 || variant == 3;
    const Unit main = shrinking ? shrink_bases[index] : grow_bases[index];
    style.box.size = {Length::length(row ? main : cross_sizes[index]),
        Length::length(row ? cross_sizes[index] : main)};
    if (variant == 0 || variant == 4 || variant == 5) {
        style.flex_grow = index == 1 ? 2.0F : index == 2 ? 0.0F : 1.0F;
    } else if (variant == 2) {
        style.flex_grow = index == 0 ? 0.2F : index == 1 ? 0.3F : 0.0F;
    }
    if (variant == 1) {
        style.flex_shrink = index == 1 ? 2.0F : index == 2 ? 0.0F : 1.0F;
    } else if (variant == 3) {
        style.flex_shrink = index == 0 ? 0.2F : index == 1 ? 0.3F : 0.0F;
    } else {
        style.flex_shrink = 0;
    }
    if (variant == 4) {
        constexpr std::array bases{10.0F, 20.0F, 15.0F, 5.0F};
        style.flex_basis = Length::length(bases[index]);
    } else if (variant == 5) {
        switch (index) {
        case 0: style.flex_basis = Length::percent(0.25F); break;
        case 1: style.flex_basis = Length::percent(0.40F); break;
        case 3: style.flex_basis = Length::percent(0.10F); break;
        default: break;
        }
    }
}

void run_case(std::size_t direction, std::size_t wrap,
              std::size_t variant, std::size_t rtl) {
    const bool row = direction < 2;
    LayoutTree tree;
    Style root_style;
    root_style.flex_direction = directions[direction];
    root_style.flex_wrap = wraps[wrap];
    root_style.align_items = {ItemPosition::Start};
    root_style.box.direction = rtl ? Direction::Rtl : Direction::Ltr;
    root_style.box.size = {Length::length(row ? 72.0F : 68.0F),
        Length::length(row ? 68.0F : 72.0F)};
    root_style.box.padding = {Length::length(3), Length::length(3),
        Length::length(3), Length::length(3)};
    root_style.gap = {Length::length(4), Length::length(6)};
    const auto root = tree.create_node(root_style);
    std::vector<reference::IdentifiedNode> nodes{{1, root}};
    for (std::size_t index = 0; index < grow_bases.size(); ++index) {
        Style child_style;
        configure_item(child_style, variant, index, row);
        const auto child = tree.create_node(child_style);
        tree.append_child(root, child);
        nodes.emplace_back(static_cast<int>(index) + 2, child);
    }
    const auto name = "flex-factors-" + std::to_string(direction) + '-'
        + std::to_string(wrap) + '-' + std::to_string(variant) + '-'
        + std::to_string(rtl);
    reference::write_case(std::cout, name,
        tree.compute(root, {AvailableSpace::definite(row ? 72.0F : 68.0F),
            AvailableSpace::definite(row ? 68.0F : 72.0F)}), nodes);
}
} // namespace

int main() {
    try {
        std::cout << "{\"protocol\":\"AXISLY_FLEX_FACTORS_MATRIX_V1\",\"cases\":[";
        bool first = true;
        for (std::size_t direction = 0; direction < directions.size(); ++direction)
        for (std::size_t wrap = 0; wrap < wraps.size(); ++wrap)
        for (std::size_t variant = 0; variant < 6; ++variant)
        for (std::size_t rtl = 0; rtl < 2; ++rtl) {
            if (!first) std::cout << ',';
            first = false;
            run_case(direction, wrap, variant, rtl);
        }
        std::cout << "]}\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
