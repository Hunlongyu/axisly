#include "layout_engine.hpp"

#include <algorithm>
#include <unordered_map>

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

void compute_scrollable_overflow(std::vector<NodeLayout>& layouts,
    const std::unordered_map<std::uint64_t, LayoutNode>& nodes) {
    std::unordered_map<std::uint64_t, std::size_t> index;
    index.reserve(layouts.size());
    std::vector<Rect> inflow(layouts.size());
    std::vector<Rect> absolute(layouts.size());
    for (std::size_t i = 0; i < layouts.size(); ++i) {
        index.emplace(layouts[i].node.value, i);
    }
    for (std::size_t i = layouts.size(); i-- > 0;) {
        auto& layout = layouts[i];
        const auto& node = nodes.at(layout.node.value);
        if (node.style.display == Display::None) continue;
        if (!node.children.empty()) {
            if (scroll_container(node.style)) {
                const Unit inline_end = node.style.box.direction == Direction::Rtl
                    ? layout.padding.left : layout.padding.right;
                inflow[i].width = checked(static_cast<double>(inflow[i].width)
                    + inline_end, node.id);
                inflow[i].height = checked(static_cast<double>(inflow[i].height)
                    + layout.padding.bottom, node.id);
            }
            unite(inflow[i], absolute[i].x, absolute[i].y,
                right(absolute[i], node.id), bottom(absolute[i], node.id), node.id);
            layout.scrollable_overflow_rect = inflow[i];
        }
        if (!layout.parent) continue;
        auto& parent = layouts[index.at(layout.parent->value)];
        const auto& parent_style = nodes.at(layout.parent->value).style;
        if (parent_style.display == Display::None) continue;
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
        if (child_right <= 0 || child_bottom <= 0) continue;
        const Unit child_left = propagate_x
            ? std::min(Unit{0}, layout.scrollable_overflow_rect.x) : Unit{0};
        const Unit child_top = propagate_y
            ? std::min(Unit{0}, layout.scrollable_overflow_rect.y) : Unit{0};
        Unit location_x = checked(static_cast<double>(layout.border_box.x)
            - parent.border_box.x, node.id);
        if (parent_style.box.direction == Direction::Rtl) {
            location_x = checked(static_cast<double>(parent.border_box.width)
                - location_x - layout.border_box.width
                - parent.overflow_origin_border.right,
                node.id);
        } else {
            location_x = checked(static_cast<double>(location_x)
                - parent.overflow_origin_border.left, node.id);
        }
        const Unit location_y = checked(static_cast<double>(layout.border_box.y)
            - parent.border_box.y - parent.overflow_origin_border.top, node.id);
        const Unit contribution_left = checked(static_cast<double>(location_x)
            + child_left, node.id);
        const Unit contribution_top = checked(static_cast<double>(location_y)
            + child_top, node.id);
        const Unit contribution_right = checked(static_cast<double>(location_x)
            + child_right, node.id);
        const Unit contribution_bottom = checked(static_cast<double>(location_y)
            + child_bottom, node.id);
        if (scroll_container(parent_style)
            && (contribution_right <= 0 || contribution_bottom <= 0)) continue;
        const auto parent_index = index.at(layout.parent->value);
        Rect& destination = node.style.position == Position::Absolute
            ? absolute[parent_index] : inflow[parent_index];
        unite(destination, contribution_left, contribution_top,
            contribution_right, contribution_bottom, parent.node);
    }
}
} // namespace axisly::css::detail
