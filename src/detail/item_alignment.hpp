#pragma once

#include <gridly/grid.hpp>

#include "track_sizing.hpp"

namespace gridly::detail {

// Requires validated item geometry and resolved finite tracks.
// Oversized margins collapse the inner box; preferred sizes are clipped to it.
[[nodiscard]] Rect align_item(
    const GridItem& item, const ResolvedTrack& column, const ResolvedTrack& row, Size preferred) noexcept;

// Uses the same alignment calculation and Unit rounding as the final rectangle.
[[nodiscard]] Unit resolved_item_width(
    const GridItem& item, const ResolvedTrack& column, Unit preferred_width) noexcept;

} // namespace gridly::detail
