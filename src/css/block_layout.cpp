#include "layout_engine.hpp"

#include <algorithm>
#include <iterator>
#include <vector>

namespace axisly::css::detail {
namespace {
struct FloatRegion {
    FloatSide side{};
    Unit left{}, right{}, top{}, bottom{};
};

Unit clear_bottom(const std::vector<FloatRegion>& floats, ClearSide clear) {
    Unit bottom = 0;
    for (const auto& region : floats) {
        if (clear == ClearSide::Both
            || (clear == ClearSide::Left && region.side == FloatSide::Left)
            || (clear == ClearSide::Right && region.side == FloatSide::Right)) {
            bottom = std::max(bottom, region.bottom);
        }
    }
    return bottom;
}

std::pair<Unit, Unit> float_position(const std::vector<FloatRegion>& floats,
    FloatSide side, Unit content_width, Unit outer_width, Unit outer_height,
    Unit initial_y, NodeId node) {
    Unit y = initial_y;
    for (std::size_t attempt = 0; attempt <= floats.size(); ++attempt) {
        Unit left = 0, right = content_width;
        std::optional<Unit> next_bottom;
        for (const auto& region : floats) {
            if (region.top >= y + outer_height || region.bottom <= y) continue;
            if (region.side == FloatSide::Left) left = std::max(left, region.right);
            else right = std::min(right, region.left);
            if (region.bottom > y) {
                next_bottom = std::min(next_bottom.value_or(region.bottom), region.bottom);
            }
        }
        if (right - left >= outer_width || !next_bottom) {
            const Unit x = side == FloatSide::Left ? left
                : checked(static_cast<double>(right) - outer_width, node);
            return {x, y};
        }
        y = *next_bottom;
    }
    throw LayoutFailure(LayoutErrorCode::LimitExceeded, node,
        "Block float placement did not converge");
}
} // namespace

Fragment Engine::block(const LayoutNode& node, const Constraints& constraints,
                       const ResolvedBox& box, std::size_t depth) {
    const auto decoration_width = box.horizontal_decoration();
    const auto decoration_height = box.vertical_decoration();
    if (!decoration_width || !decoration_height) {
        throw LayoutFailure(LayoutErrorCode::UnresolvedPercentage, node.id,
            "Block decoration requires a definite containing inline size");
    }
    const auto preferred_width = constraints.known_width ? constraints.known_width : box.preferred.width;
    const auto available_width = constraints.available.width.definite_value();
    const Unit outer_width = constrain_axis(
        preferred_width.value_or(available_width.value_or(Unit{0})),
        box.minimum.width, box.maximum.width);
    const Unit content_width = std::max(Unit{0}, outer_width - *decoration_width);
    auto preferred_height = constraints.known_height
        ? constraints.known_height : box.preferred.height;
    if (preferred_height) {
        *preferred_height = constrain_axis(*preferred_height,
            box.minimum.height, box.maximum.height);
    }
    const Unit content_height_basis = preferred_height
        ? std::max(Unit{0}, *preferred_height - *decoration_height) : Unit{0};
    const auto border = edges(box.border, node.id);
    const auto padding = edges(box.padding, node.id);
    const Unit x_origin = checked(static_cast<double>(border.left) + padding.left, node.id);
    const Unit y_origin = checked(static_cast<double>(border.top) + padding.top, node.id);
    std::vector<Fragment> fragments(node.children.size());
    std::vector<std::optional<Unit>> static_block_y(node.children.size());
    std::vector<FloatRegion> floats;
    Unit cursor = 0;
    MarginStrut pending_margin, leading_margin;
    bool has_flow = false, has_nonempty_flow = false;
    bool leading_open = true, leading_clear = false;
    for (std::size_t source = 0; source < node.children.size(); ++source) {
        const auto child_id = node.children[source];
        const auto& child = get(child_id);
        if (child.style.display == Display::None) {
            fragments[source] = hidden(child, depth + 1);
            continue;
        }
        if (child.style.position == Position::Absolute) {
            static_block_y[source] = checked(static_cast<double>(cursor)
                + (has_flow ? pending_margin.resolve(child_id) : 0), child_id);
            continue;
        }
        Constraints child_constraints;
        child_constraints.available.width = AvailableSpace::definite(content_width);
        child_constraints.available.height = AvailableSpace::indefinite();
        child_constraints.containing_width = content_width;
        if (preferred_height) child_constraints.containing_height = content_height_basis;
        ResolvedBox child_box;
        try { child_box = resolve_layout_box(child, content_width,
            child_constraints.containing_height); }
        catch (const std::invalid_argument& error) {
            throw LayoutFailure(LayoutErrorCode::InvalidStyle, child_id, error.what());
        }
        const auto margin = edges(child_box.margin, child_id);
        if (child.style.float_side == FloatSide::None
            && child.style.box.size.width.kind() == LengthKind::Auto) {
            child_constraints.known_width = std::max(Unit{0}, checked(
                static_cast<double>(content_width) - margin.left - margin.right,
                child_id));
        }
        auto nested = layout(child_id, child_constraints, depth + 1);
        if (child.style.float_side != FloatSide::None) {
            const Unit outer_float_width = checked(static_cast<double>(nested.size.width)
                + margin.left + margin.right, child_id);
            const Unit outer_float_height = checked(static_cast<double>(nested.size.height)
                + margin.top + margin.bottom, child_id);
            const auto [float_x, float_y] = float_position(floats,
                child.style.float_side, content_width, outer_float_width,
                outer_float_height, cursor, child_id);
            const Unit left = checked(static_cast<double>(float_x) + margin.left, child_id);
            const Unit top = checked(static_cast<double>(float_y) + margin.top, child_id);
            translate(nested,
                checked(static_cast<double>(x_origin) + left, child_id),
                checked(static_cast<double>(y_origin) + top, child_id), child_id);
            floats.push_back({child.style.float_side, float_x,
                checked(static_cast<double>(float_x) + outer_float_width, child_id),
                float_y, checked(static_cast<double>(float_y) + outer_float_height, child_id)});
        } else {
            const MarginStrut child_top = nested.block_margins
                ? nested.block_margins->top : MarginStrut::from(margin.top);
            const MarginStrut child_bottom = nested.block_margins
                ? nested.block_margins->bottom : MarginStrut::from(margin.bottom);
            const bool child_through = nested.block_margins
                && nested.block_margins->through;
            if (leading_open) {
                if (child.style.clear != ClearSide::None || !floats.empty()) {
                    leading_clear = true;
                }
                if (!leading_clear) {
                    leading_margin = leading_margin.merged(child_top);
                    if (child_through) {
                        leading_margin = leading_margin.merged(child_bottom);
                    }
                }
                if (!child_through) leading_open = false;
            }
            const MarginStrut separation_margin = pending_margin.merged(child_top);
            const Unit separation = separation_margin.resolve(child_id);
            Unit y = checked(static_cast<double>(cursor) + separation, child_id);
            if (child.style.clear != ClearSide::None) {
                y = std::max(y, clear_bottom(floats, child.style.clear));
            }
            const bool auto_left = child_box.margin.left.kind == EdgeKind::Auto;
            const bool auto_right = child_box.margin.right.kind == EdgeKind::Auto;
            Unit used_left = margin.left, used_right = margin.right;
            const Unit horizontal_free = checked(static_cast<double>(content_width)
                - nested.size.width - margin.left - margin.right, child_id);
            if (horizontal_free > 0 && (auto_left || auto_right)) {
                const Unit share = checked(static_cast<double>(horizontal_free)
                    / (static_cast<int>(auto_left) + static_cast<int>(auto_right)), child_id);
                if (auto_left) used_left = share;
                if (auto_right) used_right = share;
                nested.nodes.front().margin.left = used_left;
                nested.nodes.front().margin.right = used_right;
            }
            Unit x = auto_left || auto_right
                ? used_left
                : node.style.box.direction == Direction::Rtl
                    ? checked(static_cast<double>(content_width)
                        - nested.size.width - used_right, child_id)
                    : used_left;
            const Unit free_align = checked(static_cast<double>(content_width)
                - nested.size.width - used_left - used_right, child_id);
            if (free_align > 0) {
                const bool rtl = node.style.box.direction == Direction::Rtl;
                if ((node.style.text_align == TextAlign::LegacyLeft && rtl)
                    || (node.style.text_align == TextAlign::LegacyRight && !rtl)) {
                    x = checked(static_cast<double>(x) + (rtl ? -free_align : free_align),
                        child_id);
                } else if (node.style.text_align == TextAlign::LegacyCenter) {
                    x = checked(static_cast<double>(x)
                        + (rtl ? -static_cast<double>(free_align) / 2
                            : static_cast<double>(free_align) / 2), child_id);
                }
            }
            translate(nested,
                checked(static_cast<double>(x_origin) + x, child_id),
                checked(static_cast<double>(y_origin) + y, child_id), child_id);
            if (child_through) {
                pending_margin = separation_margin.merged(child_bottom);
            } else {
                cursor = checked(static_cast<double>(y) + nested.size.height, child_id);
                pending_margin = child_bottom;
                has_nonempty_flow = true;
            }
            has_flow = true;
        }
        const auto [relative_x, relative_y] = relative_offset(child,
            Size{content_width, content_height_basis}, node.id);
        translate(nested, relative_x, relative_y, child_id);
        fragments[source] = std::move(nested);
    }
    const auto scroll_container = [](Overflow overflow) {
        return overflow == Overflow::Hidden || overflow == Overflow::Scroll;
    };
    const auto own_margin = edges(box.margin, node.id);
    BlockMargins margins{MarginStrut::from(own_margin.top),
                         MarginStrut::from(own_margin.bottom), false};
    const bool block_flow_parent = node.parent
        && (get(*node.parent).style.display == Display::Block
            || get(*node.parent).style.display == Display::FlowRoot);
    const bool independent = node.style.display != Display::Block
        || node.style.contain != Contain::None
        || scroll_container(node.style.overflow_x)
        || scroll_container(node.style.overflow_y)
        || node.style.float_side != FloatSide::None;
    const bool collapse_top = block_flow_parent && !independent
        && border.top == 0 && padding.top == 0 && !leading_clear;
    const bool collapse_bottom = block_flow_parent && !independent
        && border.bottom == 0 && padding.bottom == 0
        && !preferred_height && !box.minimum.height.value_or(Unit{0});
    const bool through = collapse_top && collapse_bottom
        && !has_nonempty_flow && floats.empty();
    Unit top_shift = 0;
    if (through) {
        const MarginStrut all = margins.top.merged(margins.bottom)
            .merged(pending_margin);
        margins.top = all;
        margins.bottom = all;
        margins.through = true;
        top_shift = pending_margin.resolve(node.id);
    } else if (collapse_top && has_flow) {
        margins.top = margins.top.merged(leading_margin);
        top_shift = leading_margin.resolve(node.id);
    }
    if (collapse_bottom && !through && has_flow) {
        margins.bottom = margins.bottom.merged(pending_margin);
    }
    Unit content_height = through ? Unit{0}
        : has_flow ? checked(static_cast<double>(cursor)
            + (collapse_bottom ? 0 : pending_margin.resolve(node.id))
            - top_shift, node.id) : Unit{0};
    if (top_shift != 0) {
        for (std::size_t source = 0; source < node.children.size(); ++source) {
            const auto& child = get(node.children[source]);
            if (child.style.display != Display::None
                && child.style.position != Position::Absolute
                && child.style.float_side == FloatSide::None) {
                translate(fragments[source], 0, -top_shift, child.id);
            }
        }
    }
    if (node.style.display == Display::FlowRoot
        || node.style.contain != Contain::None
        || scroll_container(node.style.overflow_x)
        || scroll_container(node.style.overflow_y)) {
        for (const auto& region : floats) content_height = std::max(content_height, region.bottom);
    }
    Size candidate{
        checked(static_cast<double>(content_width) + *decoration_width, node.id),
        preferred_height ? *preferred_height
            : checked(static_cast<double>(content_height) + *decoration_height, node.id),
    };
    candidate = constrain_size(candidate, box);
    Fragment result;
    result.size = candidate;
    result.nodes.push_back(make_layout(node, candidate, box));
    result.nodes.front().margin.top = margins.top.resolve(node.id);
    result.nodes.front().margin.bottom = margins.bottom.resolve(node.id);
    result.block_margins = margins;
    for (std::size_t source = 0; source < node.children.size(); ++source) {
        const auto id = node.children[source];
        if (get(id).style.position == Position::Absolute
            && get(id).style.display != Display::None) {
            if (static_block_y[source]) {
                static_block_y[source] = checked(
                    static_cast<double>(*static_block_y[source]) - top_shift, id);
            }
            fragments[source] = absolute(node, id, candidate, box, depth,
                std::nullopt, static_block_y[source]);
        }
        auto& nested = fragments[source];
        result.nodes.insert(result.nodes.end(),
            std::make_move_iterator(nested.nodes.begin()),
            std::make_move_iterator(nested.nodes.end()));
        result.grid_tracks.insert(result.grid_tracks.end(),
            std::make_move_iterator(nested.grid_tracks.begin()),
            std::make_move_iterator(nested.grid_tracks.end()));
    }
    return result;
}
} // namespace axisly::css::detail
