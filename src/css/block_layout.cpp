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

Unit collapsed_margin(Unit previous, Unit next, NodeId node) {
    const double positive = std::max<double>(0, std::max(previous, next));
    const double negative = std::min<double>(0, std::min(previous, next));
    return checked(positive + negative, node);
}

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
    const Unit outer_width = preferred_width.value_or(available_width.value_or(Unit{0}));
    const Unit content_width = std::max(Unit{0}, outer_width - *decoration_width);
    const auto preferred_height = constraints.known_height ? constraints.known_height : box.preferred.height;
    const Unit content_height_basis = preferred_height
        ? std::max(Unit{0}, *preferred_height - *decoration_height) : Unit{0};
    const auto border = edges(box.border, node.id);
    const auto padding = edges(box.padding, node.id);
    const Unit x_origin = checked(static_cast<double>(border.left) + padding.left, node.id);
    const Unit y_origin = checked(static_cast<double>(border.top) + padding.top, node.id);
    std::vector<Fragment> fragments(node.children.size());
    std::vector<FloatRegion> floats;
    Unit cursor = 0, pending_margin = 0;
    bool has_flow = false;
    for (std::size_t source = 0; source < node.children.size(); ++source) {
        const auto child_id = node.children[source];
        const auto& child = get(child_id);
        if (child.style.display == Display::None) {
            fragments[source] = hidden(child, depth + 1);
            continue;
        }
        if (child.style.position == Position::Absolute) continue;
        Constraints child_constraints;
        child_constraints.available.width = AvailableSpace::definite(content_width);
        child_constraints.available.height = AvailableSpace::indefinite();
        child_constraints.containing_width = content_width;
        if (preferred_height) child_constraints.containing_height = content_height_basis;
        ResolvedBox child_box;
        try { child_box = resolve_box(child.style.box, content_width,
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
            const Unit separation = has_flow
                ? collapsed_margin(pending_margin, margin.top, child_id) : margin.top;
            Unit y = checked(static_cast<double>(cursor) + separation, child_id);
            if (child.style.clear != ClearSide::None) {
                y = std::max(y, clear_bottom(floats, child.style.clear));
            }
            translate(nested,
                checked(static_cast<double>(x_origin) + margin.left, child_id),
                checked(static_cast<double>(y_origin) + y, child_id), child_id);
            cursor = checked(static_cast<double>(y) + nested.size.height, child_id);
            pending_margin = margin.bottom;
            has_flow = true;
        }
        const auto [relative_x, relative_y] = relative_offset(child,
            Size{content_width, content_height_basis}, node.id);
        translate(nested, relative_x, relative_y, child_id);
        fragments[source] = std::move(nested);
    }
    Unit content_height = has_flow
        ? checked(static_cast<double>(cursor) + pending_margin, node.id) : Unit{0};
    if (node.style.display == Display::FlowRoot) {
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
    for (std::size_t source = 0; source < node.children.size(); ++source) {
        const auto id = node.children[source];
        if (get(id).style.position == Position::Absolute
            && get(id).style.display != Display::None) {
            fragments[source] = absolute(node, id, candidate, box, depth);
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
