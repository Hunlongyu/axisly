#include "item_alignment.hpp"

#include <algorithm>
#include <limits>

namespace axisly::detail {
namespace {

struct AxisPlacement {
    Unit offset{};
    Unit size{};
};

double anchor(HorizontalAlignment alignment) noexcept {
    switch (alignment) {
    case HorizontalAlignment::Center: return 0.5;
    case HorizontalAlignment::End: return 1.0;
    default: return 0.0;
    }
}

double anchor(VerticalAlignment alignment) noexcept {
    switch (alignment) {
    case VerticalAlignment::Center: return 0.5;
    case VerticalAlignment::End: return 1.0;
    default: return 0.0;
    }
}

AxisPlacement align_axis(const ResolvedTrack& track, Unit start_margin, Unit end_margin,
                         Unit preferred, double alignment_anchor, bool stretch) noexcept {
    // Consume the leading margin first, keeping a collapsed box inside the cell.
    const double leading = std::min(track.size, static_cast<double>(start_margin));
    const double inner_size = std::max(0.0, track.size - leading - end_margin);
    const double size = stretch ? inner_size : std::min(inner_size, static_cast<double>(preferred));
    const double offset = track.offset + leading + (inner_size - size) * alignment_anchor;

    // Valid resolved tracks bound the cell edge. A sum at Unit::max() can
    // overshoot by double roundoff; cap only the final coordinate conversion.
    const double finite_offset = std::min(offset, static_cast<double>(std::numeric_limits<Unit>::max()));
    return {static_cast<Unit>(finite_offset), static_cast<Unit>(size)};
}

} // namespace

Rect align_item(const GridItem& item, const ResolvedTrack& column, const ResolvedTrack& row, Size preferred) noexcept {
    const auto horizontal = align_axis(column, item.margin.left, item.margin.right, preferred.width,
                                       anchor(item.horizontal_alignment), item.horizontal_alignment == HorizontalAlignment::Stretch);
    const auto vertical = align_axis(row, item.margin.top, item.margin.bottom, preferred.height,
                                     anchor(item.vertical_alignment), item.vertical_alignment == VerticalAlignment::Stretch);
    return {horizontal.offset, vertical.offset, horizontal.size, vertical.size};
}

Unit resolved_item_width(const GridItem& item, const ResolvedTrack& column, Unit preferred_width) noexcept {
    return align_axis(column, item.margin.left, item.margin.right, preferred_width,
                      anchor(item.horizontal_alignment), item.horizontal_alignment == HorizontalAlignment::Stretch).size;
}

} // namespace axisly::detail
