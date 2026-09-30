#include "grid_internal.hpp"

#include <algorithm>
#include <utility>

namespace axisly::css::detail {
namespace {

std::optional<std::size_t> track_line_index(std::optional<std::int32_t> line,
    const TrackAxis& axis) {
    if (!line || *line < axis.origin || *line > axis.origin
        + static_cast<std::int32_t>(axis.tracks.size())) return std::nullopt;
    return static_cast<std::size_t>(*line - axis.origin);
}

Unit rtl_track_edge(const TrackAxis& axis, std::size_t index,
    Unit origin, Unit inner_width, bool right_edge, NodeId id) {
    if (axis.tracks[index].collapsed) {
        // Collapsed tracks receive no alignment offset. In physical RTL order
        // they remain at the edge of the preceding visible track, or at the
        // content origin when no such track exists.
        for (std::size_t next = index + 1; next < axis.tracks.size(); ++next) {
            if (!axis.tracks[next].collapsed) {
                return checked(static_cast<double>(origin) + inner_width - axis.offsets[next], id);
            }
        }
        if (std::any_of(axis.tracks.begin(), axis.tracks.end(),
            [](const TrackState& track) { return !track.collapsed; })) return origin;
    }
    return checked(static_cast<double>(origin) + inner_width - axis.offsets[index]
        - (right_edge ? 0 : axis.tracks[index].size), id);
}

std::pair<Unit, Unit> axis_area(const AbsoluteGridPlacement& placement,
    const TrackAxis& axis, Unit origin, Unit inner_size,
    Unit padding_start, Unit padding_end, bool rtl, NodeId id) {
    const auto start = track_line_index(placement.start, axis);
    const auto end = track_line_index(placement.end, axis);
    const auto start_edge = [&](std::size_t index) {
        if (rtl) {
            if (index < axis.tracks.size()) {
                return rtl_track_edge(axis, index, origin, inner_size, true, id);
            }
            if (index) return rtl_track_edge(axis, index - 1, origin, inner_size, false, id);
            return checked(static_cast<double>(origin) + inner_size - axis.offsets.front(), id);
        }
        return checked(static_cast<double>(origin) + axis.offsets[index], id);
    };
    const auto end_edge = [&](std::size_t index) {
        if (rtl) {
            if (index) return rtl_track_edge(axis, index - 1, origin, inner_size, false, id);
            if (!axis.tracks.empty()) return rtl_track_edge(axis, 0, origin, inner_size, true, id);
            return checked(static_cast<double>(origin) + inner_size - axis.offsets.front(), id);
        }
        return checked(static_cast<double>(origin) + axis.offsets[index]
            - (index ? axis.gutter_after[index - 1] : 0), id);
    };
    return rtl
        ? std::pair{end ? end_edge(*end) : padding_start,
                    start ? start_edge(*start) : padding_end}
        : std::pair{start ? start_edge(*start) : padding_start,
                    end ? end_edge(*end) : padding_end};
}

} // namespace

Rect resolve_absolute_grid_area(const AbsoluteGridPlacement& rows,
    const AbsoluteGridPlacement& columns, const TrackAxis& row_tracks,
    const TrackAxis& column_tracks, Rect padding_box, Rect content_box,
    Direction direction, NodeId id) {
    const auto [left, right] = axis_area(columns, column_tracks,
        content_box.x, content_box.width, padding_box.x,
        checked(static_cast<double>(padding_box.x) + padding_box.width, id),
        direction == Direction::Rtl, id);
    const auto [top, bottom] = axis_area(rows, row_tracks,
        content_box.y, content_box.height, padding_box.y,
        checked(static_cast<double>(padding_box.y) + padding_box.height, id), false, id);
    return {left, top, std::max(Unit{0}, checked(static_cast<double>(right) - left, id)),
        std::max(Unit{0}, checked(static_cast<double>(bottom) - top, id))};
}

} // namespace axisly::css::detail
