#include "css_reference_output.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace axisly::css;

constexpr std::array displays{Display::Block, Display::Flex, Display::Grid};
constexpr std::array display_names{"Block", "Flex", "Grid"};
constexpr std::array contains{
    Contain::None, Contain::Layout, Contain::Paint, Contain::Content,
};
constexpr std::array contain_names{"None", "Layout", "Paint", "Content"};

std::string name(const char* scenario, std::size_t display,
                 std::size_t contain) {
    return std::string{"contain-"} + scenario + '-'
        + display_names[display] + '-' + contain_names[contain];
}

void overflow_case(std::size_t display, std::size_t contain) {
    LayoutTree tree;
    Style root_style;
    root_style.display = displays[display];
    root_style.box.size = {Length::length(120), Length::length(60)};
    const auto root = tree.create_node(root_style);
    Style child_style;
    child_style.display = Display::Block;
    child_style.position = Position::Relative;
    child_style.box.size = {Length::length(40), Length::length(20)};
    child_style.contain = contains[contain];
    const auto child = tree.create_node(child_style);
    Style descendant_style;
    descendant_style.display = Display::Block;
    descendant_style.position = Position::Absolute;
    descendant_style.box.size = {Length::length(100), Length::length(80)};
    const auto descendant = tree.create_node(descendant_style);
    tree.append_child(root, child);
    tree.append_child(child, descendant);
    reference::write_case(std::cout, name("overflow", display, contain),
        tree.compute(root, {AvailableSpace::definite(120),
            AvailableSpace::definite(60)}),
        {{1, root}, {2, child}, {3, descendant}});
}

void baseline_case(std::size_t display, std::size_t contain) {
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Flex;
    root_style.box.size = {Length::length(100), Length::length(50)};
    root_style.align_items = ItemAlignment{ItemPosition::Baseline};
    const auto root = tree.create_node(root_style);
    Style child_style;
    child_style.display = displays[display];
    child_style.box.size = {Length::length(30), Length::length(20)};
    child_style.contain = contains[contain];
    const auto child = tree.create_node(child_style);
    Style nested_style;
    nested_style.box.size = {Length::length(10), Length::length(10)};
    const auto nested = tree.create_node(nested_style,
        [](const MeasureRequest&) {
            return Measurement{{10, 10}, 3.0F, {}};
        });
    Style sibling_style;
    sibling_style.box.size = {Length::length(20), Length::length(20)};
    const auto sibling = tree.create_node(sibling_style,
        [](const MeasureRequest&) {
            return Measurement{{20, 20}, 15.0F, {}};
        });
    tree.append_child(root, child);
    tree.append_child(child, nested);
    tree.append_child(root, sibling);
    reference::write_case(std::cout, name("baseline", display, contain),
        tree.compute(root, {AvailableSpace::definite(100),
            AvailableSpace::definite(50)}),
        {{1, root}, {2, child}, {3, nested}, {4, sibling}});
}

void float_case(std::size_t contain) {
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Block;
    root_style.box.size.width = Length::length(100);
    const auto root = tree.create_node(root_style);
    Style child_style;
    child_style.display = Display::Block;
    child_style.contain = contains[contain];
    const auto child = tree.create_node(child_style);
    Style floated_style;
    floated_style.display = Display::Block;
    floated_style.float_side = FloatSide::Left;
    floated_style.box.size = {Length::length(20), Length::length(30)};
    const auto floated = tree.create_node(floated_style);
    tree.append_child(root, child);
    tree.append_child(child, floated);
    reference::write_case(std::cout, name("float", 0, contain),
        tree.compute(root, {AvailableSpace::definite(100),
            AvailableSpace::max_content()}),
        {{1, root}, {2, child}, {3, floated}});
}
} // namespace

int main() {
    try {
        std::cout << "{\"protocol\":\"AXISLY_CONTAIN_MATRIX_V1\",\"cases\":[";
        bool first = true;
        for (std::size_t display = 0; display < displays.size(); ++display) {
            for (std::size_t contain = 0; contain < contains.size(); ++contain) {
                if (!first) std::cout << ',';
                first = false;
                overflow_case(display, contain);
                std::cout << ',';
                baseline_case(display, contain);
            }
        }
        for (std::size_t contain = 0; contain < contains.size(); ++contain) {
            std::cout << ',';
            float_case(contain);
        }
        std::cout << "]}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
