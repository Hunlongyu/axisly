#include "css_reference_output.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace axisly;
using namespace axisly::css;

constexpr std::array displays{
    Display::Block, Display::FlowRoot, Display::Flex, Display::Grid,
};
constexpr std::array clears{
    ClearSide::None, ClearSide::Left, ClearSide::Right, ClearSide::Both,
};

void run_case(std::size_t root_kind, std::size_t float_side,
              std::size_t clear, std::size_t follower_display,
              std::size_t follower_width, std::size_t rtl) {
    LayoutTree tree;
    Style root_style;
    root_style.display = root_kind == 0 ? Display::Block : Display::FlowRoot;
    root_style.box.size.width = Length::length(100);
    root_style.box.padding = {Length::length(2), Length::length(2),
        Length::length(2), Length::length(2)};
    root_style.box.direction = rtl ? Direction::Rtl : Direction::Ltr;
    const auto root = tree.create_node(root_style);

    Style first_style;
    first_style.display = Display::Block;
    first_style.float_side = float_side == 0 ? FloatSide::Left : FloatSide::Right;
    first_style.box.size = {Length::length(30), Length::length(25)};
    const auto first = tree.create_node(first_style);
    tree.append_child(root, first);

    Style second_style;
    second_style.display = Display::Block;
    second_style.float_side = float_side == 0 ? FloatSide::Right : FloatSide::Left;
    second_style.box.size = {Length::length(20), Length::length(30)};
    const auto second = tree.create_node(second_style);
    tree.append_child(root, second);

    Style follower_style;
    follower_style.display = displays[follower_display];
    follower_style.clear = clears[clear];
    follower_style.box.size.height = Length::length(12);
    if (follower_width) follower_style.box.size.width = Length::length(80);
    follower_style.box.margin = {Length::length(3), Length::length(5),
        Length::length(2), Length::length(0)};
    const auto follower = tree.create_node(follower_style);
    tree.append_child(root, follower);

    Style content_style;
    content_style.box.size = {Length::length(10), Length::length(8)};
    const auto content = tree.create_node(content_style);
    tree.append_child(follower, content);

    Style tail_style;
    tail_style.display = Display::Block;
    tail_style.box.size.height = Length::length(5);
    tail_style.box.margin.top = Length::length(4);
    const auto tail = tree.create_node(tail_style);
    tree.append_child(root, tail);

    const auto name = std::string("block-float-matrix-")
        + std::to_string(root_kind) + '-' + std::to_string(float_side)
        + '-' + std::to_string(clear) + '-'
        + std::to_string(follower_display) + '-'
        + std::to_string(follower_width) + '-' + std::to_string(rtl);
    reference::write_case(std::cout, name, tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::max_content()}),
        {{1, root}, {2, first}, {3, second}, {4, follower},
         {5, content}, {6, tail}});
}

void run_nested_case(std::size_t root_float, std::size_t padding,
                     std::size_t top_margin, std::size_t inner_float,
                     std::size_t display, std::size_t clear) {
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Block;
    root_style.box.size.width = Length::length(100);
    root_style.box.padding = {Length::length(2), Length::length(2),
        Length::length(2), Length::length(2)};
    const auto root = tree.create_node(root_style);

    Style root_float_style;
    root_float_style.display = Display::Block;
    root_float_style.float_side = root_float == 0
        ? FloatSide::Left : FloatSide::Right;
    root_float_style.box.size = {Length::length(20), Length::length(20)};
    const auto floating = tree.create_node(root_float_style);
    tree.append_child(root, floating);

    Style outer_style;
    outer_style.display = Display::Block;
    outer_style.box.margin.left = Length::length(3);
    outer_style.box.margin.top = Length::length(top_margin ? 4.0f : 0.0f);
    outer_style.box.padding.left = Length::length(padding ? 3.0f : 0.0f);
    outer_style.box.padding.top = Length::length(padding ? 3.0f : 0.0f);
    const auto outer = tree.create_node(outer_style);
    tree.append_child(root, outer);

    Style inner_float_style;
    inner_float_style.display = Display::Block;
    inner_float_style.float_side = inner_float == 0
        ? FloatSide::Left : FloatSide::Right;
    inner_float_style.box.size = {Length::length(10), Length::length(12)};
    const auto inner = tree.create_node(inner_float_style);
    tree.append_child(outer, inner);

    Style follower_style;
    follower_style.display = display == 0 ? Display::FlowRoot : Display::Flex;
    follower_style.clear = clear ? ClearSide::Both : ClearSide::None;
    follower_style.box.size = {Length::length(20), Length::length(8)};
    const auto follower = tree.create_node(follower_style);
    tree.append_child(outer, follower);

    Style tail_style;
    tail_style.display = Display::FlowRoot;
    tail_style.box.size = {Length::length(15), Length::length(10)};
    const auto tail = tree.create_node(tail_style);
    tree.append_child(root, tail);

    const auto name = std::string("block-float-nested-")
        + std::to_string(root_float) + '-' + std::to_string(padding)
        + '-' + std::to_string(top_margin) + '-'
        + std::to_string(inner_float) + '-' + std::to_string(display)
        + '-' + std::to_string(clear);
    reference::write_case(std::cout, name, tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::max_content()}),
        {{1, root}, {2, floating}, {3, outer}, {4, inner},
         {5, follower}, {6, tail}});
}
} // namespace

int main() {
    try {
        std::cout << "{\"protocol\":\"AXISLY_BLOCK_FLOAT_MATRIX_V1\",\"cases\":[";
        bool first = true;
        for (std::size_t root_kind = 0; root_kind < 2; ++root_kind)
        for (std::size_t float_side = 0; float_side < 2; ++float_side)
        for (std::size_t clear = 0; clear < clears.size(); ++clear)
        for (std::size_t follower_display = 0; follower_display < displays.size(); ++follower_display)
        for (std::size_t follower_width = 0; follower_width < 2; ++follower_width)
        for (std::size_t rtl = 0; rtl < 2; ++rtl) {
            if (!first) std::cout << ',';
            first = false;
            run_case(root_kind, float_side, clear, follower_display,
                follower_width, rtl);
        }
        for (std::size_t root_float = 0; root_float < 2; ++root_float)
        for (std::size_t padding = 0; padding < 2; ++padding)
        for (std::size_t top_margin = 0; top_margin < 2; ++top_margin)
        for (std::size_t inner_float = 0; inner_float < 2; ++inner_float)
        for (std::size_t display = 0; display < 2; ++display)
        for (std::size_t clear = 0; clear < 2; ++clear) {
            std::cout << ',';
            run_nested_case(root_float, padding, top_margin,
                inner_float, display, clear);
        }
        std::cout << "]}\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
