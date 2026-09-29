#include "css_reference_output.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
using namespace axisly::css;

constexpr std::array displays{Display::Block, Display::Flex, Display::Grid};
constexpr std::array display_names{"Block", "Flex", "Grid"};
constexpr std::array overflows{
    Overflow::Visible, Overflow::Clip, Overflow::Hidden, Overflow::Scroll,
};
constexpr std::array overflow_names{"Visible", "Clip", "Hidden", "Scroll"};
constexpr std::array modes{
    std::pair{0U, 0U}, std::pair{3U, 0U}, std::pair{0U, 3U},
    std::pair{3U, 3U}, std::pair{2U, 3U}, std::pair{1U, 3U},
};

void write_case(std::size_t display, std::size_t direction,
                std::size_t mode, std::size_t sizing, bool absolute) {
    LayoutTree tree;
    Style root_style;
    root_style.display = displays[display];
    root_style.position = Position::Relative;
    root_style.box.direction = direction ? Direction::Rtl : Direction::Ltr;
    root_style.box.box_sizing = sizing ? BoxSizing::ContentBox : BoxSizing::BorderBox;
    root_style.box.size = {Length::length(100), Length::length(50)};
    root_style.box.padding = {Length::length(5), Length::length(5),
        Length::length(5), Length::length(5)};
    root_style.overflow_x = overflows[modes[mode].first];
    root_style.overflow_y = overflows[modes[mode].second];
    root_style.scrollbar_width = 10;
    const auto root = tree.create_node(root_style);

    Style child_style;
    child_style.display = Display::Block;
    child_style.position = absolute ? Position::Absolute : Position::Relative;
    child_style.box.size = absolute
        ? Dimensions{Length::length(20), Length::length(10)}
        : Dimensions{Length::percent(1), Length::percent(1)};
    const auto child = tree.create_node(child_style);
    tree.append_child(root, child);

    const std::string name = std::string{"scrollbar-matrix-"}
        + display_names[display] + '-' + (direction ? "Rtl" : "Ltr")
        + '-' + overflow_names[modes[mode].first]
        + '-' + overflow_names[modes[mode].second]
        + '-' + (sizing ? "ContentBox" : "BorderBox")
        + (absolute ? "-absolute" : "");
    reference::write_case(std::cout, name,
        tree.compute(root, {AvailableSpace::definite(100),
            AvailableSpace::definite(50)}),
        {{1, root}, {2, child}});
}
} // namespace

int main() {
    try {
        std::cout << "{\"protocol\":\"AXISLY_SCROLLBAR_MATRIX_V1\",\"cases\":[";
        bool first = true;
        for (std::size_t display = 0; display < displays.size(); ++display) {
            for (std::size_t direction = 0; direction < 2; ++direction) {
                for (std::size_t mode = 0; mode < modes.size(); ++mode) {
                    for (std::size_t sizing = 0; sizing < 2; ++sizing) {
                        if (!first) std::cout << ',';
                        first = false;
                        write_case(display, direction, mode, sizing, false);
                        std::cout << ',';
                        write_case(display, direction, mode, sizing, true);
                    }
                }
            }
        }
        std::cout << "]}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
