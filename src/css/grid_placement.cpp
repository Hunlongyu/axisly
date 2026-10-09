#include "grid_internal.hpp"
#include "grid_metrics.hpp"

#include <algorithm>
#include <cstdlib>
#include <unordered_map>
#include <utility>

namespace axisly::css::detail {
namespace {

std::int32_t limited_grid_line(std::int32_t value) {
    return std::clamp(value, grid_minimum_line, grid_maximum_line);
}

std::int32_t limited_grid_span(std::uint16_t value) {
    return std::clamp<std::int32_t>(value, 1, grid_track_limit);
}

bool has_name(const std::vector<std::string>& names, const std::string& name) {
    return std::find(names.begin(), names.end(), name) != names.end();
}

std::int32_t named_index(const ExpandedTemplate& axis, const GridLine& line, bool start_side) {
    auto name = line.name();
    const bool exact_exists = std::any_of(axis.line_names.begin(), axis.line_names.end(),
        [&](const auto& names) { return has_name(names, name); });
    if (!exact_exists) name += start_side ? "-start" : "-end";
    const auto occurrence = line.index_value();
    std::int32_t found = 0;
    if (occurrence > 0) {
        for (std::size_t i = 0; i < axis.line_names.size(); ++i) {
            if (has_name(axis.line_names[i], name) && ++found == occurrence) {
                return static_cast<std::int32_t>(i);
            }
        }
        return static_cast<std::int32_t>(axis.tracks.size()) + occurrence - found;
    }
    for (std::size_t i = axis.line_names.size(); i-- > 0;) {
        if (has_name(axis.line_names[i], name) && --found == occurrence) {
            return static_cast<std::int32_t>(i);
        }
    }
    return -occurrence + found;
}

std::int32_t named_span(const ExpandedTemplate& axis, std::int32_t anchor,
                        const GridLine& anchor_style, const GridLine& span, bool forward) {
    auto name = span.name();
    const bool exact_exists = std::any_of(axis.line_names.begin(), axis.line_names.end(),
        [&](const auto& names) { return has_name(names, name); });
    if (!exact_exists) name += forward ? "-end" : "-start";
    // Taffy 0.14 filters candidates using the original one-based negative
    // endpoint. Its normalized filter boundary is one line before the resolved
    // origin-zero endpoint for negative numeric anchors.
    const auto filter_boundary = anchor_style.kind() == GridLineKind::Index
        && anchor_style.index_value() < 0 ? anchor - 1 : anchor;
    std::int32_t found = 0;
    for (std::size_t i = 0; i < axis.line_names.size(); ++i) {
        const auto index = static_cast<std::int32_t>(i);
        if ((forward ? index > filter_boundary : index < filter_boundary)
            && has_name(axis.line_names[i], name)
            && ++found == span.span_count()) return index;
    }
    return static_cast<std::int32_t>(axis.tracks.size())
        + span.span_count() - found;
}

} // namespace

AxisPlacement resolve_grid_placement(const GridPlacement& value, const ExpandedTemplate& axis) {
    const auto explicit_count = static_cast<std::int32_t>(axis.tracks.size());
    const auto numeric_line = [&](const GridLine& line, bool start_side) -> std::optional<std::int32_t> {
        if (line.kind() == GridLineKind::Auto || line.kind() == GridLineKind::Span) return std::nullopt;
        if (line.kind() == GridLineKind::NamedSpan) return std::nullopt;
        if (line.kind() == GridLineKind::NamedIndex) {
            return limited_grid_line(named_index(axis, line, start_side));
        }
        const auto index = line.index_value();
        return limited_grid_line(index > 0 ? static_cast<std::int32_t>(index - 1)
            : static_cast<std::int32_t>(explicit_count + 1 + index));
    };
    const auto start = numeric_line(value.start, true);
    const auto end = numeric_line(value.end, false);
    const auto span_size = [](const GridLine& line) {
        return line.kind() == GridLineKind::NamedSpan ? std::int32_t{1}
            : limited_grid_span(line.span_count());
    };
    std::int32_t span = 1;
    if (value.start.kind() == GridLineKind::Span
        || value.start.kind() == GridLineKind::NamedSpan) {
        span = span_size(value.start);
    } else if (value.end.kind() == GridLineKind::Span
        || value.end.kind() == GridLineKind::NamedSpan) {
        span = span_size(value.end);
    }
    if (start && end) return {std::min(*start, *end),
        std::max<std::int32_t>(1, std::abs(*end - *start))};
    if (start && value.end.kind() == GridLineKind::NamedSpan) {
        const auto target = limited_grid_line(
            named_span(axis, *start, value.start, value.end, true));
        return {std::min(*start, target),
            std::max<std::int32_t>(1, std::abs(target - *start))};
    }
    if (end && value.start.kind() == GridLineKind::NamedSpan) {
        const auto target = limited_grid_line(
            named_span(axis, *end, value.end, value.start, false));
        return {std::min(target, *end),
            std::max<std::int32_t>(1, std::abs(*end - target))};
    }
    if (start) return {*start, span};
    if (end) return {*end - span, span};
    return {std::nullopt, span};
}

AbsoluteGridPlacement resolve_absolute_grid_placement(const GridPlacement& value,
    const ExpandedTemplate& axis) {
    const auto resolved = resolve_grid_placement(value, axis);
    if (!resolved.start) return {};
    const auto definite = [](const GridLine& line) {
        return line.kind() == GridLineKind::Index || line.kind() == GridLineKind::NamedIndex;
    };
    const bool start_definite = definite(value.start);
    const bool end_definite = definite(value.end);
    if (start_definite && value.end.kind() == GridLineKind::Auto) {
        return {resolved.start, std::nullopt};
    }
    if (end_definite && value.start.kind() == GridLineKind::Auto) {
        return {std::nullopt, *resolved.start + resolved.span};
    }
    return {resolved.start, *resolved.start + resolved.span};
}

AxisEstimate estimate_grid_axis(const GridPlacement& value, std::int32_t explicit_count) {
    // Taffy's initial grid extent is computed before named lines are resolved.
    // A named endpoint therefore contributes no definite bound at this stage.
    const auto line_index = [explicit_count](const GridLine& line)
        -> std::optional<std::int32_t> {
        if (line.kind() != GridLineKind::Index || line.index_value() == 0) {
            return std::nullopt;
        }
        const auto index = line.index_value();
        return limited_grid_line(index > 0 ? static_cast<std::int32_t>(index - 1)
            : explicit_count + 1 + index);
    };
    const auto start = line_index(value.start);
    const auto end = line_index(value.end);
    const auto span_count = [](const GridLine& line) {
        return line.kind() == GridLineKind::Span
            ? limited_grid_span(line.span_count()) : std::int32_t{1};
    };
    if (start && end) {
        return {*start == *end ? *start : std::min(*start, *end),
            std::min(grid_maximum_line,
                *start == *end ? *start + 1 : std::max(*start, *end)), 1};
    }
    if (start) return {*start,
        std::min(grid_maximum_line, *start + span_count(value.end)), 1};
    if (end) {
        const auto minimum = value.start.kind() == GridLineKind::Span
            ? *end - span_count(value.start) : *end;
        return {std::max(grid_minimum_line, minimum), *end, 1};
    }
    const auto indefinite_span = value.start.kind() == GridLineKind::Span
        ? span_count(value.start) : span_count(value.end);
    return {0, 0, indefinite_span};
}

namespace {

struct OccupiedArea {
    std::int32_t primary_start{}, primary_end{};
    std::int32_t secondary_start{}, secondary_end{};
    bool auto_placed{};
};

class GridOccupancy {
public:
    GridOccupancy(std::size_t count, std::pmr::memory_resource* memory)
        : indexed_(count >= 64), row_memory_(memory), rows_(&row_memory_),
          large_(&row_memory_), areas_(memory) {
        areas_.reserve(count);
    }
    void add(std::int32_t primary, std::int32_t primary_span,
             std::int32_t secondary, std::int32_t secondary_span, bool auto_placed) {
        areas_.push_back({primary, primary + primary_span,
            secondary, secondary + secondary_span, auto_placed});
        if (!indexed_) return;
        // Very tall rectangles stay compact. Large queries use the rectangle
        // list too, bounding index duplication and sparse-coordinate work.
        if (secondary_span > indexed_span_limit) {
            large_.push_back(areas_.back());
            return;
        }
        for (auto row = secondary; row < secondary + secondary_span; ++row) {
            auto& occupied = rows_.try_emplace(row, &row_memory_).first->second;
            if (auto_placed) occupied.last_auto = std::max(
                occupied.last_auto.value_or(primary + primary_span - 1), primary + primary_span - 1);
            Interval merged{primary, primary + primary_span};
            auto first = std::lower_bound(occupied.intervals.begin(), occupied.intervals.end(), primary,
                [](const Interval& interval, std::int32_t start) { return interval.end < start; });
            auto last = first;
            while (last != occupied.intervals.end() && last->start <= merged.end) {
                merged.start = std::min(merged.start, last->start);
                merged.end = std::max(merged.end, last->end);
                ++last;
            }
            if (first == last) occupied.intervals.insert(first, merged);
            else {
                *first = merged;
                occupied.intervals.erase(first + 1, last);
            }
        }
    }

    [[nodiscard]] std::optional<std::int32_t> collision_jump(
        std::int32_t primary, std::int32_t primary_span,
        std::int32_t secondary, std::int32_t secondary_span,
        bool along_primary) const {
        AXISLY_GRID_COUNT(placement_probes, 1);
        std::optional<std::int32_t> next;
        const bool indexed_query = indexed_ && secondary_span <= indexed_span_limit;
        if (indexed_query) {
            for (auto row = secondary; row < secondary + secondary_span; ++row) {
                const auto found = rows_.find(row);
                if (found == rows_.end()) continue;
                const auto& intervals = found->second.intervals;
                auto interval = std::lower_bound(intervals.begin(), intervals.end(), primary,
                    [](const Interval& value, std::int32_t start) { return value.end <= start; });
                for (; interval != intervals.end() && interval->start < primary + primary_span; ++interval) {
                    AXISLY_GRID_COUNT(occupied_visits, 1);
                    const auto end = along_primary ? interval->end : row + 1;
                    next = std::max(next.value_or(end), end);
                }
            }
        }
        for (const auto& area : indexed_query ? large_ : areas_) {
            AXISLY_GRID_COUNT(occupied_visits, 1);
            if (primary >= area.primary_end || primary + primary_span <= area.primary_start
                || secondary >= area.secondary_end
                || secondary + secondary_span <= area.secondary_start) continue;
            const auto end = along_primary ? area.primary_end : area.secondary_end;
            next = std::max(next.value_or(end), end);
        }
        return next;
    }

    [[nodiscard]] std::optional<std::int32_t> last_auto_on_secondary(
        std::int32_t secondary) const {
        std::optional<std::int32_t> last;
        if (indexed_) {
            const auto found = rows_.find(secondary);
            if (found != rows_.end()) last = found->second.last_auto;
        }
        for (const auto& area : indexed_ ? large_ : areas_) {
            AXISLY_GRID_COUNT(occupied_visits, 1);
            if (!area.auto_placed || secondary < area.secondary_start
                || secondary >= area.secondary_end) continue;
            last = std::max(last.value_or(area.primary_end - 1), area.primary_end - 1);
        }
        return last;
    }

private:
    struct Interval { std::int32_t start, end; };
    struct Row {
        explicit Row(std::pmr::memory_resource* memory) : intervals(memory) {}
        std::pmr::vector<Interval> intervals;
        std::optional<std::int32_t> last_auto;
    };
    static constexpr std::int32_t indexed_span_limit = 64;
    bool indexed_{};
    std::pmr::monotonic_buffer_resource row_memory_;
    std::pmr::unordered_map<std::int32_t, Row> rows_;
    std::pmr::vector<OccupiedArea> large_, areas_;
};

} // namespace

GridExtent place_grid_items(std::span<Placement> placements, std::int32_t explicit_rows,
                 std::int32_t explicit_columns, GridAutoFlow flow, NodeId id,
                 std::pmr::memory_resource* memory) {
    const bool by_row = flow == GridAutoFlow::Row || flow == GridAutoFlow::RowDense;
    const bool dense = flow == GridAutoFlow::RowDense || flow == GridAutoFlow::ColumnDense;
    const auto primary = [by_row](const Placement& item) -> const AxisPlacement& {
        return by_row ? item.column : item.row;
    };
    const auto secondary = [by_row](const Placement& item) -> const AxisPlacement& {
        return by_row ? item.row : item.column;
    };
    GridExtent extent{0, explicit_rows, 0, explicit_columns};
    std::int32_t largest_row_span = 0;
    std::int32_t largest_column_span = 0;
    for (const auto& item : placements) {
        if (item.hidden) continue;
        extent.row_start = std::min(extent.row_start, item.row_estimate.minimum_line);
        extent.row_end = std::max(extent.row_end, item.row_estimate.maximum_line);
        extent.column_start = std::min(extent.column_start, item.column_estimate.minimum_line);
        extent.column_end = std::max(extent.column_end, item.column_estimate.maximum_line);
        largest_row_span = std::max(largest_row_span, item.row_estimate.indefinite_span);
        largest_column_span = std::max(largest_column_span,
            item.column_estimate.indefinite_span);
    }
    extent.row_end = std::max(extent.row_end, extent.row_start + largest_row_span);
    extent.column_end = std::max(extent.column_end,
        extent.column_start + largest_column_span);
    auto& primary_start = by_row ? extent.column_start : extent.row_start;
    auto& primary_end = by_row ? extent.column_end : extent.row_end;
    auto& secondary_start = by_row ? extent.row_start : extent.column_start;
    auto& secondary_end = by_row ? extent.row_end : extent.column_end;
    const bool automatic = std::any_of(placements.begin(), placements.end(), [](const Placement& item) {
        return !item.hidden && (!item.row.start || !item.column.start);
    });
    GridOccupancy occupied(automatic ? placements.size() : 0, memory);
    const auto record = [&](Placement& item, std::int32_t p, std::int32_t s,
                            bool auto_placed) {
        const auto clamp_range = [](std::int32_t start, std::int32_t span) {
            const auto clipped_start = std::clamp(start, grid_minimum_line,
                grid_maximum_line - 1);
            const auto clipped_end = std::clamp(start + span,
                clipped_start + 1, grid_maximum_line);
            return std::pair{clipped_start, clipped_end};
        };
        const auto [p_start, p_end] = clamp_range(p, primary(item).span);
        const auto [s_start, s_end] = clamp_range(s, secondary(item).span);
        if (by_row) {
            item.column_start = p_start;
            item.column_end = p_end;
            item.row_start = s_start;
            item.row_end = s_end;
        } else {
            item.row_start = p_start;
            item.row_end = p_end;
            item.column_start = s_start;
            item.column_end = s_end;
        }
        if (automatic) occupied.add(p_start, p_end - p_start, s_start, s_end - s_start, auto_placed);
        primary_start = std::min(primary_start, p_start);
        primary_end = std::max(primary_end, p_end);
        secondary_start = std::min(secondary_start, s_start);
        secondary_end = std::max(secondary_end, s_end);
    };

    // Definite positions reserve their cells before either auto-placement phase.
    for (auto& item : placements) {
        if (!item.hidden && primary(item).start && secondary(item).start) {
            record(item, *primary(item).start, *secondary(item).start, false);
        }
    }

    // A definite row (column flow: column) is placed before fully automatic items.
    for (auto& item : placements) {
        if (item.hidden || primary(item).start || !secondary(item).start) continue;
        const auto s = *secondary(item).start;
        auto p = dense ? primary_start
            : occupied.last_auto_on_secondary(s).value_or(primary_start);
        bool found = false;
        for (std::size_t probe = 0; probe < 100'000; ++probe) {
            const auto collision = occupied.collision_jump(p, primary(item).span,
                s, secondary(item).span, true);
            if (!collision) { found = true; break; }
            p = *collision;
        }
        if (!found) throw LayoutFailure(LayoutErrorCode::LimitExceeded, id,
            "Grid secondary-axis placement search limit exceeded");
        record(item, p, s, true);
    }

    // Remaining items share one cursor; dense flow restarts at the implicit grid start.
    const auto grid_start_primary = primary_start;
    const auto grid_start_secondary = secondary_start;
    std::int32_t cursor_primary = grid_start_primary;
    std::int32_t cursor_secondary = grid_start_secondary;
    for (auto& item : placements) {
        if (item.hidden || secondary(item).start) continue;
        std::int32_t p = cursor_primary;
        std::int32_t s = cursor_secondary;
        bool found = false;
        if (primary(item).start) {
            p = *primary(item).start;
            if (dense) s = secondary_start;
            else if (p < cursor_primary) ++s;
            for (std::size_t probe = 0; probe < 100'000; ++probe) {
                const auto collision = occupied.collision_jump(p, primary(item).span,
                    s, secondary(item).span, false);
                if (!collision) { found = true; break; }
                s = *collision;
            }
        } else {
            for (std::size_t probe = 0; probe < 100'000; ++probe) {
                if (p + primary(item).span > primary_end) {
                    if (p == primary_start) { found = true; break; }
                    p = primary_start;
                    ++s;
                    continue;
                }
                const auto collision = occupied.collision_jump(p, primary(item).span,
                    s, secondary(item).span, true);
                if (!collision) { found = true; break; }
                p = *collision;
            }
        }
        if (!found) throw LayoutFailure(LayoutErrorCode::LimitExceeded, id,
            "Grid automatic placement search limit exceeded");
        record(item, p, s, true);
        cursor_primary = dense ? grid_start_primary : p + primary(item).span;
        cursor_secondary = dense ? grid_start_secondary : s;
    }
    return extent;
}

} // namespace axisly::css::detail
