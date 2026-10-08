#include "layout_engine.hpp"

#include <algorithm>
#include <limits>

namespace axisly::css::detail {
namespace {
bool scroll_container(const Style& style) {
    return style.overflow_x == Overflow::Hidden
        || style.overflow_x == Overflow::Scroll
        || style.overflow_y == Overflow::Hidden
        || style.overflow_y == Overflow::Scroll;
}

Unit right(const Rect& rect, NodeId node) {
    return checked(static_cast<double>(rect.x) + rect.width, node);
}

Unit bottom(const Rect& rect, NodeId node) {
    return checked(static_cast<double>(rect.y) + rect.height, node);
}

void unite(Rect& destination, Unit left, Unit top, Unit edge_right,
           Unit edge_bottom, NodeId node) {
    const Unit new_left = std::min(destination.x, left);
    const Unit new_top = std::min(destination.y, top);
    const Unit new_right = std::max(right(destination, node), edge_right);
    const Unit new_bottom = std::max(bottom(destination, node), edge_bottom);
    destination = {new_left, new_top,
        checked(static_cast<double>(new_right) - new_left, node),
        checked(static_cast<double>(new_bottom) - new_top, node)};
}
} // namespace

void compute_scrollable_overflow(Fragment& fragment, const LayoutNode& parent_node,
                                  const LayoutNodes& nodes) {
    fragment.overflow_dirty = false;
    if (!fragment.descendants || parent_node.style.display == Display::None) return;
    auto& parent = fragment.mutable_root_layout();
    const auto origin_border = fragment.root.read().overflow_origin_border;
    Rect inflow{}, absolute{};
    const auto include = [&](const NodeLayout& layout) {
        const auto& node = *nodes.find(layout.node.value);
        if (node.style.display == Display::None) return;
        const bool clipped = scroll_container(node.style)
            || node.style.contain != Contain::None;
        const bool propagate_x = !clipped
            && node.style.overflow_x == Overflow::Visible;
        const bool propagate_y = !clipped
            && node.style.overflow_y == Overflow::Visible;
        const Unit child_right = propagate_x
            ? std::max(layout.border_box.width,
                right(layout.scrollable_overflow_rect, node.id))
            : layout.border_box.width;
        const Unit child_bottom = propagate_y
            ? std::max(layout.border_box.height,
                bottom(layout.scrollable_overflow_rect, node.id))
            : layout.border_box.height;
        if (child_right <= 0 || child_bottom <= 0) return;
        const Unit child_left = propagate_x
            ? std::min(Unit{0}, layout.scrollable_overflow_rect.x) : Unit{0};
        const Unit child_top = propagate_y
            ? std::min(Unit{0}, layout.scrollable_overflow_rect.y) : Unit{0};
        Unit location_x = checked(static_cast<double>(layout.border_box.x), node.id);
        if (parent_node.style.box.direction == Direction::Rtl) {
            // Mirror the physical right edge at layout precision. Folding
            // x + width into a double-precision subtraction changes which
            // side of a half-pixel a percentage-sized item lands on.
            const Unit physical_right = right(layout.border_box, node.id);
            const Unit from_right = checked(static_cast<double>(fragment.root.read().overflow_coordinate_width
                .value_or(parent.border_box.width)) - physical_right, node.id);
            location_x = checked(static_cast<double>(from_right)
                - origin_border.right, node.id);
        } else {
            location_x = checked(static_cast<double>(location_x)
                - origin_border.left, node.id);
        }
        const Unit location_y = checked(static_cast<double>(layout.border_box.y)
            - origin_border.top, node.id);
        const Unit contribution_left = checked(static_cast<double>(location_x)
            + child_left, node.id);
        const Unit contribution_top = checked(static_cast<double>(location_y)
            + child_top, node.id);
        const Unit contribution_right = checked(static_cast<double>(location_x)
            + child_right, node.id);
        const Unit contribution_bottom = checked(static_cast<double>(location_y)
            + child_bottom, node.id);
        if (scroll_container(parent_node.style)
            && (contribution_right <= 0 || contribution_bottom <= 0)) return;
        Rect& destination = node.style.position == Position::Absolute ? absolute : inflow;
        unite(destination, contribution_left, contribution_top,
            contribution_right, contribution_bottom, parent.node);
    };
    // Child roots are local to this container. Ancestor translations affect
    // publication, not its scrollable extent. Traverse in the same reverse
    // sibling order as the former bottom-up pass, retaining union rounding.
    const auto& children = *fragment.descendants;
    if (children.branched) {
        for (auto i = children.branches.size(); i-- > 0;) include(children.branches[i].root);
    } else {
        for (auto i = children.leaves.size(); i-- > 0;) include(children.leaves[i]);
    }
    if (scroll_container(parent_node.style)) {
        const Unit inline_end = parent_node.style.box.direction == Direction::Rtl
            ? parent.padding.left : parent.padding.right;
        inflow.width = checked(static_cast<double>(inflow.width) + inline_end, parent.node);
        inflow.height = checked(static_cast<double>(inflow.height) + parent.padding.bottom, parent.node);
    }
    unite(inflow, absolute.x, absolute.y, right(absolute, parent.node), bottom(absolute, parent.node), parent.node);
    parent.scrollable_overflow_rect = inflow;
}
} // namespace axisly::css::detail
