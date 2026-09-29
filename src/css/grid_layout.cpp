#include "layout_engine.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace axisly::css::detail {
namespace {
struct AxisPlacement {
    std::optional<std::int32_t> start;
    std::int32_t span{1};
};
struct Placement {
    AxisPlacement row, column;
    std::int32_t row_start{}, row_end{}, column_start{}, column_end{};
    bool hidden{};
};
struct ItemContribution {
    NodeId id;
    std::size_t source{};
    Placement placement;
    Size minimum{}, preferred{};
    Insets margin{};
};
struct TrackState {
    TrackSizing style;
    Unit size{}, preferred{};
    std::optional<Unit> maximum;
    Unit flex{};
    bool intrinsic_min{}, intrinsic_max{}, auto_max{};
    bool collapsed{};
};
struct TrackAxis {
    std::int32_t origin{};
    std::vector<TrackState> tracks;
    std::vector<Unit> offsets;
    std::vector<Unit> gutter_after;
    Unit gap{};
    [[nodiscard]] Unit line(std::int32_t index) const {
        const auto slot = static_cast<std::size_t>(index - origin);
        return offsets.at(slot);
    }
    [[nodiscard]] Unit span(std::int32_t start, std::int32_t end) const {
        const auto begin = static_cast<std::size_t>(start - origin);
        const auto finish = static_cast<std::size_t>(end - origin);
        return offsets.at(finish) - offsets.at(begin)
            - (end > start ? gutter_after.at(finish - 1) : 0);
    }
};

struct ExpandedTemplate {
    std::vector<TrackSizing> tracks;
    std::vector<std::vector<std::string>> line_names;
    std::vector<bool> auto_fit;
};

Unit definite_track_size(const TrackSizing& track, Unit basis, NodeId id) {
    const auto minimum = track.minimum.resolve_numeric(basis);
    const auto maximum = track.maximum.resolve_numeric(basis);
    if (!minimum && !maximum) {
        throw LayoutFailure(LayoutErrorCode::InvalidStyle, id,
            "Grid auto-repeat track lacks a fixed sizing component");
    }
    return std::max(Unit{0}, std::max(minimum.value_or(0), maximum.value_or(0)));
}

std::uint16_t auto_repeat_count(const std::vector<TemplateTrack>& components,
                                std::optional<Unit> available, Unit gap,
                                bool minimum_strategy, NodeId id) {
    const GridRepeat* automatic = nullptr;
    std::uint32_t fixed_count = 0;
    std::uint32_t insertion = 0;
    for (const auto& component : components) {
        if (std::holds_alternative<TrackSizing>(component)) {
            ++fixed_count;
            if (!automatic) ++insertion;
            continue;
        }
        const auto& repeat = std::get<GridRepeat>(component);
        if (repeat.kind == RepeatKind::Count) {
            const auto count = static_cast<std::uint32_t>(repeat.count)
                * static_cast<std::uint32_t>(repeat.tracks.size());
            fixed_count += count;
            if (!automatic) insertion += count;
        } else automatic = &repeat;
    }
    if (!automatic) return 0;
    const auto per_repeat = static_cast<std::uint32_t>(automatic->tracks.size());
    if (insertion >= 10'000) return 0;
    const auto maximum_repeats = (10'000u - insertion + per_repeat - 1) / per_repeat;
    if (!available) return 1;
    double fixed_space = 0;
    double repeated_space = 0;
    for (const auto& component : components) {
        if (const auto* single = std::get_if<TrackSizing>(&component)) {
            fixed_space += definite_track_size(*single, *available, id);
        } else {
            const auto& repeat = std::get<GridRepeat>(component);
            double group = 0;
            for (const auto& track : repeat.tracks) {
                group += definite_track_size(track, *available, id);
            }
            if (repeat.kind == RepeatKind::Count) fixed_space += repeat.count * group;
            else repeated_space = group;
        }
    }
    const double first = fixed_space + repeated_space
        + std::max(0u, fixed_count + per_repeat - 1) * static_cast<double>(gap);
    if (first > *available) return 1;
    const double step = repeated_space + per_repeat * static_cast<double>(gap);
    if (step <= 0) return static_cast<std::uint16_t>(maximum_repeats);
    const double extra = (static_cast<double>(*available) - first) / step;
    const double rounded = minimum_strategy ? std::ceil(extra) : std::floor(extra);
    return static_cast<std::uint16_t>(std::clamp(rounded + 1.0, 1.0,
        static_cast<double>(maximum_repeats)));
}

ExpandedTemplate expand(const std::vector<TemplateTrack>& template_tracks,
    const std::vector<std::vector<std::string>>& outer_names,
    std::optional<Unit> available, Unit gap, bool minimum_strategy, NodeId id) {
    ExpandedTemplate result;
    result.line_names.emplace_back();
    const auto automatic_count = auto_repeat_count(template_tracks, available,
        gap, minimum_strategy, id);
    for (std::size_t component_index = 0; component_index < template_tracks.size(); ++component_index) {
        const auto& component = template_tracks[component_index];
        if (!outer_names.empty()) {
            auto& boundary = result.line_names.back();
            boundary.insert(boundary.end(), outer_names[component_index].begin(),
                outer_names[component_index].end());
        }
        if (const auto* single = std::get_if<TrackSizing>(&component)) {
            result.tracks.push_back(*single);
            result.auto_fit.push_back(false);
            result.line_names.emplace_back();
        } else {
            const auto& repetition = std::get<GridRepeat>(component);
            const auto count = repetition.kind == RepeatKind::Count
                ? repetition.count : automatic_count;
            for (std::uint16_t repeat = 0; repeat < count; ++repeat) {
                if (!repetition.line_names.empty()) {
                    auto& boundary = result.line_names.back();
                    boundary.insert(boundary.end(), repetition.line_names.front().begin(),
                        repetition.line_names.front().end());
                }
                for (std::size_t i = 0; i < repetition.tracks.size(); ++i) {
                    result.tracks.push_back(repetition.tracks[i]);
                    result.auto_fit.push_back(repetition.kind == RepeatKind::AutoFit);
                    result.line_names.push_back(repetition.line_names.empty()
                        ? std::vector<std::string>{} : repetition.line_names[i + 1]);
                    if (result.tracks.size() > 10'000) {
                        throw LayoutFailure(LayoutErrorCode::LimitExceeded, id, "Grid track limit exceeded");
                    }
                }
            }
        }
    }
    if (!outer_names.empty()) {
        auto& boundary = result.line_names.back();
        boundary.insert(boundary.end(), outer_names.back().begin(), outer_names.back().end());
    }
    return result;
}

void apply_areas(const GridStyle& style, ExpandedTemplate& rows, ExpandedTemplate& columns) {
    if (!style.template_areas) return;
    const auto& cells = *style.template_areas;
    const auto extend = [](ExpandedTemplate& axis, std::size_t minimum) {
        while (axis.tracks.size() < minimum) {
            axis.tracks.push_back(TrackSizing::automatic());
            axis.auto_fit.push_back(false);
            axis.line_names.emplace_back();
        }
    };
    extend(rows, cells.size());
    extend(columns, cells.front().size());
    std::unordered_map<std::string, std::array<std::size_t, 4>> bounds;
    for (std::size_t row = 0; row < cells.size(); ++row) {
        for (std::size_t column = 0; column < cells[row].size(); ++column) {
            const auto& name = cells[row][column];
            if (name == ".") continue;
            auto [it, inserted] = bounds.try_emplace(name,
                std::array<std::size_t, 4>{row, row + 1, column, column + 1});
            if (!inserted) {
                it->second[0] = std::min(it->second[0], row);
                it->second[1] = std::max(it->second[1], row + 1);
                it->second[2] = std::min(it->second[2], column);
                it->second[3] = std::max(it->second[3], column + 1);
            }
        }
    }
    for (const auto& [name, box] : bounds) {
        rows.line_names[box[0]].push_back(name + "-start");
        rows.line_names[box[1]].push_back(name + "-end");
        columns.line_names[box[2]].push_back(name + "-start");
        columns.line_names[box[3]].push_back(name + "-end");
    }
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
    return occurrence - found;
}

std::int32_t named_span(const ExpandedTemplate& axis, std::int32_t anchor,
                        const GridLine& span, bool forward) {
    std::int32_t remaining = span.span_count();
    auto index = anchor;
    while (remaining && index >= 0 && index <= static_cast<std::int32_t>(axis.tracks.size())) {
        index += forward ? 1 : -1;
        if (index >= 0 && index < static_cast<std::int32_t>(axis.line_names.size())
            && has_name(axis.line_names[static_cast<std::size_t>(index)], span.name())) {
            --remaining;
        }
    }
    return index + (forward ? remaining - 1 : 1 - remaining);
}

AxisPlacement axis_placement(const GridPlacement& value, const ExpandedTemplate& axis) {
    const auto explicit_count = static_cast<std::int32_t>(axis.tracks.size());
    const auto numeric_line = [&](const GridLine& line, bool start_side) -> std::optional<std::int32_t> {
        if (line.kind() == GridLineKind::Auto || line.kind() == GridLineKind::Span) return std::nullopt;
        if (line.kind() == GridLineKind::NamedSpan) return std::nullopt;
        if (line.kind() == GridLineKind::NamedIndex) return named_index(axis, line, start_side);
        const auto index = line.index_value();
        return index > 0 ? static_cast<std::int32_t>(index - 1)
            : static_cast<std::int32_t>(explicit_count + 1 + index);
    };
    const auto start = numeric_line(value.start, true);
    const auto end = numeric_line(value.end, false);
    std::int32_t span = 1;
    if (value.start.kind() == GridLineKind::Span) span = value.start.span_count();
    if (value.end.kind() == GridLineKind::Span) span = value.end.span_count();
    if (value.start.kind() == GridLineKind::NamedSpan) span = value.start.span_count();
    if (value.end.kind() == GridLineKind::NamedSpan) span = value.end.span_count();
    if (start && end) return {*start, std::max<std::int32_t>(1, *end - *start)};
    if (start && value.end.kind() == GridLineKind::NamedSpan) {
        const auto target = named_span(axis, *start, value.end, true);
        return {*start, std::max<std::int32_t>(1, target - *start)};
    }
    if (end && value.start.kind() == GridLineKind::NamedSpan) {
        const auto target = named_span(axis, *end, value.start, false);
        return {target, std::max<std::int32_t>(1, *end - target)};
    }
    if (start) return {*start, span};
    if (end) return {*end - span, span};
    return {std::nullopt, span};
}

std::uint64_t cell_key(std::int32_t row, std::int32_t column) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(row)) << 32)
        | static_cast<std::uint32_t>(column);
}

bool vacant(const std::unordered_set<std::uint64_t>& occupied,
            std::int32_t row, std::int32_t column,
            std::int32_t row_span, std::int32_t column_span) {
    for (std::int32_t r = row; r < row + row_span; ++r) {
        for (std::int32_t c = column; c < column + column_span; ++c) {
            if (occupied.contains(cell_key(r, c))) return false;
        }
    }
    return true;
}

void occupy(std::unordered_set<std::uint64_t>& occupied, const Placement& item) {
    for (auto row = item.row_start; row < item.row_end; ++row) {
        for (auto column = item.column_start; column < item.column_end; ++column) {
            occupied.insert(cell_key(row, column));
        }
    }
}

void place(std::vector<Placement>& placements, std::int32_t explicit_rows,
           std::int32_t explicit_columns, GridAutoFlow flow, NodeId id) {
    std::unordered_set<std::uint64_t> occupied;
    auto columns = std::max<std::int32_t>(1, explicit_columns);
    auto rows = std::max<std::int32_t>(1, explicit_rows);
    for (auto& item : placements) {
        if (item.hidden) continue;
        if (item.column.start) columns = std::max(columns, *item.column.start + item.column.span);
        if (item.row.start) rows = std::max(rows, *item.row.start + item.row.span);
    }
    for (auto& item : placements) {
        if (item.hidden || !item.row.start || !item.column.start) continue;
        item.row_start = *item.row.start;
        item.column_start = *item.column.start;
        item.row_end = item.row_start + item.row.span;
        item.column_end = item.column_start + item.column.span;
        occupy(occupied, item);
    }
    const bool by_row = flow == GridAutoFlow::Row || flow == GridAutoFlow::RowDense;
    const bool dense = flow == GridAutoFlow::RowDense || flow == GridAutoFlow::ColumnDense;
    std::int32_t cursor_row = 0, cursor_column = 0;
    for (auto& item : placements) {
        if (item.hidden || (item.row.start && item.column.start)) continue;
        if (by_row) columns = std::max(columns, item.column.span);
        else rows = std::max(rows, item.row.span);
        bool found = false;
        std::int32_t row = dense ? 0 : cursor_row;
        std::int32_t column = dense ? 0 : cursor_column;
        for (std::size_t probe = 0; probe < 100'000; ++probe) {
            if (item.row.start) row = *item.row.start;
            if (item.column.start) column = *item.column.start;
            if ((item.row.start || !by_row || column + item.column.span <= columns)
                && (item.column.start || by_row || row + item.row.span <= rows)
                && vacant(occupied, row, column, item.row.span, item.column.span)) {
                found = true;
                break;
            }
            if (item.row.start) ++column;
            else if (item.column.start) ++row;
            else if (by_row) {
                ++column;
                if (column + item.column.span > columns) { column = 0; ++row; }
            } else {
                ++row;
                if (row + item.row.span > rows) { row = 0; ++column; }
            }
        }
        if (!found) throw LayoutFailure(LayoutErrorCode::LimitExceeded, id, "Grid placement search limit exceeded");
        item.row_start = row;
        item.column_start = column;
        item.row_end = row + item.row.span;
        item.column_end = column + item.column.span;
        occupy(occupied, item);
        if (!dense) {
            cursor_row = row;
            cursor_column = column;
            if (by_row) {
                cursor_column = item.column_end;
                if (cursor_column >= columns) { cursor_column = 0; ++cursor_row; }
            } else {
                cursor_row = item.row_end;
                if (cursor_row >= rows) { cursor_row = 0; ++cursor_column; }
            }
        }
    }
}

TrackAxis make_axis(const std::vector<TrackSizing>& explicit_tracks,
                    const std::vector<TrackSizing>& auto_tracks,
                    std::int32_t minimum_line, std::int32_t maximum_line,
                    std::optional<Unit> available, Unit gap, NodeId id) {
    TrackAxis axis;
    axis.origin = std::min<std::int32_t>(0, minimum_line);
    axis.gap = gap;
    const auto finish = std::max<std::int32_t>(static_cast<std::int32_t>(explicit_tracks.size()), maximum_line);
    if (finish - axis.origin > 10'000) {
        throw LayoutFailure(LayoutErrorCode::LimitExceeded, id, "Grid track limit exceeded");
    }
    for (auto index = axis.origin; index < finish; ++index) {
        TrackSizing style;
        if (index >= 0 && index < static_cast<std::int32_t>(explicit_tracks.size())) {
            style = explicit_tracks[static_cast<std::size_t>(index)];
        } else if (!auto_tracks.empty()) {
            const auto sequence = index < 0 ? static_cast<std::size_t>(-index - 1)
                : static_cast<std::size_t>(index - explicit_tracks.size());
            style = auto_tracks[sequence % auto_tracks.size()];
        }
        TrackState state;
        state.style = style;
        const auto minimum = style.minimum.resolve_numeric(available);
        auto maximum = style.maximum.resolve_numeric(available);
        if (!maximum) maximum = style.maximum.resolve_fit_content_limit(available);
        state.size = minimum ? std::max(*minimum, Unit{0}) : 0;
        state.preferred = state.size;
        if (maximum) state.maximum = std::max(*maximum, state.size);
        if (style.maximum.kind() == TrackBreadthKind::Fraction) {
            state.flex = style.maximum.fraction_factor();
        }
        const auto min_kind = style.minimum.kind();
        const auto max_kind = style.maximum.kind();
        state.intrinsic_min = min_kind == TrackBreadthKind::Auto
            || min_kind == TrackBreadthKind::MinContent || min_kind == TrackBreadthKind::MaxContent
            || (min_kind == TrackBreadthKind::Percent && !available);
        state.intrinsic_max = max_kind == TrackBreadthKind::Auto
            || max_kind == TrackBreadthKind::MinContent || max_kind == TrackBreadthKind::MaxContent
            || max_kind == TrackBreadthKind::FitContentLength
            || max_kind == TrackBreadthKind::FitContentPercent
            || (max_kind == TrackBreadthKind::Percent && !available);
        state.auto_max = max_kind == TrackBreadthKind::Auto;
        axis.tracks.push_back(std::move(state));
    }
    axis.gutter_after.assign(axis.tracks.size(), gap);
    if (!axis.gutter_after.empty()) axis.gutter_after.back() = 0;
    return axis;
}

void collapse_auto_fit(TrackAxis& axis, const ExpandedTemplate& definition,
                       const std::vector<Placement>& placements, bool columns) {
    for (std::size_t index = 0; index < definition.auto_fit.size(); ++index) {
        if (!definition.auto_fit[index]) continue;
        const auto occupied = std::any_of(placements.begin(), placements.end(),
            [&](const Placement& placement) {
                const auto start = columns ? placement.column_start : placement.row_start;
                const auto end = columns ? placement.column_end : placement.row_end;
                return !placement.hidden && start <= static_cast<std::int32_t>(index)
                    && end > static_cast<std::int32_t>(index);
            });
        if (occupied) continue;
        auto& track = axis.tracks[static_cast<std::size_t>(
            static_cast<std::int32_t>(index) - axis.origin)];
        track = {};
        track.collapsed = true;
    }
    bool later_visible = false;
    for (std::size_t index = axis.tracks.size(); index-- > 0;) {
        axis.gutter_after[index] = !axis.tracks[index].collapsed && later_visible
            ? axis.gap : Unit{0};
        later_visible |= !axis.tracks[index].collapsed;
    }
}

void size_axis(TrackAxis& axis, const std::vector<ItemContribution>& items,
               bool columns, std::optional<Unit> available, NodeId id) {
    const auto range = [&](const ItemContribution& item) {
        return columns
            ? std::pair{item.placement.column_start, item.placement.column_end}
            : std::pair{item.placement.row_start, item.placement.row_end};
    };
    const auto contribution = [&](const ItemContribution& item, bool preferred) {
        const Unit content = columns
            ? (preferred ? item.preferred.width : item.minimum.width)
            : (preferred ? item.preferred.height : item.minimum.height);
        const double margin = columns
            ? static_cast<double>(item.margin.left) + item.margin.right
            : static_cast<double>(item.margin.top) + item.margin.bottom;
        return checked(std::max(0.0, static_cast<double>(content) + margin), id);
    };
    for (const auto& item : items) {
        const auto [start, end] = range(item);
        const auto count = end - start;
        if (count != 1) continue;
        auto& track = axis.tracks[static_cast<std::size_t>(start - axis.origin)];
        const auto minimum = contribution(item, false);
        const auto preferred = contribution(item, true);
        if (track.intrinsic_min) {
            const auto kind = track.style.minimum.kind();
            track.size = std::max(track.size,
                kind == TrackBreadthKind::MaxContent ? preferred : minimum);
        }
        if (track.intrinsic_max) {
            const auto kind = track.style.maximum.kind();
            track.preferred = std::max(track.preferred,
                kind == TrackBreadthKind::MinContent ? minimum : preferred);
        }
        if (track.flex > 0) track.preferred = std::max(track.preferred, preferred);
    }
    for (const auto& item : items) {
        const auto [start, end] = range(item);
        if (end - start <= 1) continue;
        double occupied = 0;
        for (auto index = start; index + 1 < end; ++index) {
            occupied += axis.gutter_after[static_cast<std::size_t>(index - axis.origin)];
        }
        std::size_t growable = 0;
        for (auto index = start; index < end; ++index) {
            const auto& track = axis.tracks[static_cast<std::size_t>(index - axis.origin)];
            occupied += track.size;
            if (track.intrinsic_min) ++growable;
        }
        const double deficit = contribution(item, false) - occupied;
        if (deficit > 0 && growable) {
            const Unit share = checked(deficit / growable, id);
            for (auto index = start; index < end; ++index) {
                auto& track = axis.tracks[static_cast<std::size_t>(index - axis.origin)];
                if (track.intrinsic_min) track.size = checked(static_cast<double>(track.size) + share, id);
            }
        }
        double preferred_occupied = 0;
        for (auto index = start; index + 1 < end; ++index) {
            preferred_occupied += axis.gutter_after[static_cast<std::size_t>(index - axis.origin)];
        }
        std::size_t preferred_growable = 0;
        for (auto index = start; index < end; ++index) {
            const auto& track = axis.tracks[static_cast<std::size_t>(index - axis.origin)];
            preferred_occupied += std::max(track.size, track.preferred);
            if (track.intrinsic_max || track.flex > 0) ++preferred_growable;
        }
        const double preferred_deficit = contribution(item, true) - preferred_occupied;
        if (preferred_deficit > 0 && preferred_growable) {
            const Unit share = checked(preferred_deficit / preferred_growable, id);
            for (auto index = start; index < end; ++index) {
                auto& track = axis.tracks[static_cast<std::size_t>(index - axis.origin)];
                if (track.intrinsic_max || track.flex > 0) {
                    track.preferred = checked(static_cast<double>(track.preferred) + share, id);
                }
            }
        }
    }
    double occupied = 0;
    for (const auto gap : axis.gutter_after) occupied += gap;
    for (auto& track : axis.tracks) {
        track.preferred = std::max(track.preferred, track.size);
        if (track.maximum) {
            track.maximum = std::max(*track.maximum, track.size);
            track.preferred = std::min(track.preferred, *track.maximum);
        }
        occupied += track.size;
    }
    double free = available ? static_cast<double>(*available) - occupied : 0;
    for (auto& track : axis.tracks) {
        if (!track.intrinsic_max) continue;
        const Unit growth = std::max(Unit{0}, track.preferred - track.size);
        const Unit allowed = available ? checked(std::min<double>(growth, std::max(0.0, free)), id) : growth;
        track.size = checked(static_cast<double>(track.size) + allowed, id);
        free -= allowed;
    }
    if (available && free > 0) {
        std::vector<bool> frozen(axis.tracks.size());
        double nonflex = 0;
        for (const auto gap : axis.gutter_after) nonflex += gap;
        for (std::size_t i = 0; i < axis.tracks.size(); ++i) {
            if (axis.tracks[i].flex <= 0) {
                frozen[i] = true;
                nonflex += axis.tracks[i].size;
            }
        }
        for (std::size_t pass = 0; pass < axis.tracks.size(); ++pass) {
            double factors = 0;
            for (std::size_t i = 0; i < axis.tracks.size(); ++i) {
                if (!frozen[i]) factors += axis.tracks[i].flex;
            }
            if (factors <= 0) break;
            const double unit = (static_cast<double>(*available) - nonflex)
                / std::max(1.0, factors);
            bool changed = false;
            for (std::size_t i = 0; i < axis.tracks.size(); ++i) {
                if (!frozen[i] && axis.tracks[i].size > unit * axis.tracks[i].flex) {
                    frozen[i] = true;
                    nonflex += axis.tracks[i].size;
                    changed = true;
                }
            }
            if (changed) continue;
            for (std::size_t i = 0; i < axis.tracks.size(); ++i) {
                if (!frozen[i]) axis.tracks[i].size = checked(
                    std::max<double>(axis.tracks[i].size, unit * axis.tracks[i].flex), id);
            }
            break;
        }
        free = static_cast<double>(*available);
        for (const auto gap : axis.gutter_after) free -= gap;
        for (const auto& track : axis.tracks) free -= track.size;
    } else if (!available) {
        double fr_unit = 0;
        for (const auto& track : axis.tracks) {
            if (track.flex > 0) {
                fr_unit = std::max(fr_unit,
                    static_cast<double>(track.preferred) / track.flex);
            }
        }
        for (auto& track : axis.tracks) {
            if (track.flex > 0) {
                track.size = checked(std::max<double>(track.size,
                    fr_unit * track.flex), id);
            }
        }
    }
    if (available && free > 0) {
        const auto count = static_cast<std::size_t>(std::count_if(axis.tracks.begin(), axis.tracks.end(),
            [](const TrackState& track) { return track.auto_max; }));
        if (count) {
            const Unit share = checked(free / count, id);
            for (auto& track : axis.tracks) if (track.auto_max) {
                track.size = checked(static_cast<double>(track.size) + share, id);
            }
        }
    }
    axis.offsets.resize(axis.tracks.size() + 1);
    for (std::size_t i = 0; i < axis.tracks.size(); ++i) {
        axis.offsets[i + 1] = checked(static_cast<double>(axis.offsets[i])
            + axis.tracks[i].size + axis.gutter_after[i], id);
    }
}

void align_tracks(TrackAxis& axis, Unit available,
                  ContentAlignment alignment, NodeId id) {
    const Unit used = axis.offsets.empty() ? 0 : axis.offsets.back();
    const Unit free = checked(static_cast<double>(available) - used, id);
    if (alignment.safety == AlignmentSafety::Safe && free < 0) return;
    std::size_t visible = 0;
    for (const auto& track : axis.tracks) if (!track.collapsed) ++visible;
    Unit start = 0, extra_gap = 0;
    switch (alignment.position) {
    case ContentPosition::End:
    case ContentPosition::FlexEnd: start = free; break;
    case ContentPosition::Center: start = checked(static_cast<double>(free) / 2, id); break;
    case ContentPosition::SpaceBetween:
        if (free > 0 && visible > 1) {
            extra_gap = checked(static_cast<double>(free) / (visible - 1), id);
        }
        break;
    case ContentPosition::SpaceAround:
        if (free > 0 && visible) {
            extra_gap = checked(static_cast<double>(free) / visible, id);
            start = checked(static_cast<double>(extra_gap) / 2, id);
        }
        break;
    case ContentPosition::SpaceEvenly:
        if (free > 0 && visible) {
            extra_gap = checked(static_cast<double>(free) / (visible + 1), id);
            start = extra_gap;
        }
        break;
    default: break;
    }
    bool later_visible = false;
    for (std::size_t i = axis.tracks.size(); i-- > 0;) {
        if (!axis.tracks[i].collapsed && later_visible) {
            axis.gutter_after[i] = checked(
                static_cast<double>(axis.gutter_after[i]) + extra_gap, id);
        }
        later_visible |= !axis.tracks[i].collapsed;
    }
    if (axis.offsets.empty()) axis.offsets.resize(1);
    axis.offsets[0] = start;
    for (std::size_t i = 0; i < axis.tracks.size(); ++i) {
        axis.offsets[i + 1] = checked(static_cast<double>(axis.offsets[i])
            + axis.tracks[i].size + axis.gutter_after[i], id);
    }
}

Unit resolved_gap(const Length& value, std::optional<Unit> basis, NodeId id) {
    const auto numeric = value.resolve_numeric(basis);
    if (!numeric) return 0;
    if (*numeric < 0) throw LayoutFailure(LayoutErrorCode::InvalidStyle, id, "Grid gap must be nonnegative");
    return *numeric;
}

Unit align_offset(Unit free, ItemAlignment alignment, NodeId id) {
    if (alignment.safety == AlignmentSafety::Safe && free < 0) return 0;
    switch (alignment.position) {
    case ItemPosition::End:
    case ItemPosition::FlexEnd:
    case ItemPosition::SelfEnd: return free;
    case ItemPosition::Center: return checked(static_cast<double>(free) / 2, id);
    default: return 0;
    }
}
} // namespace

Fragment Engine::grid(const LayoutNode& node, const Constraints& constraints,
                      const ResolvedBox& box, std::size_t depth) {
    const auto decoration_width = box.horizontal_decoration();
    const auto decoration_height = box.vertical_decoration();
    if (!decoration_width || !decoration_height) {
        throw LayoutFailure(LayoutErrorCode::UnresolvedPercentage, node.id,
            "Grid decoration requires a definite containing inline size");
    }
    auto border_width = constraints.known_width ? constraints.known_width : box.preferred.width;
    auto border_height = constraints.known_height ? constraints.known_height : box.preferred.height;
    if (!border_width && constraints.available.width.kind == AvailableKind::Definite) {
        border_width = constraints.available.width.value;
    }
    auto inner_width = border_width
        ? std::optional<Unit>(std::max(Unit{0}, *border_width - *decoration_width)) : std::nullopt;
    auto inner_height = border_height
        ? std::optional<Unit>(std::max(Unit{0}, *border_height - *decoration_height)) : std::nullopt;
    const Unit column_gap = resolved_gap(node.style.gap.width, inner_width, node.id);
    const Unit row_gap = resolved_gap(node.style.gap.height, inner_height, node.id);
    const auto repeat_basis = [](std::optional<Unit> preferred,
        std::optional<Unit> minimum, std::optional<Unit> maximum, Unit decoration) {
        auto value = preferred ? preferred : maximum ? maximum : minimum;
        if (value && maximum) *value = std::min(*value, *maximum);
        if (value && minimum) *value = std::max(*value, *minimum);
        if (value) *value = std::max(Unit{0}, *value - decoration);
        return value;
    };
    const auto repeat_width = repeat_basis(border_width, box.minimum.width,
        box.maximum.width, *decoration_width);
    const auto repeat_height = repeat_basis(border_height, box.minimum.height,
        box.maximum.height, *decoration_height);
    auto columns = expand(node.style.grid.template_columns,
        node.style.grid.column_line_names, repeat_width, column_gap,
        !border_width && !box.maximum.width, node.id);
    auto rows = expand(node.style.grid.template_rows,
        node.style.grid.row_line_names, repeat_height, row_gap,
        !border_height && !box.maximum.height, node.id);
    apply_areas(node.style.grid, rows, columns);
    std::vector<Placement> placements(node.children.size());
    for (std::size_t i = 0; i < node.children.size(); ++i) {
        const auto& child = get(node.children[i]);
        if (child.style.display == Display::None) { placements[i].hidden = true; continue; }
        if (child.style.position == Position::Absolute) {
            placements[i].hidden = true;
            continue;
        }
        placements[i].row = axis_placement(child.style.grid.row, rows);
        placements[i].column = axis_placement(child.style.grid.column, columns);
    }
    place(placements, static_cast<std::int32_t>(rows.tracks.size()),
        static_cast<std::int32_t>(columns.tracks.size()), node.style.grid.auto_flow, node.id);
    std::int32_t min_row = 0, max_row = static_cast<std::int32_t>(rows.tracks.size());
    std::int32_t min_column = 0, max_column = static_cast<std::int32_t>(columns.tracks.size());
    for (const auto& placement : placements) {
        if (placement.hidden) continue;
        min_row = std::min(min_row, placement.row_start);
        max_row = std::max(max_row, placement.row_end);
        min_column = std::min(min_column, placement.column_start);
        max_column = std::max(max_column, placement.column_end);
    }
    auto track_columns = make_axis(columns.tracks, node.style.grid.auto_columns,
        min_column, max_column, inner_width, column_gap, node.id);
    auto track_rows = make_axis(rows.tracks, node.style.grid.auto_rows,
        min_row, max_row, inner_height, row_gap, node.id);
    collapse_auto_fit(track_columns, columns, placements, true);
    collapse_auto_fit(track_rows, rows, placements, false);
    std::vector<ItemContribution> contributions;
    contributions.reserve(node.children.size());
    for (std::size_t i = 0; i < node.children.size(); ++i) {
        if (placements[i].hidden) continue;
        const auto id = node.children[i];
        ResolvedBox child_box;
        try { child_box = resolve_layout_box(get(id), inner_width, inner_height); }
        catch (const std::invalid_argument& error) {
            throw LayoutFailure(LayoutErrorCode::InvalidStyle, id, error.what());
        }
        Constraints intrinsic;
        intrinsic.containing_width = inner_width;
        intrinsic.containing_height = inner_height;
        intrinsic.available = {AvailableSpace::min_content(), AvailableSpace::min_content()};
        const auto min_fragment = layout(id, intrinsic, depth + 1);
        intrinsic.available = {AvailableSpace::max_content(), AvailableSpace::max_content()};
        const auto max_fragment = layout(id, intrinsic, depth + 1);
        contributions.push_back({id, i, placements[i], min_fragment.size, max_fragment.size,
            edges(child_box.margin, id)});
    }
    size_axis(track_columns, contributions, true, inner_width, node.id);
    size_axis(track_rows, contributions, false, inner_height, node.id);
    const Unit measured_width = track_columns.offsets.empty() ? 0
        : track_columns.offsets.back();
    const Unit measured_height = track_rows.offsets.empty() ? 0
        : track_rows.offsets.back();
    if (!border_width) {
        inner_width = measured_width;
        border_width = checked(static_cast<double>(measured_width) + *decoration_width, node.id);
    }
    if (!border_height) {
        inner_height = measured_height;
        border_height = checked(static_cast<double>(measured_height) + *decoration_height, node.id);
    }
    Size candidate{*border_width, *border_height};
    candidate = constrain_size(candidate, box);
    inner_width = std::max(Unit{0}, candidate.width - *decoration_width);
    inner_height = std::max(Unit{0}, candidate.height - *decoration_height);
    align_tracks(track_columns, *inner_width,
        node.style.justify_content.value_or(ContentAlignment{}), node.id);
    align_tracks(track_rows, *inner_height,
        node.style.align_content.value_or(ContentAlignment{}), node.id);
    Fragment result;
    result.size = candidate;
    result.nodes.push_back(make_layout(node, candidate, box));
    GridTracks report;
    report.node = node.id;
    report.column_origin = track_columns.origin;
    report.row_origin = track_rows.origin;
    for (const auto& track : track_columns.tracks) report.columns.push_back(track.size);
    for (const auto& track : track_rows.tracks) report.rows.push_back(track.size);
    result.grid_tracks.push_back(std::move(report));
    std::vector<Fragment> fragments(node.children.size());
    struct BaselinePlacement {
        std::size_t source;
        std::int32_t row;
        Unit baseline;
        Unit margin_top;
    };
    std::vector<BaselinePlacement> baseline_items;
    std::unordered_map<std::int32_t, Unit> row_baselines;
    const auto border = edges(box.border, node.id);
    const auto padding = edges(box.padding, node.id);
    const Unit content_x = checked(static_cast<double>(border.left) + padding.left, node.id);
    const Unit content_y = checked(static_cast<double>(border.top) + padding.top, node.id);
    for (std::size_t i = 0; i < node.children.size(); ++i) {
        const auto id = node.children[i];
        const auto& child = get(id);
        if (child.style.position == Position::Absolute
            && child.style.display != Display::None) {
            fragments[i] = absolute(node, id, candidate, box, depth);
            continue;
        }
        if (placements[i].hidden) { fragments[i] = hidden(child, depth + 1); continue; }
        const auto& place = placements[i];
        const Unit cell_width = track_columns.span(place.column_start, place.column_end);
        const Unit cell_height = track_rows.span(place.row_start, place.row_end);
        const Unit cell_x = track_columns.line(place.column_start);
        const Unit cell_y = track_rows.line(place.row_start);
        const auto& child_box = child.style.box;
        const auto resolved = resolve_layout_box(child, cell_width, cell_height);
        const auto margin = edges(resolved.margin, id);
        Constraints final;
        final.containing_width = cell_width;
        final.containing_height = cell_height;
        final.available = {AvailableSpace::definite(cell_width), AvailableSpace::definite(cell_height)};
        const auto horizontal = child.style.justify_self.value_or(node.style.justify_items.value_or(
            ItemAlignment{ItemPosition::Stretch, AlignmentSafety::Unsafe}));
        const auto vertical = child.style.align_self.value_or(node.style.align_items.value_or(
            ItemAlignment{ItemPosition::Stretch, AlignmentSafety::Unsafe}));
        if (horizontal.position == ItemPosition::Stretch
            && child_box.size.width.kind() == LengthKind::Auto
            && resolved.margin.left.kind != EdgeKind::Auto
            && resolved.margin.right.kind != EdgeKind::Auto) {
            final.known_width = std::max(Unit{0}, checked(
                static_cast<double>(cell_width) - margin.left - margin.right, id));
        }
        if (vertical.position == ItemPosition::Stretch
            && child_box.size.height.kind() == LengthKind::Auto
            && resolved.margin.top.kind != EdgeKind::Auto
            && resolved.margin.bottom.kind != EdgeKind::Auto) {
            final.known_height = std::max(Unit{0}, checked(
                static_cast<double>(cell_height) - margin.top - margin.bottom, id));
        }
        auto child_fragment = layout(id, final, depth + 1);
        if (vertical.position == ItemPosition::Baseline
            && resolved.margin.top.kind != EdgeKind::Auto
            && resolved.margin.bottom.kind != EdgeKind::Auto) {
            const Unit baseline = child_fragment.nodes.front()
                .first_baseline.value_or(child_fragment.size.height);
            const Unit ascent = checked(static_cast<double>(margin.top) + baseline, id);
            auto [entry, inserted] = row_baselines.try_emplace(place.row_start, ascent);
            if (!inserted) entry->second = std::max(entry->second, ascent);
            baseline_items.push_back({i, place.row_start, baseline, margin.top});
        }
        const Unit horizontal_free = checked(static_cast<double>(cell_width)
            - child_fragment.size.width - margin.left - margin.right, id);
        const Unit vertical_free = checked(static_cast<double>(cell_height)
            - child_fragment.size.height - margin.top - margin.bottom, id);
        const bool auto_left = resolved.margin.left.kind == EdgeKind::Auto;
        const bool auto_right = resolved.margin.right.kind == EdgeKind::Auto;
        const bool auto_top = resolved.margin.top.kind == EdgeKind::Auto;
        const bool auto_bottom = resolved.margin.bottom.kind == EdgeKind::Auto;
        Unit used_left = margin.left, used_right = margin.right;
        Unit used_top = margin.top, used_bottom = margin.bottom;
        if (horizontal_free > 0 && (auto_left || auto_right)) {
            const Unit share = checked(static_cast<double>(horizontal_free)
                / (static_cast<int>(auto_left) + static_cast<int>(auto_right)), id);
            if (auto_left) used_left = share;
            if (auto_right) used_right = share;
        }
        if (vertical_free > 0 && (auto_top || auto_bottom)) {
            const Unit share = checked(static_cast<double>(vertical_free)
                / (static_cast<int>(auto_top) + static_cast<int>(auto_bottom)), id);
            if (auto_top) used_top = share;
            if (auto_bottom) used_bottom = share;
        }
        Unit x = checked(static_cast<double>(cell_x) + used_left
            + ((auto_left || auto_right) ? 0 : align_offset(horizontal_free, horizontal, id)), id);
        Unit y = checked(static_cast<double>(cell_y) + used_top
            + ((auto_top || auto_bottom) ? 0 : align_offset(vertical_free, vertical, id)), id);
        child_fragment.nodes.front().margin = {used_left, used_top, used_right, used_bottom};
        if (node.style.box.direction == Direction::Rtl) {
            x = checked(static_cast<double>(*inner_width) - x - child_fragment.size.width, id);
        }
        translate(child_fragment,
            checked(static_cast<double>(content_x) + x, id),
            checked(static_cast<double>(content_y) + y, id), id);
        const auto [relative_x, relative_y] = relative_offset(child,
            Size{*inner_width, *inner_height}, node.id);
        translate(child_fragment, relative_x, relative_y, id);
        fragments[i] = std::move(child_fragment);
    }
    for (const auto& item : baseline_items) {
        const Unit delta = checked(static_cast<double>(row_baselines.at(item.row))
            - item.baseline - item.margin_top, node.id);
        translate(fragments[item.source], 0, delta, node.id);
        auto& used_margin = fragments[item.source].nodes.front().margin;
        used_margin.top = checked(static_cast<double>(used_margin.top) + delta,
            node.id);
    }
    for (auto& fragment : fragments) {
        result.nodes.insert(result.nodes.end(),
            std::make_move_iterator(fragment.nodes.begin()),
            std::make_move_iterator(fragment.nodes.end()));
        result.grid_tracks.insert(result.grid_tracks.end(),
            std::make_move_iterator(fragment.grid_tracks.begin()),
            std::make_move_iterator(fragment.grid_tracks.end()));
    }
    return result;
}

} // namespace axisly::css::detail
