#include "css_reference_output.hpp"

#include <array>
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
};

MeasureFunction measured(Size minimum, Size preferred) {
    return [=](const MeasureRequest& request) {
        const auto width = request.available_width.kind == AvailableKind::MinContent
            ? minimum.width : preferred.width;
        const auto height = request.available_height.kind == AvailableKind::MinContent
            ? minimum.height : preferred.height;
        return Measurement{{request.known_content_width.value_or(width),
            request.known_content_height.value_or(height)}, {}, {}, preferred};
    };
}

void run_case(std::size_t scenario, std::size_t direction, std::size_t wrap,
              std::size_t nested_direction, std::size_t variant, std::size_t rtl) {
    const bool row = direction < 2;
    LayoutTree tree;
    Style root_style;
    root_style.flex_direction = directions[direction];
    root_style.flex_wrap = wraps[wrap];
    root_style.align_items = {ItemPosition::Start};
    root_style.box.direction = rtl ? Direction::Rtl : Direction::Ltr;
    root_style.box.padding = {Length::length(3), Length::length(3),
        Length::length(3), Length::length(3)};
    root_style.gap = {Length::length(4), Length::length(6)};
    if (scenario == 0) {
        root_style.box.size = {Length::length(row ? 90.0F : 80.0F),
            Length::length(row ? 80.0F : 90.0F)};
    }
    const auto root = tree.create_node(root_style);
    Style nested_style;
    nested_style.flex_direction = nested_direction == 0
        ? FlexDirection::Row : FlexDirection::Column;
    nested_style.align_items = {ItemPosition::Start};
    nested_style.box.padding = {Length::length(2), Length::length(2),
        Length::length(2), Length::length(2)};
    nested_style.gap = {Length::length(3), Length::length(3)};
    nested_style.flex_shrink = 0;
    if (variant == 1) {
        nested_style.flex_grow = 1;
        nested_style.flex_basis = Length::length(20);
    }
    const auto nested = tree.create_node(nested_style);
    tree.append_child(root, nested);
    const auto first = tree.create_node({}, measured({10, 8}, {24, 18}));
    const auto second = tree.create_node({}, measured({16, 11}, {29, 21}));
    tree.append_child(nested, first);
    tree.append_child(nested, second);
    Style fixed_style;
    fixed_style.box.size = {Length::length(23), Length::length(19)};
    fixed_style.flex_shrink = 0;
    const auto fixed = tree.create_node(fixed_style);
    tree.append_child(root, fixed);
    const auto measured_child = tree.create_node({}, measured({12, 9}, {28, 16}));
    tree.append_child(root, measured_child);

    const auto prefix = scenario == 0 ? "flex-nested-"
        : scenario == 1 ? "flex-nested-max-" : "flex-nested-min-";
    const auto name = prefix + std::to_string(direction) + '-'
        + std::to_string(wrap) + '-' + std::to_string(nested_direction)
        + '-' + std::to_string(variant) + '-' + std::to_string(rtl);
    const auto available = scenario == 0
        ? AvailableSize{AvailableSpace::definite(row ? 90.0F : 80.0F),
            AvailableSpace::definite(row ? 80.0F : 90.0F)}
        : scenario == 1
            ? AvailableSize{AvailableSpace::max_content(), AvailableSpace::max_content()}
            : AvailableSize{AvailableSpace::min_content(), AvailableSpace::min_content()};
    reference::write_case(std::cout, name, tree.compute(root, available),
        {{1, root}, {2, nested}, {3, first}, {4, second},
         {5, fixed}, {6, measured_child}});
}
} // namespace

int main(int argc, char** argv) {
    try {
        const std::string_view mode = argc == 2 ? argv[1] : "";
        if (argc > 2 || (argc == 2 && mode != "--max-content"
            && mode != "--min-content")) {
            throw std::invalid_argument("Expected an intrinsic query or no argument");
        }
        const std::size_t scenario = mode == "--max-content" ? 1
            : mode == "--min-content" ? 2 : 0;
        std::cout << "{\"protocol\":\"AXISLY_FLEX_NESTED_V1\",\"cases\":[";
        bool first = true;
        for (std::size_t direction = 0; direction < directions.size(); ++direction)
        for (std::size_t wrap = 0; wrap < wraps.size(); ++wrap)
        for (std::size_t nested_direction = 0; nested_direction < 2; ++nested_direction)
        for (std::size_t variant = 0; variant < 2; ++variant)
        for (std::size_t rtl = 0; rtl < 2; ++rtl) {
            if (!first) std::cout << ',';
            first = false;
            run_case(scenario, direction, wrap, nested_direction, variant, rtl);
        }
        std::cout << "]}\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
