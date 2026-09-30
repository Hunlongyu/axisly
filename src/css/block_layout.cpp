#include "block_item_sizing.hpp"
#include "block_flow_state.hpp"
#include "block_float_context.hpp"

#include <algorithm>
#include <iterator>
#include <vector>

namespace axisly::css::detail {
namespace {
bool scroll_container(Overflow overflow) {
    return overflow == Overflow::Hidden || overflow == Overflow::Scroll;
}

// Taffy positions these block items beside floats. A Block with align-content
// starts a new context for its own children but its border box still overlaps
// ancestor floats like an ordinary Block item.
bool avoids_floats_as_block_item(const LayoutNode& node) {
    return node.style.display == Display::FlowRoot
        || node.style.display == Display::Flex
        || node.style.display == Display::Grid
        || node.style.contain != Contain::None
        || scroll_container(node.style.overflow_x)
        || scroll_container(node.style.overflow_y);
}

Unit block_alignment_offset(Unit free, ContentAlignment alignment) {
    if (alignment.safety == AlignmentSafety::Safe && free < 0) return 0;
    switch (alignment.position) {
    case ContentPosition::End:
    case ContentPosition::FlexEnd: return free;
    case ContentPosition::Center: return free / 2;
    case ContentPosition::SpaceAround:
    case ContentPosition::SpaceEvenly: return free > 0 ? free / 2 : Unit{0};
    case ContentPosition::Start:
    case ContentPosition::FlexStart:
    case ContentPosition::Stretch:
    case ContentPosition::SpaceBetween: return 0;
    }
    return 0;
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
    auto requested = requested_size(constraints, box);
    if (box.aspect_ratio && (constraints.known_width || constraints.known_height)) {
        // Taffy transfers parent-known Block dimensions as border-box values.
        // An auto width inferred from available space is not a known dimension.
        auto ratio_box = box;
        ratio_box.box_sizing = BoxSizing::BorderBox;
        const auto derived = transfer_aspect_ratio(
            {constraints.known_width, constraints.known_height}, ratio_box);
        if (!requested.width) requested.width = derived.width;
        if (!requested.height) requested.height = derived.height;
    }
    const auto preferred_width = requested.width;
    const auto available_width = constraints.available.width.definite_value();
    const bool content_based_width = !preferred_width
        && (constraints.content_based_width || constraints.sizing_mode == SizingMode::Content
            || node.style.float_side != FloatSide::None
            || node.style.item_is_table || node.style.box.replaced
            || !available_width);
    const ResolvedDimensions early_content_basis{
        preferred_width ? std::optional<Unit>(std::max(Unit{0},
            *preferred_width - *decoration_width)) : std::nullopt,
        requested.height && (!constraints.known_height || constraints.known_height_is_definite)
            ? std::optional<Unit>(std::max(Unit{0}, *requested.height - *decoration_height))
            : std::nullopt};
    const Unit measured_width = content_based_width
        ? checked(static_cast<double>(block_content_based_width(node,
            subtract_decoration(constraints.available.width, *decoration_width),
            early_content_basis, depth)) + *decoration_width, node.id) : Unit{0};
    const Unit outer_width = constraints.known_width_is_final && constraints.known_width
        ? *constraints.known_width : constrain_axis(
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
    const Unit positioning_width = outer_width - *flow_decoration_width;
    const Unit content_width = std::max(Unit{0}, positioning_width);
    // An intrinsic outer width provides space for placement, but it does not
    // turn the containing block into a definite percentage basis for children.
    // A content-based width is known only after intrinsic child measurement.
    // Cyclic percentage child sizes remain unresolved for this layout pass.
    const std::optional<Unit> child_percentage_width = !content_based_width
        ? std::optional<Unit>(content_width) : std::nullopt;
    auto preferred_height = requested.height;
    if (preferred_height && !constraints.known_height_is_final) {
        *preferred_height = constrain_axis(*preferred_height,
            box.minimum.height, box.maximum.height);
    }
    const Unit content_height_basis = preferred_height
        ? std::max(Unit{0}, *preferred_height - *flow_decoration_height) : Unit{0};
    const auto border = edges(flow_box.border, node.id);
    const auto padding = edges(flow_box.padding, node.id);
    const Unit x_origin = content_origin_x(flow_box, border, padding, node.id);
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
        const Unit horizontal_free = checked(static_cast<double>(positioning_width)
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
            ? checked(static_cast<double>(positioning_width)
                - fragment.size.width - used_right, child_id)
            : used_left;
        const Unit free_align = checked(static_cast<double>(positioning_width)
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
    const bool block_flow_parent = node.parent
        && (get(*node.parent).style.display == Display::Block
            || get(*node.parent).style.display == Display::FlowRoot);
    const bool independent = node.style.display != Display::Block
        || node.style.position == Position::Absolute
        || node.style.item_is_table || node.style.box.replaced
        || node.style.align_content.has_value()
        || node.style.contain != Contain::None
        || scroll_container(node.style.overflow_x)
        || scroll_container(node.style.overflow_y)
        || node.style.float_side != FloatSide::None;
    const bool collapsible_top = block_flow_parent && !independent
        && border.top == 0 && padding.top == 0;
    const bool collapsible_bottom = block_flow_parent && !independent
        && border.bottom == 0 && padding.bottom == 0 && !preferred_height;
    BlockFlowState flow(collapsible_top);
    if (!collapsible_top) context.commit_strut();
    const auto clear_threshold = [&](ClearSide clear, NodeId child_id) -> std::optional<Unit> {
        if (const auto value = floats.clear_threshold(clear)) {
            return checked(static_cast<double>(*value) - context.origin_y, child_id);
        }
        return std::nullopt;
    };
    for (std::size_t source = 0; source < node.children.size(); ++source) {
        const auto child_id = node.children[source];
        const auto& child = get(child_id);
        if (child.style.display == Display::None) {
            fragments[source] = hidden(child, depth + 1);
            continue;
        }
        if (child.style.position == Position::Absolute) {
            static_block_y[source] = flow.absolute_y(child_id);
            continue;
        }
        const auto height_basis = preferred_height
            && (constraints.known_height_is_definite || box.preferred.height)
            ? std::optional<Unit>{content_height_basis} : std::nullopt;
        auto sizing = resolve_block_item(child,
            {child_percentage_width, height_basis}, content_width);
        auto& child_constraints = sizing.constraints;
        const auto& child_box = sizing.box;
        const auto margin = edges(child_box.margin, child_id);
        const bool intrinsic_width = sizing.intrinsic_width;
        const bool unresolved_normal_width = sizing.unresolved_normal_width;
        std::optional<FloatSlot> formatting_slot;
        if (child.style.float_side == FloatSide::None
            && avoids_floats_as_block_item(child) && !floats.empty()) {
            const auto estimated = flow.position(MarginStrut::from(margin.top),
                clear_threshold(child.style.clear, child_id),
                context.has_adjoining_float(child.style.clear), std::nullopt, child_id);
            const Unit estimated_y = checked(static_cast<double>(context.origin_y)
                + estimated.y, child_id);
            formatting_slot = floats.place_formatting_context(estimated_y,
                margin.left, margin.right,
                {child_box.preferred.width, child_box.minimum.width, child_box.maximum.width},
                node.style.box.direction, float_bounds, child_id);
            formatting_slot->x = checked(static_cast<double>(formatting_slot->x)
                - context.origin_x, child_id);
            formatting_slot->y = checked(static_cast<double>(formatting_slot->y)
                - context.origin_y, child_id);
            if (unresolved_normal_width && !child_box.preferred.width
                && !intrinsic_width) {
                child_constraints.known_width = constrain_axis(
                    formatting_slot->stretch_width, child_box.minimum.width,
                    child_box.maximum.width);
            }
        }
        const bool same_bfc = child.style.display == Display::Block
            && child.style.float_side == FloatSide::None
            && child.style.position != Position::Absolute
            && !intrinsic_width && !avoids_floats_as_block_item(child)
            && !child.style.align_content.has_value();
        const bool share_float_context = same_bfc && !child.children.empty()
            && (!floats.empty() || has_float_subtree(child_id, depth + 1));
        Fragment nested;
        if (share_float_context) {
            // Float context coordinates follow the provisional margin edge.
            // The final border edge is selected from the returned collapsed
            // strut and may differ when that strut escapes through the parent.
            Unit y = checked(static_cast<double>(flow.absolute_y(child_id)) + margin.top, child_id);
            if (const auto cleared = clear_threshold(child.style.clear, child_id)) {
                y = std::max(y, *cleared);
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
                    + child_border.top + child_padding.top, child_id),
                context.adjoining_floats, {}};
            nested = layout(child_id, child_constraints, depth + 1, &child_context);
            context.merge_adjoining(child_context);
        } else {
            nested = layout(child_id, child_constraints, depth + 1);
        }
        // Block item metadata is resolved when the parent's item list is
        // formed. The child's own layout may use a later containing width,
        // but it does not replace the parent's reported padding and border.
        const auto item_padding = edges(child_box.padding, child_id);
        const auto item_border = edges(child_box.border, child_id);
        if (nested.nodes.front().padding != item_padding
            || nested.nodes.front().border != item_border) {
            publish_item_box(nested, child, child_box);
        }
        if (child.style.float_side != FloatSide::None) {
            const Unit outer_float_width = checked(static_cast<double>(nested.size.width)
                + margin.left + margin.right, child_id);
            const Unit outer_float_height = checked(static_cast<double>(nested.size.height)
                + margin.top + margin.bottom, child_id);
            const auto [float_x, float_y] = floats.place_float(
                child.style.float_side, child.style.clear, outer_float_width,
                outer_float_height,
                checked(static_cast<double>(context.origin_y) + flow.float_y(child_id), child_id),
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
            if (collapsible_top && flow.leading_open()) {
                context.adjoining_floats[child.style.float_side == FloatSide::Left ? 0 : 1] = true;
            }
        } else {
            const MarginStrut child_top = nested.block_margins
                ? nested.block_margins->top : MarginStrut::from(margin.top);
            const MarginStrut child_bottom = nested.block_margins
                ? nested.block_margins->bottom : MarginStrut::from(margin.bottom);
            const bool child_through = nested.block_margins
                && nested.block_margins->through;
            const auto placed = flow.position(child_top,
                clear_threshold(child.style.clear, child_id),
                context.has_adjoining_float(child.style.clear),
                formatting_slot ? std::optional<Unit>{formatting_slot->y} : std::nullopt, child_id);
            const Unit y = placed.y;
            const Unit x = inline_position(nested, child_box, margin,
                formatting_slot, child_id);
            translate(nested,
                checked(static_cast<double>(x_origin) + x, child_id),
                checked(static_cast<double>(y_origin) + y, child_id), child_id);
            flow.commit(placed, child_top, child_bottom, child_through,
                nested.size.height, child_id);
            if (!child_through || placed.clearance) context.commit_strut();
        }
        const auto [relative_x, relative_y] = relative_offset(child,
            Size{content_width, content_height_basis},
            node.style.box.direction, node.id);
        translate(nested, relative_x, relative_y, child_id);
        fragments[source] = std::move(nested);
    }
    const bool through = collapsible_top && collapsible_bottom
        && !box.minimum.height.value_or(Unit{0}) && !flow.has_nonempty_flow();
    const Unit content_height = through ? Unit{0}
        : flow.content_height(collapsible_bottom, node.id);
    const bool contains_floats = !node.parent || node.style.display == Display::FlowRoot
        || node.style.position == Position::Absolute
        || node.style.float_side != FloatSide::None
        || node.style.align_content.has_value()
        || node.style.contain != Contain::None
        || scroll_container(node.style.overflow_x)
        || scroll_container(node.style.overflow_y);
    Unit intrinsic_outer_height = checked(
        static_cast<double>(content_height) + *flow_decoration_height, node.id);
    if (contains_floats) {
        intrinsic_outer_height = std::max(intrinsic_outer_height, checked(
            static_cast<double>(y_origin) + floats.maximum_bottom()
                - context.origin_y, node.id));
    }
    Size candidate{
        outer_width,
        preferred_height ? *preferred_height
            : intrinsic_outer_height,
    };
    // A finalized border-box axis has already applied style bounds. A
    // provisional parent item size still needs this node's own min/max.
    if (!constraints.known_width_is_final) {
        candidate.width = constrain_axis(candidate.width,
            box.minimum.width, box.maximum.width);
    }
    if (!constraints.known_height_is_final) {
        candidate.height = constrain_axis(candidate.height,
            box.minimum.height, box.maximum.height);
    }
    const auto own_margin = edges(box.margin, node.id);
    const bool minimum_blocks_bottom = box.minimum.height.value_or(Unit{0}) > 0
        && *box.minimum.height >= candidate.height;
    const auto margins = flow.margins(MarginStrut::from(own_margin.top),
        MarginStrut::from(own_margin.bottom),
        collapsible_bottom && !minimum_blocks_bottom, through);
    if (node.style.align_content) {
        const Unit free = checked(static_cast<double>(candidate.height)
            - intrinsic_outer_height, node.id);
        const Unit offset = block_alignment_offset(free, *node.style.align_content);
        if (offset != 0) {
            for (std::size_t source = 0; source < node.children.size(); ++source) {
                const auto& child = get(node.children[source]);
                if (child.style.display != Display::None
                    && child.style.position != Position::Absolute) {
                    translate(fragments[source], 0, offset, child.id);
                }
            }
        }
    }
    Fragment result;
    result.size = candidate;
    result.nodes.push_back(make_layout(node, candidate, box));
    result.nodes.front().overflow_origin_border = border;
    result.nodes.front().margin.top = margins.top.resolve(node.id);
    result.nodes.front().margin.bottom = margins.bottom.resolve(node.id);
    result.block_margins = margins;
    if (detail_ == FragmentDetail::Size) return result;
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
    for (std::size_t source = 0; source < node.children.size(); ++source) {
        const auto id = node.children[source];
        if (get(id).style.position == Position::Absolute
            && get(id).style.display != Display::None) {
            fragments[source] = absolute(node, id, candidate, flow_box, depth,
                std::nullopt, static_block_y[source]);
        }
        auto& nested = fragments[source];
        if (detail_ == FragmentDetail::Full) {
            result.nodes.insert(result.nodes.end(),
                std::make_move_iterator(nested.nodes.begin()),
                std::make_move_iterator(nested.nodes.end()));
            result.grid_tracks.insert(result.grid_tracks.end(),
                std::make_move_iterator(nested.grid_tracks.begin()),
                std::make_move_iterator(nested.grid_tracks.end()));
        }
    }
    return result;
}
} // namespace axisly::css::detail
