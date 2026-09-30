#include "flex_internal.hpp"
#include "sizing_keywords.hpp"

#include <algorithm>

namespace axisly::css::detail {
AvailableSpace divided_flex_cross_space(const LayoutNode& node,
    AvailableSpace available, std::optional<Unit> percentage_basis) {
    if (node.style.flex_wrap == FlexWrap::NoWrap || node.style.flex_line_count <= 1
        || available.kind != AvailableKind::Definite) return available;
    const auto& gap_style = flex_is_row(node.style.flex_direction)
        ? node.style.gap.height : node.style.gap.width;
    const Unit gap = gap_style.resolve_numeric(percentage_basis.value_or(Unit{0})).value_or(Unit{0});
    const auto count = node.style.flex_line_count;
    return AvailableSpace::definite(std::max(Unit{0}, checked(
        (static_cast<double>(available.value) - (count - 1.0) * gap) / count, node.id)));
}

AvailableSpace cross_space_for_item(AvailableSpace available,
                                   std::optional<Unit> minimum,
                                   std::optional<Unit> maximum,
                                   Unit outer_margin, CrossSpaceStage stage,
                                   NodeId id) {
    if (minimum) *minimum = checked(static_cast<double>(*minimum) + outer_margin, id);
    if (maximum) *maximum = checked(static_cast<double>(*maximum) + outer_margin, id);
    if (available.kind == AvailableKind::Definite) {
        return AvailableSpace::definite(constrain_axis(available.value, minimum, maximum));
    }
    if (stage == CrossSpaceStage::Hypothetical) return available;
    if (available.kind == AvailableKind::MinContent && minimum) {
        return AvailableSpace::definite(*minimum);
    }
    if (available.kind == AvailableKind::MaxContent && maximum) {
        return AvailableSpace::definite(*maximum);
    }
    return available;
}


FlexItem Engine::prepare_flex_item(const LayoutNode& node, const LayoutNode& child,
    const Constraints& constraints, const FlexItemSizingInput& input, std::size_t depth) {
    const auto id = child.id;
    const auto source = input.source_index;
    const bool row = flex_is_row(node.style.flex_direction);
    const auto inner_width = input.inner.width;
    const auto inner_height = input.inner.height;
    const auto main_available = row ? inner_width : inner_height;
    const auto cross_available = row ? inner_height : inner_width;
    const auto main_query = input.main_query;
    const bool main_size_is_definite = input.main_size_is_definite;
    const bool main_reverse = input.main_reverse;
    const bool cross_reverse = input.cross_reverse;
    ResolvedBox child_box;
    try {
        child_box = resolve_layout_box(child, inner_width, inner_height);
        if (!main_size_is_definite) {
            // Percentage dimensions use a definite main axis; percentage
            // edges continue to use the actual containing inline width.
            auto sizing_style = child.style.box;
            const auto padding = edges(child_box.padding, id);
            const auto border = edges(child_box.border, id);
            const auto numeric_edges = [](Insets value) {
                return EdgeValues{Length::length(value.left), Length::length(value.top),
                    Length::length(value.right), Length::length(value.bottom)};
            };
            sizing_style.padding = numeric_edges(padding);
            sizing_style.border = numeric_edges(border);
            const auto dimensions = resolve_box(sizing_style,
                row ? std::nullopt : inner_width, row ? inner_height : std::nullopt,
                IndefiniteEdgeBasis::Zero);
            child_box.preferred = dimensions.preferred;
            child_box.specified_minimum = dimensions.specified_minimum;
            child_box.specified_maximum = dimensions.specified_maximum;
        }
    }
    catch (const std::invalid_argument& error) {
        throw LayoutFailure(LayoutErrorCode::InvalidStyle, id, error.what());
    }
    // Flex resolves raw min/max styles to border-box values first. Its
    // hypothetical-size ratio transfer then uses those border-box axes,
    // while preferred style sizes transfer before content-box decoration.
    auto item_ratio_box = child_box;
    item_ratio_box.box_sizing = BoxSizing::BorderBox;
    child_box.minimum = transfer_aspect_ratio(
        child_box.specified_minimum, item_ratio_box);
    child_box.maximum = transfer_aspect_ratio(
        child_box.specified_maximum, item_ratio_box);
    const auto cross_alignment = child.style.align_self.value_or(
        node.style.align_items.value_or(ItemAlignment{}));
    const auto cross_preferred = flex_axis(child_box.preferred, row, false);
    const auto main_preferred = flex_axis(child_box.preferred, row, true);
    const auto& cross_size_style = row
        ? child.style.box.size.height : child.style.box.size.width;
    const bool cross_auto_margin = (row
        ? child_box.margin.top.kind == EdgeKind::Auto
            || child_box.margin.bottom.kind == EdgeKind::Auto
        : child_box.margin.left.kind == EdgeKind::Auto
            || child_box.margin.right.kind == EdgeKind::Auto);
    const bool content_based_cross_width = !row
        && (child.style.display == Display::Block
            || child.style.display == Display::FlowRoot)
        && cross_size_style.kind() == LengthKind::Auto
        && !cross_preferred
        && (cross_alignment.position != ItemPosition::Stretch
            || cross_auto_margin);
    const auto main_decoration = row
        ? child_box.horizontal_padding_border() : child_box.vertical_padding_border();
    if (!main_decoration) {
        throw LayoutFailure(LayoutErrorCode::UnresolvedPercentage, id,
            "Flex item decoration requires a definite containing inline size");
    }
    const auto margin = edges(child_box.margin, id);
    const Unit cross_outer_margin = checked(static_cast<double>(
        flex_axis(margin, row, false, true))
        + flex_axis(margin, row, false, false), id);
    Constraints child_constraints;
    child_constraints.sizing_mode = SizingMode::Content;
    child_constraints.content_based_width = content_based_cross_width;
    // Main-axis percentages must not contribute to an intrinsic main size.
    // Item styles were resolved above; content queries retain only the cross
    // containing block and available-space constraint.
    child_constraints.containing_width = row ? std::nullopt : inner_width;
    child_constraints.containing_height = row ? inner_height : std::nullopt;
    child_constraints.available.width = inner_width
        ? AvailableSpace::definite(*inner_width) : constraints.available.width;
    child_constraints.available.height = inner_height
        ? AvailableSpace::definite(*inner_height) : constraints.available.height;
    const auto base_space = main_query == AvailableKind::MinContent
        ? AvailableSpace::min_content() : AvailableSpace::max_content();
    if (row) child_constraints.available.width = base_space;
    else child_constraints.available.height = base_space;
    auto& child_cross_space = row ? child_constraints.available.height
        : child_constraints.available.width;
    child_cross_space = divided_flex_cross_space(node, child_cross_space, cross_available);
    child_cross_space = cross_space_for_item(child_cross_space,
        flex_axis(child_box.minimum, row, false),
        flex_axis(child_box.maximum, row, false), cross_outer_margin,
        CrossSpaceStage::FlexBase, id);
    if (const auto keyword = sizing_keyword(cross_size_style,
            cross_available ? std::optional<Unit>{*cross_available - cross_outer_margin} : std::nullopt,
            cross_available); keyword && std::holds_alternative<AvailableSpace>(*keyword)) {
        child_cross_space = std::get<AvailableSpace>(*keyword);
    }
    auto known_cross = cross_preferred;
    if (known_cross) {
        *known_cross = constrain_axis(*known_cross,
            flex_axis(child_box.minimum, row, false),
            flex_axis(child_box.maximum, row, false));
    } else if (cross_alignment.position == ItemPosition::Stretch && !cross_auto_margin) {
        if (const auto available = child_cross_space.definite_value()) {
            known_cross = std::max(Unit{0}, *available - cross_outer_margin);
        }
    }
    if (row) child_constraints.known_height = known_cross;
    else child_constraints.known_width = known_cross;
    const auto base_layout = layout_size(id, child_constraints, depth + 1);
    Unit basis = flex_axis(base_layout.size, row, true);
    const auto flex_basis = child.style.flex_basis.kind();
    bool basis_is_definite = false;
    const auto main_percentage_basis = main_size_is_definite ? main_available : std::nullopt;
    const Unit main_margin = checked(static_cast<double>(flex_axis(margin, row, true, true))
        + flex_axis(margin, row, true, false), id);
    const auto stretch = main_percentage_basis
        ? std::optional<Unit>{*main_percentage_basis - main_margin} : std::nullopt;
    const auto measure_main = [&](AvailableSpace main_space) {
        auto query = child_constraints;
        if (row) query.available.width = main_space;
        else query.available.height = main_space;
        return flex_axis(layout_size(id, query, depth + 1).size, row, true);
    };
    const auto use_keyword = [&](const Length& style) {
        const auto keyword = sizing_keyword(style, stretch, main_percentage_basis);
        if (!keyword) return false;
        if (const auto exact = std::get_if<Unit>(&*keyword)) {
            basis = *exact;
            basis_is_definite = true;
        } else {
            basis = measure_main(std::get<AvailableSpace>(*keyword));
        }
        return true;
    };
    if (flex_basis == LengthKind::Length || flex_basis == LengthKind::Percent
        || flex_basis == LengthKind::Calc) {
        const auto resolved = child.style.flex_basis.resolve_numeric(main_percentage_basis);
        if (resolved) {
            basis_is_definite = true;
            basis = child.style.box.box_sizing == BoxSizing::ContentBox
                ? checked(static_cast<double>(std::max(*resolved, Unit{0})) + *main_decoration, id)
                : std::max(*resolved, *main_decoration);
        } else if (main_preferred) {
            basis = *main_preferred;
            basis_is_definite = true;
        } else use_keyword(row ? child.style.box.size.width : child.style.box.size.height);
    } else if (flex_basis != LengthKind::Auto) {
        use_keyword(child.style.flex_basis);
    } else if (main_preferred) {
        basis = *main_preferred;
        basis_is_definite = true;
    } else {
        use_keyword(row ? child.style.box.size.width : child.style.box.size.height);
    }
    if (child_box.aspect_ratio && cross_available
        && !main_preferred && !cross_preferred
        && cross_size_style.kind() == LengthKind::Auto
        && (flex_basis == LengthKind::Auto || flex_basis == LengthKind::Content)
        && node.style.flex_wrap == FlexWrap::NoWrap
        && cross_alignment.position == ItemPosition::Stretch
        && !cross_auto_margin) {
        const Unit cross_margin = checked(static_cast<double>(
            flex_axis(margin, row, false, true))
            + flex_axis(margin, row, false, false), id);
        const auto transferred_max = transfer_aspect_ratio(
            child_box.maximum, child_box);
        const auto cross_limit = [&](std::optional<Unit> limit) -> std::optional<Unit> {
            return limit ? std::optional<Unit>(checked(
                static_cast<double>(*limit) + cross_margin, id)) : std::nullopt;
        };
        const Unit constrained_cross_space = constrain_axis(*cross_available,
            cross_limit(flex_axis(child_box.minimum, row, false)),
            cross_limit(flex_axis(transferred_max, row, false)));
        const Unit stretched_cross = std::max(Unit{0}, checked(
            static_cast<double>(constrained_cross_space) - cross_margin, id));
        const auto transferred = transfer_aspect_ratio(row
            ? ResolvedDimensions{std::nullopt, stretched_cross}
            : ResolvedDimensions{stretched_cross, std::nullopt}, item_ratio_box);
        basis = flex_axis(transferred, row, true).value_or(basis);
        basis_is_definite = true;
    }
    auto minimum = flex_axis(child_box.specified_minimum, row, true);
    if (!minimum) {
        const auto overflow = row ? child.style.overflow_x : child.style.overflow_y;
        if (overflow == Overflow::Hidden || overflow == Overflow::Scroll) {
            minimum = Unit{0};
        } else {
            auto min_constraints = child_constraints;
            min_constraints.ignore_min_max_width = false;
            min_constraints.ignore_min_max_height = false;
            if (row) {
                min_constraints.available.width = AvailableSpace::min_content();
                min_constraints.ignore_preferred_width = true;
            } else {
                min_constraints.available.height = AvailableSpace::min_content();
                min_constraints.ignore_preferred_height = true;
            }
            const auto intrinsic = layout_size(id, min_constraints, depth + 1);
            minimum = flex_axis(intrinsic.size, row, true);
            if (const auto suggested = flex_axis(child_box.preferred, row, true)) {
                minimum = std::min(*minimum, *suggested);
            }
            if (const auto transferred_max = flex_axis(child_box.maximum, row, true)) {
                minimum = std::min(*minimum, *transferred_max);
            }
            minimum = std::max(*minimum, *main_decoration);
        }
    }
    const auto maximum = flex_axis(child_box.specified_maximum, row, true);
    const auto& margin_start = row ? child_box.margin.left : child_box.margin.top;
    const auto& margin_end = row ? child_box.margin.right : child_box.margin.bottom;
    const auto& cross_start = row ? child_box.margin.top : child_box.margin.left;
    const auto& cross_end = row ? child_box.margin.bottom : child_box.margin.right;
    const bool scroll_container = child.style.overflow_x == Overflow::Hidden
        || child.style.overflow_x == Overflow::Scroll
        || child.style.overflow_y == Overflow::Hidden
        || child.style.overflow_y == Overflow::Scroll;
    basis = std::max(basis, *main_decoration);
    FlexItem item{id, source, child_box, margin};
    item.base = basis;
    item.inner_base = checked(static_cast<double>(basis) - *main_decoration, id);
    item.main_decoration = *main_decoration;
    item.content_main = flex_axis(base_layout.size, row, true);
    if (child_box.aspect_ratio && known_cross) {
        const Unit transferred = row ? *known_cross * *child_box.aspect_ratio
            : *known_cross / *child_box.aspect_ratio;
        item.content_main = std::max(item.content_main, transferred);
    }
    item.target = basis;
    item.minimum = minimum.value_or(Unit{0});
    item.maximum = maximum;
    item.hypothetical_minimum = std::max(item.minimum,
        flex_axis(child_box.minimum, row, true).value_or(Unit{0}));
    item.hypothetical_maximum = flex_axis(transfer_aspect_ratio(
        child_box.maximum, child_box), row, true);
    item.grow = child.style.flex_grow;
    item.shrink = child.style.flex_shrink;
    item.cross = flex_axis(base_layout.size, row, false);
    item.auto_main_start = (main_reverse ? margin_end : margin_start).kind == EdgeKind::Auto;
    item.auto_main_end = (main_reverse ? margin_start : margin_end).kind == EdgeKind::Auto;
    item.auto_cross_start = (cross_reverse ? cross_end : cross_start).kind == EdgeKind::Auto;
    item.auto_cross_end = (cross_reverse ? cross_start : cross_end).kind == EdgeKind::Auto;
    item.content_based_cross_width = content_based_cross_width;
    item.main_size_is_definite = main_size_is_definite || basis_is_definite;
    const bool cross_available_is_definite = cross_available.has_value()
        || (row ? constraints.available.height : constraints.available.width).kind
            == AvailableKind::Definite;
    const bool stretched_cross = !cross_auto_margin
        && (cross_size_style.kind() == LengthKind::Stretch
            || (cross_alignment.position == ItemPosition::Stretch
                && cross_size_style.kind() == LengthKind::Auto));
    item.cross_size_is_definite = cross_preferred.has_value() || stretched_cross
        || (!row && cross_available_is_definite);
    item.scroll_container = scroll_container;
    return item;
}
} // namespace axisly::css::detail
