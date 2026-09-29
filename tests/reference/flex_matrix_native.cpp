#include "css_reference_output.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
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
constexpr std::array item_positions{
    ItemPosition::Start, ItemPosition::End, ItemPosition::Center,
};
constexpr std::array content_positions{
    ContentPosition::FlexStart, ContentPosition::FlexEnd,
    ContentPosition::Center,
};
constexpr std::array line_positions{
    ContentPosition::Start, ContentPosition::End, ContentPosition::Center,
};
constexpr std::array item_sizes{
    Size{22, 17}, Size{29, 23}, Size{18, 31}, Size{27, 14},
};

std::string case_name(std::size_t scenario,
                      std::size_t direction, std::size_t wrap,
                      std::size_t item, std::size_t content,
                      std::size_t lines, std::size_t rtl,
                      std::size_t line_count) {
    const auto prefix = scenario == 0 ? "flex-matrix-"
        : scenario == 1 ? "flex-intrinsic-max-"
        : scenario == 2 ? "flex-intrinsic-min-" : "flex-auto-definite-";
    return prefix + std::to_string(direction) + '-'
        + std::to_string(wrap) + '-' + std::to_string(item) + '-'
        + std::to_string(content) + '-' + std::to_string(lines) + '-'
        + std::to_string(rtl) + '-' + std::to_string(line_count);
}

void run_case(std::size_t scenario, std::size_t direction, std::size_t wrap,
              std::size_t item, std::size_t content,
              std::size_t lines, std::size_t rtl,
              std::size_t line_count) {
    LayoutTree tree;
    Style root_style;
    root_style.flex_direction = directions[direction];
    root_style.flex_wrap = wraps[wrap];
    if (wrap >= 3) root_style.flex_line_count = static_cast<std::uint16_t>(line_count);
    root_style.align_items = {item_positions[item]};
    root_style.justify_content = {content_positions[content]};
    root_style.align_content = {line_positions[lines]};
    root_style.box.direction = rtl ? Direction::Rtl : Direction::Ltr;
    if (scenario == 0) {
        root_style.box.size = {Length::length(72), Length::length(68)};
    }
    root_style.box.padding = {Length::length(3), Length::length(3),
        Length::length(3), Length::length(3)};
    root_style.gap = {Length::length(4), Length::length(6)};
    const auto root = tree.create_node(root_style);
    std::vector<reference::IdentifiedNode> nodes{{1, root}};
    for (std::size_t index = 0; index < item_sizes.size(); ++index) {
        Style child_style;
        child_style.box.size = {Length::length(item_sizes[index].width),
            Length::length(item_sizes[index].height)};
        child_style.flex_shrink = 0;
        const auto child = tree.create_node(child_style);
        tree.append_child(root, child);
        nodes.emplace_back(static_cast<int>(index) + 2, child);
    }
    reference::write_case(std::cout,
        case_name(scenario, direction, wrap, item, content, lines, rtl, line_count),
        tree.compute(root, scenario == 0 || scenario == 3
            ? AvailableSize{AvailableSpace::definite(72), AvailableSpace::definite(68)}
            : scenario == 1
                ? AvailableSize{AvailableSpace::max_content(), AvailableSpace::max_content()}
                : AvailableSize{AvailableSpace::min_content(), AvailableSpace::min_content()}),
        nodes);
}
} // namespace

int main(int argc, char** argv) {
    try {
        const std::string_view mode = argc == 2 ? argv[1] : "";
        if (argc > 2 || (argc == 2 && mode != "--max-content"
            && mode != "--min-content" && mode != "--auto-definite")) {
            throw std::invalid_argument("Expected --max-content, --min-content, --auto-definite or no argument");
        }
        const std::size_t scenario = mode == "--max-content" ? 1
            : mode == "--min-content" ? 2 : mode == "--auto-definite" ? 3 : 0;
        std::cout << "{\"protocol\":\""
            << (scenario == 1 || scenario == 2
                ? "AXISLY_FLEX_INTRINSIC_MATRIX_V1" : "AXISLY_FLEX_MATRIX_V1")
            << "\",\"cases\":[";
        bool first = true;
        for (std::size_t direction = 0; direction < directions.size(); ++direction)
        for (std::size_t wrap = 0; wrap < wraps.size(); ++wrap)
        for (std::size_t item = 0; item < item_positions.size(); ++item)
        for (std::size_t content = 0; content < content_positions.size(); ++content)
        for (std::size_t lines = 0; lines < line_positions.size(); ++lines)
        for (std::size_t rtl = 0; rtl < 2; ++rtl)
        for (std::size_t line_count = 1; line_count <= 3; ++line_count) {
            if (wrap < 3 && line_count != 1) continue;
            if (!first) std::cout << ',';
            first = false;
            run_case(scenario, direction, wrap, item, content, lines, rtl,
                line_count);
        }
        std::cout << "]}\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
