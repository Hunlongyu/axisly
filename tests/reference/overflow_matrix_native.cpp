#include "css_reference_output.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace axisly::css;

constexpr std::array displays{Display::Block, Display::Flex, Display::Grid};
constexpr std::array display_names{"Block", "Flex", "Grid"};
constexpr std::array overflows{
    Overflow::Visible, Overflow::Clip, Overflow::Hidden, Overflow::Scroll,
};
constexpr std::array overflow_names{"Visible", "Clip", "Hidden", "Scroll"};

void write_case(std::size_t display, std::size_t overflow_x,
                std::size_t overflow_y) {
    LayoutTree tree;
    Style root_style;
    root_style.display = displays[display];
    root_style.box.size = {Length::length(120), Length::length(60)};
    const auto root = tree.create_node(root_style);

    Style child_style;
    child_style.display = Display::Block;
    child_style.position = Position::Relative;
    child_style.box.size = {Length::length(40), Length::length(20)};
    child_style.overflow_x = overflows[overflow_x];
    child_style.overflow_y = overflows[overflow_y];
    const auto child = tree.create_node(child_style);

    Style descendant_style;
    descendant_style.display = Display::Block;
    descendant_style.position = Position::Absolute;
    descendant_style.box.size = {Length::length(100), Length::length(80)};
    const auto descendant = tree.create_node(descendant_style);
    tree.append_child(root, child);
    tree.append_child(child, descendant);

    const std::string name = std::string{"overflow-matrix-"}
        + display_names[display] + '-' + overflow_names[overflow_x]
        + '-' + overflow_names[overflow_y];
    reference::write_case(std::cout, name,
        tree.compute(root, {AvailableSpace::definite(120),
            AvailableSpace::definite(60)}),
        {{1, root}, {2, child}, {3, descendant}});
}

void write_automatic_minimum(std::size_t display, std::size_t overflow_x) {
    LayoutTree tree;
    Style root_style;
    root_style.display = displays[display];
    root_style.box.size = {Length::length(40), Length::length(20)};
    if (displays[display] == Display::Grid) {
        root_style.grid.template_columns = {TrackSizing::fraction(1)};
        root_style.grid.template_rows = {TrackSizing::fixed(20)};
    }
    const auto root = tree.create_node(root_style);

    Style child_style;
    child_style.display = Display::Block;
    child_style.overflow_x = overflows[overflow_x];
    const auto child = tree.create_node(child_style);

    const auto descendant = tree.create_node(Style{},
        [](const MeasureRequest&) {
            return Measurement{{80, 10}, {}, {}};
        });
    tree.append_child(root, child);
    tree.append_child(child, descendant);

    const std::string name = std::string{"overflow-auto-min-"}
        + display_names[display] + '-' + overflow_names[overflow_x];
    reference::write_case(std::cout, name,
        tree.compute(root, {AvailableSpace::definite(40),
            AvailableSpace::definite(20)}),
        {{1, root}, {2, child}, {3, descendant}});
}
} // namespace

int main() {
    try {
        std::cout << "{\"protocol\":\"AXISLY_OVERFLOW_MATRIX_V1\",\"cases\":[";
        bool first = true;
        for (std::size_t display = 0; display < displays.size(); ++display) {
            for (std::size_t x = 0; x < overflows.size(); ++x) {
                for (std::size_t y = 0; y < overflows.size(); ++y) {
                    if (!first) std::cout << ',';
                    first = false;
                    write_case(display, x, y);
                }
            }
        }
        for (std::size_t display = 1; display < displays.size(); ++display) {
            for (std::size_t x = 0; x < overflows.size(); ++x) {
                std::cout << ',';
                write_automatic_minimum(display, x);
            }
        }
        std::cout << "]}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
