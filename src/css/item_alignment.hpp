#pragma once

#include <axisly/css_style.hpp>

namespace axisly::css::detail {

enum class ItemEdge { Start, Center, End };

// Resolve CSS alignment keywords to the algorithm's logical axis. The
// container's writing start, the item's own writing start, and flex-start
// may be three different physical edges.
[[nodiscard]] inline ItemEdge item_edge(ItemPosition position, bool horizontal,
    Direction container_direction, Direction item_direction,
    bool flex_start_at_physical_end, bool logical_axis_reversed) noexcept {
    if (position == ItemPosition::Center) return ItemEdge::Center;
    bool physical_end = false;
    switch (position) {
    case ItemPosition::End:
        physical_end = !(horizontal && container_direction == Direction::Rtl);
        break;
    case ItemPosition::SelfStart:
        physical_end = horizontal && item_direction == Direction::Rtl;
        break;
    case ItemPosition::SelfEnd:
        physical_end = !(horizontal && item_direction == Direction::Rtl);
        break;
    case ItemPosition::FlexStart:
        physical_end = flex_start_at_physical_end;
        break;
    case ItemPosition::FlexEnd:
        physical_end = !flex_start_at_physical_end;
        break;
    case ItemPosition::Start:
    case ItemPosition::Baseline:
    case ItemPosition::Stretch:
    case ItemPosition::Center:
        physical_end = horizontal && container_direction == Direction::Rtl;
        break;
    }
    return physical_end != logical_axis_reversed ? ItemEdge::End : ItemEdge::Start;
}

} // namespace axisly::css::detail
