#include "layout_engine.hpp"

#include <algorithm>

namespace axisly::css::detail {
namespace {
std::optional<Unit> inset_value(const Length& value, Unit basis, NodeId node) {
    try { return value.resolve_numeric(basis); }
    catch (const std::invalid_argument& error) {
        throw LayoutFailure(LayoutErrorCode::InvalidStyle, node, error.what());
    }
}

struct AbsoluteAxis {
    Unit offset{}, start_margin{}, end_margin{};
};

enum class AbsoluteMarginMode { InsetConstrained, FlexContainer, GridArea };

struct AbsoluteAxisInput {
    std::optional<Unit> start, end;
    Unit containing_size{}, item_size{};
    Unit start_margin{}, end_margin{};
    bool auto_start{}, auto_end{};
    Unit auto_margin_basis{};
    AbsoluteMarginMode margin_mode{AbsoluteMarginMode::InsetConstrained};
    bool prefer_end{};
    NodeId node{};
};

enum class StaticPlacement { Start, Center, End };

StaticPlacement writing_start(bool horizontal, Direction direction) {
    return horizontal && direction == Direction::Rtl
        ? StaticPlacement::End : StaticPlacement::Start;
}

StaticPlacement opposite(StaticPlacement placement) {
    return placement == StaticPlacement::Start
        ? StaticPlacement::End : StaticPlacement::Start;
}

StaticPlacement content_placement(ContentAlignment alignment,
    bool horizontal, Direction direction, bool flex_reverse) {
    const auto flex_start = flex_reverse
        ? StaticPlacement::End : StaticPlacement::Start;
    switch (alignment.position) {
    case ContentPosition::Start: return writing_start(horizontal, direction);
    case ContentPosition::End: return opposite(writing_start(horizontal, direction));
    case ContentPosition::FlexEnd: return opposite(flex_start);
    case ContentPosition::Center:
    case ContentPosition::SpaceAround:
    case ContentPosition::SpaceEvenly: return StaticPlacement::Center;
    case ContentPosition::FlexStart:
    case ContentPosition::Stretch:
    case ContentPosition::SpaceBetween: return flex_start;
    }
    return flex_start;
}

StaticPlacement item_placement(ItemAlignment alignment, bool horizontal,
    Direction container_direction, Direction item_direction, bool flex_reverse,
    bool overflow) {
    if (alignment.safety == AlignmentSafety::Safe && overflow) {
        return writing_start(horizontal, container_direction);
    }
    const auto flex_start = flex_reverse
        ? StaticPlacement::End : StaticPlacement::Start;
    switch (alignment.position) {
    case ItemPosition::Start:
    case ItemPosition::Baseline:
        return writing_start(horizontal, container_direction);
    case ItemPosition::End:
        return opposite(writing_start(horizontal, container_direction));
    case ItemPosition::SelfStart:
        return writing_start(horizontal, item_direction);
    case ItemPosition::SelfEnd:
        return opposite(writing_start(horizontal, item_direction));
    case ItemPosition::FlexEnd: return opposite(flex_start);
    case ItemPosition::Center: return StaticPlacement::Center;
    case ItemPosition::FlexStart:
    case ItemPosition::Stretch: return flex_start;
    }
    return flex_start;
}

Unit static_offset(StaticPlacement placement, Unit containing_size, Unit item_size,
                   Unit start_margin, Unit end_margin, NodeId node) {
    const Unit free = checked(static_cast<double>(containing_size)
        - item_size - start_margin - end_margin, node);
    switch (placement) {
    case StaticPlacement::Start: return start_margin;
    case StaticPlacement::Center:
        return checked(static_cast<double>(start_margin) + free / 2, node);
    case StaticPlacement::End:
        return checked(static_cast<double>(start_margin) + free, node);
    }
    return start_margin;
}

AbsoluteAxis solve_absolute_axis(const AbsoluteAxisInput& input) {
    Unit start_margin = input.start_margin;
    Unit end_margin = input.end_margin;
    const Unit specified_start_margin = start_margin;
    const Unit specified_end_margin = end_margin;
    const bool distribute_auto_margins = input.margin_mode == AbsoluteMarginMode::GridArea
        || (input.start && input.end);
    if (distribute_auto_margins) {
        double free = static_cast<double>(input.auto_margin_basis)
            - input.item_size - start_margin - end_margin;
        if (input.margin_mode == AbsoluteMarginMode::InsetConstrained) {
            free -= *input.start + *input.end;
        }
        if (free > 0 && (input.auto_start || input.auto_end)) {
            const Unit share = checked(free
                / (static_cast<int>(input.auto_start) + static_cast<int>(input.auto_end)), input.node);
            if (input.auto_start) start_margin = share;
            if (input.auto_end) end_margin = share;
        }
    }
    // Grid reports the resolved auto margins, but an explicit inset uses
    // only non-auto margins when choosing the border-box position.
    const Unit position_start_margin = input.margin_mode == AbsoluteMarginMode::GridArea
        && (input.start || input.end) ? specified_start_margin : start_margin;
    const Unit position_end_margin = input.margin_mode == AbsoluteMarginMode::GridArea
        && (input.start || input.end) ? specified_end_margin : end_margin;
    const Unit offset = input.prefer_end && input.end
        ? checked(static_cast<double>(input.containing_size) - *input.end
            - input.item_size - position_end_margin, input.node)
        : input.start ? checked(static_cast<double>(*input.start) + position_start_margin, input.node)
        : input.end ? checked(static_cast<double>(input.containing_size) - *input.end
            - input.item_size - position_end_margin, input.node)
        : input.prefer_end ? checked(static_cast<double>(input.containing_size)
            - input.item_size - position_end_margin, input.node) : position_start_margin;
    return {offset, start_margin, end_margin};
}
} // namespace

std::pair<Unit, Unit> relative_offset(const LayoutNode& node,
                                      Size containing, NodeId parent) {
    if (node.style.position != Position::Relative) return {};
    const auto& inset = node.style.inset;
    const auto left = inset_value(inset.left, containing.width, node.id);
    const auto right = inset_value(inset.right, containing.width, node.id);
    const auto top = inset_value(inset.top, containing.height, node.id);
    const auto bottom = inset_value(inset.bottom, containing.height, node.id);
    const Unit x = node.style.box.direction == Direction::Rtl && right
        ? checked(-static_cast<double>(*right), parent)
        : left ? *left : right ? checked(-static_cast<double>(*right), parent) : Unit{0};
    const Unit y = top ? *top : bottom ? checked(-static_cast<double>(*bottom), parent) : Unit{0};
    return {x, y};
}

Fragment Engine::absolute(const LayoutNode& parent, NodeId child_id,
                          Size parent_size, const ResolvedBox& parent_box,
                          std::size_t depth,
                          std::optional<Rect> containing_override,
                          std::optional<Unit> static_block_y) {
    const auto& child = get(child_id);
    const auto border = edges(parent_box.border, parent.id);
    const Rect containing = containing_override.value_or(Rect{
        border.left, border.top,
        std::max(Unit{0}, checked(static_cast<double>(parent_size.width)
            - border.left - border.right, parent.id)),
        std::max(Unit{0}, checked(static_cast<double>(parent_size.height)
            - border.top - border.bottom, parent.id)),
    });
    const Unit containing_width = containing.width;
    const Unit containing_height = containing.height;
    const auto& inset = child.style.inset;
    const auto left = inset_value(inset.left, containing_width, child.id);
    const auto right = inset_value(inset.right, containing_width, child.id);
    const auto top = inset_value(inset.top, containing_height, child.id);
    const auto bottom = inset_value(inset.bottom, containing_height, child.id);
    ResolvedBox child_box;
    try { child_box = resolve_layout_box(child, containing_width, containing_height); }
    catch (const std::invalid_argument& error) {
        throw LayoutFailure(LayoutErrorCode::InvalidStyle, child.id, error.what());
    }
    const auto margin = edges(child_box.margin, child.id);
    Constraints constraints;
    constraints.containing_width = containing_width;
    constraints.containing_height = containing_height;
    constraints.available = {AvailableSpace::definite(containing_width),
        AvailableSpace::definite(containing_height)};
    constraints.content_based_width = child.style.box.size.width.kind()
        == LengthKind::Auto && !(left && right);
    if (left && right && child.style.box.size.width.kind() == LengthKind::Auto) {
        constraints.known_width = std::max(Unit{0}, checked(
            static_cast<double>(containing_width) - *left - *right
            - margin.left - margin.right, child.id));
    }
    if (top && bottom && child.style.box.size.height.kind() == LengthKind::Auto) {
        constraints.known_height = std::max(Unit{0}, checked(
            static_cast<double>(containing_height) - *top - *bottom
            - margin.top - margin.bottom, child.id));
    }
    auto fragment = layout(child_id, constraints, depth + 1);
    const auto margin_mode = parent.style.display == Display::Flex
        ? AbsoluteMarginMode::FlexContainer
        : parent.style.display == Display::Grid
            ? AbsoluteMarginMode::GridArea
            : AbsoluteMarginMode::InsetConstrained;
    const Unit horizontal_margin_basis = margin_mode == AbsoluteMarginMode::FlexContainer
        ? parent_size.width : containing_width;
    const Unit vertical_margin_basis = margin_mode == AbsoluteMarginMode::FlexContainer
        ? parent_size.height : containing_height;
    const auto horizontal = solve_absolute_axis({
        .start = left, .end = right,
        .containing_size = containing_width, .item_size = fragment.size.width,
        .start_margin = margin.left, .end_margin = margin.right,
        .auto_start = child_box.margin.left.kind == EdgeKind::Auto,
        .auto_end = child_box.margin.right.kind == EdgeKind::Auto,
        .auto_margin_basis = horizontal_margin_basis, .margin_mode = margin_mode,
        .prefer_end = parent.style.box.direction == Direction::Rtl,
        .node = child.id,
    });
    const auto vertical = solve_absolute_axis({
        .start = top, .end = bottom,
        .containing_size = containing_height, .item_size = fragment.size.height,
        .start_margin = margin.top, .end_margin = margin.bottom,
        .auto_start = child_box.margin.top.kind == EdgeKind::Auto,
        .auto_end = child_box.margin.bottom.kind == EdgeKind::Auto,
        .auto_margin_basis = vertical_margin_basis, .margin_mode = margin_mode,
        .node = child.id,
    });
    auto placed_horizontal = horizontal;
    auto placed_vertical = vertical;
    const bool static_x = !left && !right;
    const bool static_y = !top && !bottom;
    const auto padding = edges(parent_box.padding, parent.id);
    const Unit content_width = std::max(Unit{0}, checked(
        static_cast<double>(containing_width) - padding.left - padding.right
            - parent_box.scrollbar.width, parent.id));
    const Unit content_height = std::max(Unit{0}, checked(
        static_cast<double>(containing_height) - padding.top - padding.bottom
            - parent_box.scrollbar.height, parent.id));
    if (parent.style.display == Display::Flex && (static_x || static_y)) {
        const bool row = parent.style.flex_direction == FlexDirection::Row
            || parent.style.flex_direction == FlexDirection::RowReverse;
        const bool rtl = parent.style.box.direction == Direction::Rtl;
        const bool main_reverse = row
            ? (parent.style.flex_direction == FlexDirection::RowReverse) != rtl
            : parent.style.flex_direction == FlexDirection::ColumnReverse;
        const bool cross_reverse = (parent.style.flex_wrap == FlexWrap::WrapReverse
            || parent.style.flex_wrap == FlexWrap::BalanceReverse) != (!row && rtl);
        const auto main_alignment = parent.style.justify_content.value_or(
            ContentAlignment{ContentPosition::FlexStart});
        const auto cross_alignment = child.style.align_self.value_or(
            parent.style.align_items.value_or(ItemAlignment{}));
        if (static_x) {
            const auto placement = row
                ? content_placement(main_alignment, true,
                    parent.style.box.direction, main_reverse)
                : item_placement(cross_alignment, true, parent.style.box.direction,
                    child.style.box.direction, cross_reverse,
                    fragment.size.width + margin.left + margin.right > content_width);
            placed_horizontal.offset = checked(static_cast<double>(padding.left)
                + static_offset(placement, content_width, fragment.size.width,
                    margin.left, margin.right, child.id), child.id);
        }
        if (static_y) {
            const auto placement = row
                ? item_placement(cross_alignment, false, parent.style.box.direction,
                    child.style.box.direction, cross_reverse,
                    fragment.size.height + margin.top + margin.bottom > content_height)
                : content_placement(main_alignment, false,
                    parent.style.box.direction, main_reverse);
            placed_vertical.offset = checked(static_cast<double>(padding.top)
                + static_offset(placement, content_height, fragment.size.height,
                    margin.top, margin.bottom, child.id), child.id);
        }
    } else if (parent.style.display == Display::Grid && (static_x || static_y)) {
        const auto justify = child.style.justify_self.value_or(
            parent.style.justify_items.value_or(ItemAlignment{}));
        const auto align = child.style.align_self.value_or(
            parent.style.align_items.value_or(ItemAlignment{}));
        if (static_x) {
            const auto placement = item_placement(justify, true,
                parent.style.box.direction, child.style.box.direction, false,
                fragment.size.width + margin.left + margin.right > containing_width);
            placed_horizontal.offset = static_offset(placement, containing_width,
                fragment.size.width, horizontal.start_margin,
                horizontal.end_margin, child.id);
        }
        if (static_y) {
            const auto placement = item_placement(align, false,
                parent.style.box.direction, child.style.box.direction, false,
                fragment.size.height + margin.top + margin.bottom > containing_height);
            placed_vertical.offset = static_offset(placement, containing_height,
                fragment.size.height, vertical.start_margin,
                vertical.end_margin, child.id);
        }
    } else if ((parent.style.display == Display::Block
        || parent.style.display == Display::FlowRoot) && (static_x || static_y)) {
        if (static_x) {
            placed_horizontal.offset = checked(static_cast<double>(padding.left)
                + static_offset(writing_start(true, parent.style.box.direction),
                    content_width, fragment.size.width, margin.left, margin.right,
                    child.id), child.id);
        }
        if (static_y) {
            placed_vertical.offset = checked(static_cast<double>(padding.top)
                + static_block_y.value_or(Unit{0}) + margin.top, child.id);
        }
    }
    fragment.nodes.front().margin = {horizontal.start_margin, vertical.start_margin,
        horizontal.end_margin, vertical.end_margin};
    translate(fragment,
        checked(static_cast<double>(containing.x) + placed_horizontal.offset, child.id),
        checked(static_cast<double>(containing.y) + placed_vertical.offset, child.id),
        child.id);
    return fragment;
}
} // namespace axisly::css::detail
