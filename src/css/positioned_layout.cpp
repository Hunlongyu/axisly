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
                          std::size_t depth) {
    const auto& child = get(child_id);
    const auto border = edges(parent_box.border, parent.id);
    const Unit containing_width = std::max(Unit{0}, checked(
        static_cast<double>(parent_size.width) - border.left - border.right, parent.id));
    const Unit containing_height = std::max(Unit{0}, checked(
        static_cast<double>(parent_size.height) - border.top - border.bottom, parent.id));
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
    const Unit x = left ? checked(static_cast<double>(*left) + margin.left, child.id)
        : right ? checked(static_cast<double>(containing_width) - *right
            - fragment.size.width - margin.right, child.id) : margin.left;
    const Unit y = top ? checked(static_cast<double>(*top) + margin.top, child.id)
        : bottom ? checked(static_cast<double>(containing_height) - *bottom
            - fragment.size.height - margin.bottom, child.id) : margin.top;
    translate(fragment,
        checked(static_cast<double>(border.left) + x, child.id),
        checked(static_cast<double>(border.top) + y, child.id), child.id);
    return fragment;
}
} // namespace axisly::css::detail
