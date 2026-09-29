#include "css_reference_output.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace axisly;
using namespace axisly::css;

constexpr std::array displays{Display::Block, Display::Flex, Display::Grid};

Length dimension_style(std::size_t kind) {
    switch (kind) {
    case 0: return Length::min_content();
    case 1: return Length::max_content();
    case 2: return Length::fit_content();
    case 3: return Length::fit_content_length(15);
    case 4: return Length::fit_content_length(55);
    case 5: return Length::fit_content_length(95);
    case 6: return Length::fit_content_percent(0.5F);
    default: throw std::invalid_argument("Unknown intrinsic dimension");
    }
}

MeasureFunction measured(Unit minimum, Unit maximum, std::size_t axis) {
    return [=](const MeasureRequest& request) {
        const Unit width = axis == 0
            ? request.available_width.kind == AvailableKind::MinContent ? minimum : maximum
            : Unit{12};
        const Unit height = axis == 1
            ? request.available_height.kind == AvailableKind::MinContent ? minimum : maximum
            : Unit{12};
        return Measurement{{request.known_content_width.value_or(width),
            request.known_content_height.value_or(height)}, {}, {},
            axis == 0 ? Size{maximum, 12} : Size{12, maximum}};
    };
}

void run_case(std::size_t axis, std::size_t display, std::size_t dimension,
              std::size_t query, std::size_t direction, bool details) {
    LayoutTree tree;
    Style root_style;
    root_style.display = displays[display];
    root_style.box.size = axis == 0
        ? Dimensions{dimension_style(dimension), Length::length(60)}
        : Dimensions{Length::length(60), dimension_style(dimension)};
    if (axis == 1) root_style.flex_direction = FlexDirection::Column;
    root_style.box.direction = direction == 0 ? Direction::Ltr : Direction::Rtl;
    if (axis == 0) {
        root_style.box.padding.left = Length::length(4);
        root_style.box.padding.right = Length::length(4);
        root_style.gap.width = Length::length(4);
    } else {
        root_style.box.padding.top = Length::length(4);
        root_style.box.padding.bottom = Length::length(4);
        root_style.gap.height = Length::length(4);
    }
    root_style.align_items = {ItemPosition::Start};
    root_style.justify_items = {ItemPosition::Start};
    if (display == 2) {
        auto& main_tracks = axis == 0 ? root_style.grid.template_columns
            : root_style.grid.template_rows;
        auto& cross_tracks = axis == 0 ? root_style.grid.template_rows
            : root_style.grid.template_columns;
        main_tracks = {TrackSizing::automatic(), TrackSizing::automatic()};
        cross_tracks = {TrackSizing::automatic()};
    }
    const auto root = tree.create_node(root_style);
    Style child_style;
    (axis == 0 ? child_style.box.size.height : child_style.box.size.width)
        = Length::length(12);
    child_style.flex_shrink = 0;
    const auto first = tree.create_node(child_style, measured(20, 60, axis));
    const auto second = tree.create_node(child_style, measured(30, 40, axis));
    tree.append_child(root, first);
    tree.append_child(root, second);
    const AvailableSpace main_available = query == 0
        ? AvailableSpace::definite(160)
        : query == 1 ? AvailableSpace::max_content()
        : AvailableSpace::min_content();
    const auto name = "intrinsic-size-" + std::to_string(axis) + '-'
        + std::to_string(display) + '-' + std::to_string(dimension) + '-'
        + std::to_string(query)
        + '-' + std::to_string(direction);
    reference::write_case(std::cout, name,
        tree.compute(root, axis == 0
            ? AvailableSize{main_available, AvailableSpace::definite(60)}
            : AvailableSize{AvailableSpace::definite(60), main_available}),
        {{1, root}, {2, first}, {3, second}}, details);
}

void run_nested_case(std::size_t axis, std::size_t parent_display,
                     std::size_t child_display, std::size_t dimension,
                     std::size_t direction, std::size_t constraint, bool details) {
    LayoutTree tree;
    Style outer_style;
    outer_style.display = displays[parent_display];
    outer_style.box.size = axis == 0
        ? Dimensions{Length::length(160), Length::length(80)}
        : Dimensions{Length::length(80), Length::length(160)};
    if (axis == 1) outer_style.flex_direction = FlexDirection::Column;
    outer_style.box.direction = direction == 0 ? Direction::Ltr : Direction::Rtl;
    outer_style.align_items = {ItemPosition::Start};
    outer_style.justify_items = {ItemPosition::Start};
    if (parent_display == 2) {
        (axis == 0 ? outer_style.grid.template_columns : outer_style.grid.template_rows)
            = {TrackSizing::fixed(120)};
        (axis == 0 ? outer_style.grid.template_rows : outer_style.grid.template_columns)
            = {TrackSizing::automatic()};
    }
    const auto outer = tree.create_node(outer_style);
    Style inner_style;
    inner_style.display = displays[child_display];
    (axis == 0 ? inner_style.box.size.width : inner_style.box.size.height)
        = dimension_style(dimension);
    (axis == 0 ? inner_style.box.size.height : inner_style.box.size.width)
        = Length::length(60);
    if (constraint == 1) {
        (axis == 0 ? inner_style.box.min_size.width : inner_style.box.min_size.height)
            = Length::length(80);
    } else if (constraint == 2) {
        (axis == 0 ? inner_style.box.max_size.width : inner_style.box.max_size.height)
            = Length::length(70);
    }
    if (axis == 1) inner_style.flex_direction = FlexDirection::Column;
    if (axis == 0) {
        inner_style.box.padding.left = Length::length(4);
        inner_style.box.padding.right = Length::length(4);
        inner_style.gap.width = Length::length(4);
    } else {
        inner_style.box.padding.top = Length::length(4);
        inner_style.box.padding.bottom = Length::length(4);
        inner_style.gap.height = Length::length(4);
    }
    inner_style.flex_shrink = 0;
    inner_style.align_items = {ItemPosition::Start};
    inner_style.justify_items = {ItemPosition::Start};
    if (child_display == 2) {
        (axis == 0 ? inner_style.grid.template_columns : inner_style.grid.template_rows)
            = {TrackSizing::automatic(), TrackSizing::automatic()};
        (axis == 0 ? inner_style.grid.template_rows : inner_style.grid.template_columns)
            = {TrackSizing::automatic()};
    }
    const auto inner = tree.create_node(inner_style);
    tree.append_child(outer, inner);
    Style leaf_style;
    (axis == 0 ? leaf_style.box.size.height : leaf_style.box.size.width)
        = Length::length(12);
    leaf_style.flex_shrink = 0;
    const auto first = tree.create_node(leaf_style, measured(20, 60, axis));
    const auto second = tree.create_node(leaf_style, measured(30, 40, axis));
    tree.append_child(inner, first);
    tree.append_child(inner, second);
    const auto name = "intrinsic-nested-" + std::to_string(axis) + '-'
        + std::to_string(parent_display) + '-' + std::to_string(child_display)
        + '-' + std::to_string(dimension)
        + '-' + std::to_string(direction) + '-' + std::to_string(constraint);
    reference::write_case(std::cout, name,
        tree.compute(outer, axis == 0
            ? AvailableSize{AvailableSpace::definite(160), AvailableSpace::definite(80)}
            : AvailableSize{AvailableSpace::definite(80), AvailableSpace::definite(160)}),
        {{1, outer}, {2, inner}, {3, first}, {4, second}}, details);
}

void run_deep_case(std::size_t axis, std::size_t outer_display,
                   std::size_t middle_display, std::size_t inner_display,
                   std::size_t middle_dimension, std::size_t inner_dimension,
                   std::size_t direction, bool details) {
    LayoutTree tree;
    const auto main_size = [&](Unit main, Unit cross) {
        return axis == 0
            ? Dimensions{Length::length(main), Length::length(cross)}
            : Dimensions{Length::length(cross), Length::length(main)};
    };
    const auto set_intrinsic = [&](Style& style, std::size_t dimension, Unit cross) {
        style.box.size = axis == 0
            ? Dimensions{dimension_style(dimension), Length::length(cross)}
            : Dimensions{Length::length(cross), dimension_style(dimension)};
        style.box.direction = direction == 0 ? Direction::Ltr : Direction::Rtl;
        style.align_items = {ItemPosition::Start};
        style.justify_items = {ItemPosition::Start};
        style.flex_shrink = 0;
        if (axis == 1) style.flex_direction = FlexDirection::Column;
        if (style.display == Display::Grid) {
            (axis == 0 ? style.grid.template_columns : style.grid.template_rows)
                = {TrackSizing::automatic(), TrackSizing::automatic()};
            (axis == 0 ? style.grid.template_rows : style.grid.template_columns)
                = {TrackSizing::automatic()};
        }
    };
    Style outer_style;
    outer_style.display = displays[outer_display];
    outer_style.box.size = main_size(160, 80);
    outer_style.box.direction = direction == 0 ? Direction::Ltr : Direction::Rtl;
    outer_style.align_items = {ItemPosition::Start};
    outer_style.justify_items = {ItemPosition::Start};
    if (axis == 1) outer_style.flex_direction = FlexDirection::Column;
    if (outer_display == 2) {
        (axis == 0 ? outer_style.grid.template_columns : outer_style.grid.template_rows)
            = {TrackSizing::fixed(120)};
        (axis == 0 ? outer_style.grid.template_rows : outer_style.grid.template_columns)
            = {TrackSizing::automatic()};
    }
    const auto outer = tree.create_node(outer_style);
    Style middle_style;
    middle_style.display = displays[middle_display];
    set_intrinsic(middle_style, middle_dimension, 60);
    if (middle_display == 2) {
        (axis == 0 ? middle_style.grid.template_columns : middle_style.grid.template_rows)
            = {TrackSizing::automatic()};
    }
    const auto middle = tree.create_node(middle_style);
    tree.append_child(outer, middle);
    Style inner_style;
    inner_style.display = displays[inner_display];
    set_intrinsic(inner_style, inner_dimension, 48);
    if (axis == 0) {
        inner_style.box.padding.left = Length::length(4);
        inner_style.box.padding.right = Length::length(4);
        inner_style.gap.width = Length::length(4);
    } else {
        inner_style.box.padding.top = Length::length(4);
        inner_style.box.padding.bottom = Length::length(4);
        inner_style.gap.height = Length::length(4);
    }
    const auto inner = tree.create_node(inner_style);
    tree.append_child(middle, inner);
    Style leaf_style;
    (axis == 0 ? leaf_style.box.size.height : leaf_style.box.size.width)
        = Length::length(12);
    leaf_style.flex_shrink = 0;
    const auto first = tree.create_node(leaf_style, measured(20, 60, axis));
    const auto second = tree.create_node(leaf_style, measured(30, 40, axis));
    tree.append_child(inner, first);
    tree.append_child(inner, second);
    const auto name = "intrinsic-deep-" + std::to_string(axis) + '-'
        + std::to_string(outer_display) + '-' + std::to_string(middle_display)
        + '-' + std::to_string(inner_display) + '-'
        + std::to_string(middle_dimension) + '-'
        + std::to_string(inner_dimension) + '-' + std::to_string(direction);
    reference::write_case(std::cout, name,
        tree.compute(outer, axis == 0
            ? AvailableSize{AvailableSpace::definite(160), AvailableSpace::definite(80)}
            : AvailableSize{AvailableSpace::definite(80), AvailableSpace::definite(160)}),
        {{1, outer}, {2, middle}, {3, inner}, {4, first}, {5, second}}, details);
}

void run_biaxial_case(std::size_t parent_display, std::size_t child_display,
                      std::size_t width_dimension, std::size_t height_dimension,
                      std::size_t direction, bool details) {
    LayoutTree tree;
    Style outer_style;
    outer_style.display = displays[parent_display];
    outer_style.box.size = {Length::length(160), Length::length(120)};
    outer_style.box.direction = direction == 0 ? Direction::Ltr : Direction::Rtl;
    outer_style.align_items = {ItemPosition::Start};
    outer_style.justify_items = {ItemPosition::Start};
    if (parent_display == 2) {
        outer_style.grid.template_columns = {TrackSizing::fixed(130)};
        outer_style.grid.template_rows = {TrackSizing::automatic()};
    }
    const auto outer = tree.create_node(outer_style);
    Style inner_style;
    inner_style.display = displays[child_display];
    inner_style.box.size = {dimension_style(width_dimension),
                            dimension_style(height_dimension)};
    inner_style.box.direction = outer_style.box.direction;
    inner_style.box.padding = {Length::length(4), Length::length(4),
                               Length::length(4), Length::length(4)};
    inner_style.gap = {Length::length(4), Length::length(4)};
    inner_style.flex_shrink = 0;
    inner_style.align_items = {ItemPosition::Start};
    inner_style.justify_items = {ItemPosition::Start};
    if (child_display == 2) {
        inner_style.grid.template_columns = {
            TrackSizing::automatic(), TrackSizing::automatic()};
        inner_style.grid.template_rows = {TrackSizing::automatic()};
    }
    const auto inner = tree.create_node(inner_style);
    tree.append_child(outer, inner);
    const auto measured_both = [](Unit min_width, Unit max_width,
                                  Unit min_height, Unit max_height) {
        return [=](const MeasureRequest& request) {
            const Unit width = request.available_width.kind == AvailableKind::MinContent
                ? min_width : max_width;
            const Unit height = request.available_height.kind == AvailableKind::MinContent
                ? min_height : max_height;
            return Measurement{{request.known_content_width.value_or(width),
                request.known_content_height.value_or(height)}, {}, {},
                Size{max_width, max_height}};
        };
    };
    Style leaf_style;
    leaf_style.flex_shrink = 0;
    const auto first = tree.create_node(leaf_style, measured_both(20, 60, 15, 35));
    const auto second = tree.create_node(leaf_style, measured_both(30, 40, 25, 45));
    tree.append_child(inner, first);
    tree.append_child(inner, second);
    const auto name = "intrinsic-biaxial-" + std::to_string(parent_display)
        + '-' + std::to_string(child_display) + '-'
        + std::to_string(width_dimension) + '-'
        + std::to_string(height_dimension) + '-' + std::to_string(direction);
    reference::write_case(std::cout, name,
        tree.compute(outer, {AvailableSpace::definite(160),
                             AvailableSpace::definite(120)}),
        {{1, outer}, {2, inner}, {3, first}, {4, second}}, details);
}
} // namespace

int main(int argc, char** argv) {
    try {
        const bool details = argc == 2 && std::string(argv[1]) == "--details";
        if (argc != 1 && !details) throw std::invalid_argument("Expected --details or no argument");
        std::cout << "{\"protocol\":\"AXISLY_INTRINSIC_SIZE_V1\",\"cases\":[";
        bool first = true;
        for (std::size_t axis = 0; axis < 2; ++axis)
        for (std::size_t display = 0; display < 3; ++display)
        for (std::size_t width = 0; width < 7; ++width)
        for (std::size_t query = 0; query < 3; ++query)
        for (std::size_t direction = 0; direction < 2; ++direction) {
            if (!first) std::cout << ',';
            first = false;
            run_case(axis, display, width, query, direction, details);
        }
        for (std::size_t axis = 0; axis < 2; ++axis)
        for (std::size_t parent_display = 0; parent_display < 3; ++parent_display)
        for (std::size_t child_display = 0; child_display < 3; ++child_display)
        for (std::size_t width = 0; width < 7; ++width)
        for (std::size_t direction = 0; direction < 2; ++direction)
        for (std::size_t constraint = 0; constraint < 3; ++constraint) {
            if (!first) std::cout << ',';
            first = false;
            run_nested_case(axis, parent_display, child_display, width,
                direction, constraint, details);
        }
        for (std::size_t axis = 0; axis < 2; ++axis)
        for (std::size_t outer_display = 0; outer_display < 3; ++outer_display)
        for (std::size_t middle_display = 0; middle_display < 3; ++middle_display)
        for (std::size_t inner_display = 0; inner_display < 3; ++inner_display)
        for (const std::size_t middle_dimension : std::array<std::size_t, 3>{0, 1, 4})
        for (const std::size_t inner_dimension : std::array<std::size_t, 3>{0, 1, 4})
        for (std::size_t direction = 0; direction < 2; ++direction) {
            if (!first) std::cout << ',';
            first = false;
            run_deep_case(axis, outer_display, middle_display, inner_display,
                middle_dimension, inner_dimension, direction, details);
        }
        for (std::size_t parent_display = 0; parent_display < 3; ++parent_display)
        for (std::size_t child_display = 0; child_display < 3; ++child_display)
        for (const std::size_t width_dimension : std::array<std::size_t, 3>{0, 1, 4})
        for (const std::size_t height_dimension : std::array<std::size_t, 3>{0, 1, 4})
        for (std::size_t direction = 0; direction < 2; ++direction) {
            if (!first) std::cout << ',';
            first = false;
            run_biaxial_case(parent_display, child_display, width_dimension,
                height_dimension, direction, details);
        }
        std::cout << "]}\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
