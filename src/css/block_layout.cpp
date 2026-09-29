#include "layout_engine.hpp"
#include "block_float_context.hpp"

#include <algorithm>
#include <iterator>
#include <vector>

namespace axisly::css::detail {
namespace {
bool scroll_container(Overflow overflow) {
    return overflow == Overflow::Hidden || overflow == Overflow::Scroll;
}

bool establishes_formatting_context(const LayoutNode& node) {
    return node.style.display == Display::FlowRoot
        || node.style.display == Display::Flex
        || node.style.display == Display::Grid
        || node.style.contain != Contain::None
        || scroll_container(node.style.overflow_x)
        || scroll_container(node.style.overflow_y);
}
} // namespace

Fragment Engine::block(const LayoutNode& node, const Constraints& constraints,
                       const ResolvedBox& box, std::size_t depth,
                       BlockContext* block_context) {
    const auto decoration_width = box.horizontal_decoration();
    const auto decoration_height = box.vertical_decoration();
    if (!decoration_width || !decoration_height) {
        throw LayoutFailure(LayoutErrorCode::UnresolvedPercentage, node.id,
            "Block decoration requires a definite containing inline size");
    }
    const auto requested = requested_size(constraints, box);
    const auto preferred_width = requested.width;
    const auto available_width = constraints.available.width.definite_value();
    const bool content_based_width = !preferred_width
        && (constraints.content_based_width
            || node.style.float_side != FloatSide::None
            || node.style.item_is_table || node.style.box.replaced
            || !available_width);
    const Unit measured_width = content_based_width
        ? checked(static_cast<double>(block_content_based_width(node,
            subtract_decoration(constraints.available.width, *decoration_width),
            depth)) + *decoration_width, node.id) : Unit{0};
    const Unit outer_width = constrain_axis(
        preferred_width.value_or(content_based_width
            ? measured_width : available_width.value_or(Unit{0})),
        box.minimum.width, box.maximum.width);
    // A content-sized Block determines its outer width before cyclic percentage
    // edges can be used. Taffy then resolves those edges against that width for
    // child layout while retaining the original reported box edges.
    const ResolvedBox flow_box = constraints.containing_width ? box
        : resolve_layout_box(node, outer_width, constraints.containing_height);
    const auto flow_decoration_width = flow_box.horizontal_decoration();
    const auto flow_decoration_height = flow_box.vertical_decoration();
    const Unit content_width = std::max(Unit{0}, outer_width - *flow_decoration_width);
    // An intrinsic outer width provides space for placement, but it does not
    // turn the containing block into a definite percentage basis for children.
    const std::optional<Unit> child_percentage_width =
        (constraints.containing_width || preferred_width)
            ? std::optional<Unit>(content_width) : std::nullopt;
    auto preferred_height = requested.height;
    if (!preferred_height) {
        preferred_height = transfer_aspect_ratio({outer_width, std::nullopt}, box).height;
    }
    if (preferred_height) {
        *preferred_height = constrain_axis(*preferred_height,
            box.minimum.height, box.maximum.height);
    }
    const Unit content_height_basis = preferred_height
        ? std::max(Unit{0}, *preferred_height - *flow_decoration_height) : Unit{0};
    const auto border = edges(flow_box.border, node.id);
    const auto padding = edges(flow_box.padding, node.id);
    const Unit x_origin = checked(static_cast<double>(border.left) + padding.left, node.id);
    const Unit y_origin = checked(static_cast<double>(border.top) + padding.top, node.id);
    std::vector<Fragment> fragments(node.children.size());
    std::vector<std::optional<Unit>> static_block_y(node.children.size());
    FloatContext owned_floats;
    BlockContext root_context{owned_floats, 0, 0};
    BlockContext& context = block_context ? *block_context : root_context;
    FloatContext& floats = context.floats;
    const FloatBounds float_bounds{context.origin_x,
        checked(static_cast<double>(context.origin_x) + content_width, node.id)};
    const auto inline_position = [&](Fragment& fragment,
        const ResolvedBox& child_box, Insets margin,
        const std::optional<FloatSlot>& slot, NodeId child_id) {
        const bool auto_left = child_box.margin.left.kind == EdgeKind::Auto;
        const bool auto_right = child_box.margin.right.kind == EdgeKind::Auto;
        Unit used_left = margin.left, used_right = margin.right;
        const Unit horizontal_free = checked(static_cast<double>(content_width)
            - fragment.size.width - margin.left - margin.right, child_id);
        if (horizontal_free > 0 && (auto_left || auto_right)) {
            const Unit share = checked(static_cast<double>(horizontal_free)
                / (static_cast<int>(auto_left) + static_cast<int>(auto_right)), child_id);
            if (auto_left) used_left = share;
            if (auto_right) used_right = share;
            fragment.nodes.front().margin.left = used_left;
            fragment.nodes.front().margin.right = used_right;
        }
        const bool rtl = node.style.box.direction == Direction::Rtl;
        Unit x = rtl
            ? checked(static_cast<double>(content_width)
                - fragment.size.width - used_right, child_id)
            : used_left;
        const Unit free_align = checked(static_cast<double>(content_width)
            - fragment.size.width - used_left - used_right, child_id);
        if (free_align > 0) {
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
        if (slot) {
            x = checked(static_cast<double>(slot->x)
                + (rtl ? static_cast<double>(slot->width)
                    - fragment.size.width : 0.0), child_id);
        }
        return x;
    };
    Unit cursor = 0;
    MarginStrut pending_margin, leading_margin;
    bool has_flow = false, has_nonempty_flow = false;
    bool leading_open = true, leading_clear = false, placed_local_float = false;
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
        const bool intrinsic_width = child.style.item_is_table
            || child.style.box.replaced;
        child_constraints.available.width = intrinsic_width
            ? AvailableSpace::max_content()
            : AvailableSpace::definite(content_width);
        child_constraints.available.height = AvailableSpace::indefinite();
        child_constraints.containing_width = child_percentage_width;
        if (preferred_height) child_constraints.containing_height = content_height_basis;
        ResolvedBox child_box;
        try { child_box = resolve_layout_box(child, child_percentage_width,
            child_constraints.containing_height); }
        catch (const std::invalid_argument& error) {
            throw LayoutFailure(LayoutErrorCode::InvalidStyle, child_id, error.what());
        }
        const auto margin = edges(child_box.margin, child_id);
        if (child.style.float_side == FloatSide::None
            && !intrinsic_width
            && child.style.box.size.width.kind() == LengthKind::Auto) {
            child_constraints.known_width = std::max(Unit{0}, checked(
                static_cast<double>(content_width) - margin.left - margin.right,
                child_id));
        }
        std::optional<FloatSlot> formatting_slot;
        if (child.style.float_side == FloatSide::None
            && establishes_formatting_context(child) && !floats.empty()) {
            const Unit separation = pending_margin.merged(
                MarginStrut::from(margin.top)).resolve(child_id);
            Unit estimated_y = checked(static_cast<double>(context.origin_y)
                + cursor + separation, child_id);
            if (child.style.clear != ClearSide::None) {
                estimated_y = std::max(estimated_y,
                    floats.clear_bottom(child.style.clear));
            }
            const auto preferred = child_box.preferred.width;
            const auto required = preferred
                ? std::optional<Unit>(constrain_axis(*preferred,
                    child_box.minimum.width, child_box.maximum.width))
                : child_box.minimum.width;
            formatting_slot = floats.place_formatting_context(estimated_y,
                margin.left, margin.right, required, float_bounds, child_id);
            formatting_slot->x = checked(static_cast<double>(formatting_slot->x)
                - context.origin_x, child_id);
            formatting_slot->y = checked(static_cast<double>(formatting_slot->y)
                - context.origin_y, child_id);
            if (child.style.box.size.width.kind() == LengthKind::Auto
                && !intrinsic_width) {
                child_constraints.known_width = formatting_slot->width;
            }
        }
        const bool same_bfc = child.style.display == Display::Block
            && child.style.float_side == FloatSide::None
            && child.style.position != Position::Absolute
            && !intrinsic_width && !establishes_formatting_context(child);
        FloatContext probe_floats;
        BlockContext probe_context{probe_floats, 0, 0};
        auto nested = layout(child_id, child_constraints, depth + 1,
            same_bfc && !child.children.empty() ? &probe_context : nullptr);
        if (same_bfc && !child.children.empty()) {
            const MarginStrut child_top = nested.block_margins
                ? nested.block_margins->top : MarginStrut::from(margin.top);
            Unit y = checked(static_cast<double>(cursor)
                + pending_margin.merged(child_top).resolve(child_id), child_id);
            if (child.style.clear != ClearSide::None) {
                y = std::max(y, checked(static_cast<double>(
                    floats.clear_bottom(child.style.clear)) - context.origin_y, child_id));
            }
            const auto child_border = edges(child_box.border, child_id);
            const auto child_padding = edges(child_box.padding, child_id);
            // Same-BFC descendants use the containing block's inline insets.
            // The child's final border-box x may differ under RTL, auto margins,
            // or text-align; it does not redefine the float coordinate space.
            BlockContext child_context{floats,
                checked(static_cast<double>(context.origin_x) + margin.left
                    + child_border.left + child_padding.left, child_id),
                checked(static_cast<double>(context.origin_y) + y
                    + child_border.top + child_padding.top, child_id)};
            nested = layout(child_id, child_constraints, depth + 1, &child_context);
        }
        if (child.style.float_side != FloatSide::None) {
            const Unit outer_float_width = checked(static_cast<double>(nested.size.width)
                + margin.left + margin.right, child_id);
            const Unit outer_float_height = checked(static_cast<double>(nested.size.height)
                + margin.top + margin.bottom, child_id);
            const auto [float_x, float_y] = floats.place_float(
                child.style.float_side, child.style.clear, outer_float_width,
                outer_float_height,
                checked(static_cast<double>(context.origin_y) + cursor, child_id),
                float_bounds, child_id);
            const Unit left = checked(static_cast<double>(float_x)
                - context.origin_x + margin.left, child_id);
            const Unit top = checked(static_cast<double>(float_y)
                - context.origin_y + margin.top, child_id);
            translate(nested,
                checked(static_cast<double>(x_origin) + left, child_id),
                checked(static_cast<double>(y_origin) + top, child_id), child_id);
            floats.add(child.style.float_side, float_x, float_y,
                outer_float_width, outer_float_height, child_id);
            placed_local_float = true;
        } else {
            const MarginStrut child_top = nested.block_margins
                ? nested.block_margins->top : MarginStrut::from(margin.top);
            const MarginStrut child_bottom = nested.block_margins
                ? nested.block_margins->bottom : MarginStrut::from(margin.bottom);
            const bool child_through = nested.block_margins
                && nested.block_margins->through;
            if (leading_open) {
                if (child.style.clear != ClearSide::None || placed_local_float) {
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
                y = std::max(y, checked(static_cast<double>(
                    floats.clear_bottom(child.style.clear)) - context.origin_y, child_id));
            }
            if (formatting_slot) y = std::max(y, formatting_slot->y);
            const Unit x = inline_position(nested, child_box, margin,
                formatting_slot, child_id);
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
    const auto own_margin = edges(box.margin, node.id);
    BlockMargins margins{MarginStrut::from(own_margin.top),
                         MarginStrut::from(own_margin.bottom), false};
    const bool block_flow_parent = node.parent
        && (get(*node.parent).style.display == Display::Block
            || get(*node.parent).style.display == Display::FlowRoot);
    const bool independent = node.style.display != Display::Block
        || node.style.position == Position::Absolute
        || node.style.item_is_table || node.style.box.replaced
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
        && !has_nonempty_flow && !placed_local_float;
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
    const bool contains_floats = !node.parent || node.style.display == Display::FlowRoot
        || node.style.position == Position::Absolute
        || node.style.float_side != FloatSide::None
        || node.style.contain != Contain::None
        || scroll_container(node.style.overflow_x)
        || scroll_container(node.style.overflow_y);
    Size candidate{
        outer_width,
        preferred_height ? *preferred_height
            : checked(static_cast<double>(content_height) + *flow_decoration_height, node.id),
    };
    if (contains_floats && !preferred_height) {
        candidate.height = std::max(candidate.height, checked(
            static_cast<double>(y_origin) + floats.maximum_bottom()
                - context.origin_y, node.id));
    }
    candidate = constrain_size(candidate, box);
    Fragment result;
    result.size = candidate;
    result.nodes.push_back(make_layout(node, candidate, box));
    result.nodes.front().overflow_origin_border = border;
    for (std::size_t source = 0; source < node.children.size(); ++source) {
        const auto& child = get(node.children[source]);
        if (child.style.display == Display::None
            || child.style.position == Position::Absolute
            || child.style.float_side != FloatSide::None) continue;
        const auto baseline = child_baseline(fragments[source], child.style);
        if (!baseline) continue;
        result.nodes.front().first_baseline = checked(
            static_cast<double>(fragments[source].nodes.front().border_box.y)
                + *baseline, node.id);
        break;
    }
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
            fragments[source] = absolute(node, id, candidate, flow_box, depth,
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
