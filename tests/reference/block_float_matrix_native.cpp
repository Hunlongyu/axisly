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
constexpr std::array alignments{
    ContentAlignment{ContentPosition::End},
    ContentAlignment{ContentPosition::Center},
    ContentAlignment{ContentPosition::SpaceAround},
    ContentAlignment{ContentPosition::SpaceBetween},
    ContentAlignment{ContentPosition::Stretch},
    ContentAlignment{ContentPosition::Center, AlignmentSafety::Safe},
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
                     std::size_t display, std::size_t clear,
                     std::size_t rtl) {
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Block;
    root_style.box.size.width = Length::length(100);
    root_style.box.padding = {Length::length(2), Length::length(2),
        Length::length(2), Length::length(2)};
    root_style.box.direction = rtl ? Direction::Rtl : Direction::Ltr;
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
    outer_style.box.padding.right = Length::length(padding ? 2.0f : 0.0f);
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
        + '-' + std::to_string(clear) + '-' + std::to_string(rtl);
    reference::write_case(std::cout, name, tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::max_content()}),
        {{1, root}, {2, floating}, {3, outer}, {4, inner},
         {5, follower}, {6, tail}});
}

void run_deep_case(std::size_t float_side, std::size_t outer_padding,
                   std::size_t inner_padding, std::size_t display,
                   std::size_t clear, std::size_t rtl) {
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Block;
    root_style.box.size.width = Length::length(100);
    root_style.box.padding = {Length::length(2), Length::length(2),
        Length::length(2), Length::length(2)};
    root_style.box.direction = rtl ? Direction::Rtl : Direction::Ltr;
    const auto root = tree.create_node(root_style);

    Style float_style;
    float_style.display = Display::Block;
    float_style.float_side = float_side == 0 ? FloatSide::Left : FloatSide::Right;
    float_style.box.size = {Length::length(25), Length::length(30)};
    const auto floating = tree.create_node(float_style);
    tree.append_child(root, floating);

    Style outer_style;
    outer_style.display = Display::Block;
    outer_style.box.margin.left = Length::length(3);
    outer_style.box.margin.top = Length::length(5);
    outer_style.box.padding.left = Length::length(outer_padding ? 3.0f : 0.0f);
    outer_style.box.padding.top = Length::length(outer_padding ? 3.0f : 0.0f);
    const auto outer = tree.create_node(outer_style);
    tree.append_child(root, outer);

    Style inner_style;
    inner_style.display = Display::Block;
    inner_style.box.margin.left = Length::length(4);
    inner_style.box.margin.top = Length::length(2);
    inner_style.box.padding.left = Length::length(inner_padding ? 2.0f : 0.0f);
    inner_style.box.padding.top = Length::length(inner_padding ? 2.0f : 0.0f);
    const auto inner = tree.create_node(inner_style);
    tree.append_child(outer, inner);

    Style follower_style;
    follower_style.display = display == 0 ? Display::FlowRoot : Display::Flex;
    follower_style.clear = clear ? ClearSide::Both : ClearSide::None;
    follower_style.box.size = {Length::length(20), Length::length(8)};
    const auto follower = tree.create_node(follower_style);
    tree.append_child(inner, follower);

    Style tail_style;
    tail_style.display = Display::FlowRoot;
    tail_style.box.size = {Length::length(10), Length::length(5)};
    const auto tail = tree.create_node(tail_style);
    tree.append_child(root, tail);

    const auto name = std::string("block-float-deep-")
        + std::to_string(float_side) + '-' + std::to_string(outer_padding)
        + '-' + std::to_string(inner_padding) + '-'
        + std::to_string(display) + '-' + std::to_string(clear)
        + '-' + std::to_string(rtl);
    reference::write_case(std::cout, name, tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::max_content()}),
        {{1, root}, {2, floating}, {3, outer}, {4, inner},
         {5, follower}, {6, tail}});
}

void run_align_case(std::size_t root_kind, std::size_t align,
                    std::size_t floated, std::size_t rtl,
                    std::size_t compact) {
    LayoutTree tree;
    Style root_style;
    root_style.display = root_kind == 0 ? Display::Block : Display::FlowRoot;
    root_style.box.size = {Length::length(100),
        Length::length(compact ? 20.0f : 80.0f)};
    root_style.box.padding = {Length::length(2), Length::length(2),
        Length::length(2), Length::length(2)};
    root_style.box.direction = rtl ? Direction::Rtl : Direction::Ltr;
    root_style.align_content = alignments[align];
    const auto root = tree.create_node(root_style);

    std::vector<reference::IdentifiedNode> nodes{{1, root}};
    if (floated) {
        Style float_style;
        float_style.display = Display::Block;
        float_style.float_side = FloatSide::Left;
        float_style.box.size = {Length::length(20), Length::length(20)};
        const auto floating = tree.create_node(float_style);
        tree.append_child(root, floating);
        nodes.emplace_back(2, floating);
    }
    Style first_style;
    first_style.display = Display::FlowRoot;
    first_style.box.size = {Length::length(20), Length::length(10)};
    first_style.box.margin.top = Length::length(5);
    const auto first = tree.create_node(first_style);
    tree.append_child(root, first);
    nodes.emplace_back(3, first);

    Style second_style;
    second_style.display = Display::Block;
    second_style.box.size = {Length::length(20), Length::length(8)};
    second_style.box.margin.top = Length::length(3);
    const auto second = tree.create_node(second_style);
    tree.append_child(root, second);
    nodes.emplace_back(4, second);

    const auto name = std::string("block-align-")
        + std::to_string(root_kind) + '-' + std::to_string(align)
        + '-' + std::to_string(floated) + '-' + std::to_string(rtl)
        + '-' + std::to_string(compact);
    reference::write_case(std::cout, name, tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::max_content()}), nodes);
}

void run_nested_align_case(std::size_t float_side,
                           std::size_t align, std::size_t rtl) {
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Block;
    root_style.box.size.width = Length::length(100);
    root_style.box.padding = {Length::length(2), Length::length(2),
        Length::length(2), Length::length(2)};
    root_style.box.direction = rtl ? Direction::Rtl : Direction::Ltr;
    const auto root = tree.create_node(root_style);

    Style float_style;
    float_style.display = Display::Block;
    float_style.float_side = float_side == 0 ? FloatSide::Left : FloatSide::Right;
    float_style.box.size = {Length::length(30), Length::length(30)};
    const auto floating = tree.create_node(float_style);
    tree.append_child(root, floating);

    Style aligned_style;
    aligned_style.display = Display::Block;
    aligned_style.box.size.height = Length::length(25);
    aligned_style.box.margin.top = Length::length(3);
    aligned_style.align_content = align == 0
        ? ContentAlignment{ContentPosition::End}
        : ContentAlignment{ContentPosition::Center};
    const auto aligned = tree.create_node(aligned_style);
    tree.append_child(root, aligned);

    Style inner_style;
    inner_style.display = Display::Flex;
    inner_style.box.size = {Length::length(15), Length::length(5)};
    const auto inner = tree.create_node(inner_style);
    tree.append_child(aligned, inner);

    Style tail_style;
    tail_style.display = Display::Block;
    tail_style.box.size.height = Length::length(8);
    const auto tail = tree.create_node(tail_style);
    tree.append_child(root, tail);

    const auto name = std::string("block-align-nested-")
        + std::to_string(float_side) + '-' + std::to_string(align)
        + '-' + std::to_string(rtl);
    reference::write_case(std::cout, name, tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::max_content()}),
        {{1, root}, {2, floating}, {3, aligned}, {4, inner}, {5, tail}});
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
        for (std::size_t clear = 0; clear < 2; ++clear)
        for (std::size_t rtl = 0; rtl < 2; ++rtl) {
            std::cout << ',';
            run_nested_case(root_float, padding, top_margin,
                inner_float, display, clear, rtl);
        }
        for (std::size_t float_side = 0; float_side < 2; ++float_side)
        for (std::size_t outer_padding = 0; outer_padding < 2; ++outer_padding)
        for (std::size_t inner_padding = 0; inner_padding < 2; ++inner_padding)
        for (std::size_t display = 0; display < 2; ++display)
        for (std::size_t clear = 0; clear < 2; ++clear)
        for (std::size_t rtl = 0; rtl < 2; ++rtl) {
            std::cout << ',';
            run_deep_case(float_side, outer_padding, inner_padding,
                display, clear, rtl);
        }
        for (std::size_t root_kind = 0; root_kind < 2; ++root_kind)
        for (std::size_t align = 0; align < alignments.size(); ++align)
        for (std::size_t floated = 0; floated < 2; ++floated)
        for (std::size_t rtl = 0; rtl < 2; ++rtl)
        for (std::size_t compact = 0; compact < 2; ++compact) {
            std::cout << ',';
            run_align_case(root_kind, align, floated, rtl, compact);
        }
        for (std::size_t float_side = 0; float_side < 2; ++float_side)
        for (std::size_t align = 0; align < 2; ++align)
        for (std::size_t rtl = 0; rtl < 2; ++rtl) {
            std::cout << ',';
            run_nested_align_case(float_side, align, rtl);
        }
        std::cout << "]}\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
