#include "grid_internal.hpp"
#include "item_alignment.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <unordered_map>
#include <utility>

namespace axisly::css::detail {
namespace {
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
    const auto span_size = [](const GridLine& line) {
        return line.kind() == GridLineKind::NamedSpan ? std::int32_t{1}
            : static_cast<std::int32_t>(line.span_count());
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
        const auto target = named_span(axis, *start, value.start, value.end, true);
        return {std::min(*start, target),
            std::max<std::int32_t>(1, std::abs(target - *start))};
    }
    if (end && value.start.kind() == GridLineKind::NamedSpan) {
        const auto target = named_span(axis, *end, value.end, value.start, false);
        return {std::min(target, *end),
            std::max<std::int32_t>(1, std::abs(*end - target))};
    }
    if (start) return {*start, span};
    if (end) return {*end - span, span};
    return {std::nullopt, span};
}

AxisEstimate estimate_axis(const GridPlacement& value, std::int32_t explicit_count) {
    // Taffy's initial grid extent is computed before named lines are resolved.
    // A named endpoint therefore contributes no definite bound at this stage.
    const auto line_index = [explicit_count](const GridLine& line)
        -> std::optional<std::int32_t> {
        if (line.kind() != GridLineKind::Index || line.index_value() == 0) {
            return std::nullopt;
        }
        const auto index = line.index_value();
        return index > 0 ? static_cast<std::int32_t>(index - 1)
            : explicit_count + 1 + index;
    };
    const auto start = line_index(value.start);
    const auto end = line_index(value.end);
    const auto span_count = [](const GridLine& line) {
        return line.kind() == GridLineKind::Span
            ? static_cast<std::int32_t>(line.span_count()) : std::int32_t{1};
    };
    if (start && end) {
        return {*start == *end ? *start : std::min(*start, *end),
            *start == *end ? *start + 1 : std::max(*start, *end), 1};
    }
    if (start) return {*start, *start + span_count(value.end), 1};
    if (end) {
        const auto minimum = value.start.kind() == GridLineKind::Span
            ? *end - span_count(value.start) : *end;
        return {minimum, *end, 1};
    }
    const auto indefinite_span = value.start.kind() == GridLineKind::Span
        ? span_count(value.start) : span_count(value.end);
    return {0, 0, indefinite_span};
}

struct OccupiedArea {
    std::int32_t primary_start{}, primary_end{};
    std::int32_t secondary_start{}, secondary_end{};
    bool auto_placed{};
};

class GridOccupancy {
public:
    void add(std::int32_t primary, std::int32_t primary_span,
             std::int32_t secondary, std::int32_t secondary_span, bool auto_placed) {
        areas_.push_back({primary, primary + primary_span,
            secondary, secondary + secondary_span, auto_placed});
    }

    [[nodiscard]] std::optional<std::int32_t> collision_jump(
        std::int32_t primary, std::int32_t primary_span,
        std::int32_t secondary, std::int32_t secondary_span,
        bool along_primary) const {
        std::optional<std::int32_t> next;
        for (const auto& area : areas_) {
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
        for (const auto& area : areas_) {
            if (!area.auto_placed || secondary < area.secondary_start
                || secondary >= area.secondary_end) continue;
            last = std::max(last.value_or(area.primary_end - 1), area.primary_end - 1);
        }
        return last;
    }

private:
    std::vector<OccupiedArea> areas_;
};

void place(std::vector<Placement>& placements, std::int32_t explicit_rows,
           std::int32_t explicit_columns, GridAutoFlow flow, NodeId id) {
    const bool by_row = flow == GridAutoFlow::Row || flow == GridAutoFlow::RowDense;
    const bool dense = flow == GridAutoFlow::RowDense || flow == GridAutoFlow::ColumnDense;
    const auto primary = [by_row](const Placement& item) -> const AxisPlacement& {
        return by_row ? item.column : item.row;
    };
    const auto secondary = [by_row](const Placement& item) -> const AxisPlacement& {
        return by_row ? item.row : item.column;
    };
    std::int32_t primary_start = 0;
    std::int32_t primary_end = by_row ? explicit_columns : explicit_rows;
    std::int32_t secondary_start = 0;
    std::int32_t largest_primary_span = 1;
    for (const auto& item : placements) {
        if (item.hidden) continue;
        const auto& p = by_row ? item.column_estimate : item.row_estimate;
        const auto& s = by_row ? item.row_estimate : item.column_estimate;
        primary_start = std::min(primary_start, p.minimum_line);
        primary_end = std::max(primary_end, p.maximum_line);
        largest_primary_span = std::max(largest_primary_span, p.indefinite_span);
        secondary_start = std::min(secondary_start, s.minimum_line);
    }
    primary_end = std::max(primary_end, primary_start + largest_primary_span);
    GridOccupancy occupied;
    const auto record = [&](Placement& item, std::int32_t p, std::int32_t s,
                            bool auto_placed) {
        const auto p_end = p + primary(item).span;
        const auto s_end = s + secondary(item).span;
        if (by_row) {
            item.column_start = p;
            item.column_end = p_end;
            item.row_start = s;
            item.row_end = s_end;
        } else {
            item.row_start = p;
            item.row_end = p_end;
            item.column_start = s;
            item.column_end = s_end;
        }
        occupied.add(p, primary(item).span, s, secondary(item).span, auto_placed);
        primary_start = std::min(primary_start, p);
        primary_end = std::max(primary_end, p_end);
        secondary_start = std::min(secondary_start, s);
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
}

Unit resolved_gap(const Length& value, std::optional<Unit> basis, NodeId id) {
    const auto numeric = value.resolve_numeric(basis);
    if (!numeric) return 0;
    if (*numeric < 0) throw LayoutFailure(LayoutErrorCode::InvalidStyle, id, "Grid gap must be nonnegative");
    return *numeric;
}

Unit fit_content_item_size(Unit minimum, Unit maximum, Unit area,
                           Unit before_margin, Unit after_margin, NodeId id) {
    const double available = std::max(0.0, static_cast<double>(area)
        - before_margin - after_margin);
    return checked(std::max<double>(minimum,
        std::min<double>(std::max(minimum, maximum), available)), id);
}

Unit align_offset(Unit free, ItemAlignment alignment, bool horizontal,
    Direction container_direction, Direction item_direction, NodeId id) {
    if (alignment.safety == AlignmentSafety::Safe && free < 0) return 0;
    const bool reversed = horizontal && container_direction == Direction::Rtl;
    switch (item_edge(alignment.position, horizontal, container_direction,
        item_direction, reversed, reversed)) {
    case ItemEdge::Start: return 0;
    case ItemEdge::Center: return checked(static_cast<double>(free) / 2, id);
    case ItemEdge::End: return free;
    }
    return 0;
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
    const auto requested = requested_size(constraints, box);
    auto border_width = requested.width;
    auto border_height = requested.height;
    if (border_width) {
        *border_width = constrain_axis(*border_width,
            box.minimum.width, box.maximum.width);
    }
    if (border_height) {
        *border_height = constrain_axis(*border_height,
            box.minimum.height, box.maximum.height);
    }
    auto inner_width = border_width
        ? std::optional<Unit>(std::max(Unit{0}, *border_width - *decoration_width)) : std::nullopt;
    auto inner_height = border_height
        ? std::optional<Unit>(std::max(Unit{0}, *border_height - *decoration_height)) : std::nullopt;
    const auto initial_track_width = inner_width ? inner_width
        : box.minimum.width ? std::optional<Unit>(std::max(Unit{0},
            *box.minimum.width - *decoration_width)) : std::nullopt;
    const auto initial_track_height = inner_height ? inner_height
        : box.minimum.height ? std::optional<Unit>(std::max(Unit{0},
            *box.minimum.height - *decoration_height)) : std::nullopt;
    const auto column_query = !inner_width && initial_track_width
        ? AvailableKind::Definite : constraints.available.width.kind;
    const auto row_query = !inner_height && initial_track_height
        ? AvailableKind::Definite : constraints.available.height.kind;
    Unit column_gap = resolved_gap(node.style.gap.width, initial_track_width, node.id);
    Unit row_gap = resolved_gap(node.style.gap.height, initial_track_height, node.id);
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
        node.style.grid.column_line_names, repeat_width,
        resolved_gap(node.style.gap.width, repeat_width, node.id),
        !border_width && !box.maximum.width, node.id);
    auto rows = expand(node.style.grid.template_rows,
        node.style.grid.row_line_names, repeat_height,
        resolved_gap(node.style.gap.height, repeat_height, node.id),
        !border_height && !box.maximum.height, node.id);
    apply_areas(node.style.grid, rows, columns);
    std::vector<Placement> placements(node.children.size());
    std::vector<AxisPlacement> absolute_rows(node.children.size());
    std::vector<AxisPlacement> absolute_columns(node.children.size());
    for (std::size_t i = 0; i < node.children.size(); ++i) {
        const auto& child = get(node.children[i]);
        if (child.style.display == Display::None) { placements[i].hidden = true; continue; }
        if (child.style.position == Position::Absolute) {
            placements[i].hidden = true;
            absolute_rows[i] = axis_placement(child.style.grid.row, rows);
            absolute_columns[i] = axis_placement(child.style.grid.column, columns);
            continue;
        }
        placements[i].row = axis_placement(child.style.grid.row, rows);
        placements[i].column = axis_placement(child.style.grid.column, columns);
        placements[i].row_estimate = estimate_axis(child.style.grid.row,
            static_cast<std::int32_t>(rows.tracks.size()));
        placements[i].column_estimate = estimate_axis(child.style.grid.column,
            static_cast<std::int32_t>(columns.tracks.size()));
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
        min_column, max_column, initial_track_width, column_gap, node.id);
    auto track_rows = make_axis(rows.tracks, node.style.grid.auto_rows,
        min_row, max_row, initial_track_height, row_gap, node.id);
    collapse_auto_fit(track_columns, columns, placements, true);
    collapse_auto_fit(track_rows, rows, placements, false);
    std::vector<ItemContribution> contributions;
    contributions.reserve(node.children.size());
    std::vector<std::optional<std::size_t>> contribution_indices(node.children.size());
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
        const Size minimum_contribution{
            grid_item_minimum(get(id), child_box, placements[i], track_columns,
                min_fragment.size.width, true),
            grid_item_minimum(get(id), child_box, placements[i], track_rows,
                min_fragment.size.height, false)};
        const auto scrolls = [](Overflow overflow) {
            return overflow == Overflow::Hidden || overflow == Overflow::Scroll;
        };
        const auto horizontal = get(id).style.justify_self.value_or(
            node.style.justify_items.value_or(ItemAlignment{}));
        const bool stretched_ratio = inner_height && child_box.aspect_ratio
            && get(id).style.box.size.width.kind() == LengthKind::Auto
            && horizontal.position == ItemPosition::Stretch
            && !child_box.preferred.width;
        contribution_indices[i] = contributions.size();
        contributions.push_back({placements[i], minimum_contribution,
            min_fragment.size, max_fragment.size, edges(child_box.margin, id),
            scrolls(get(id).style.overflow_x), scrolls(get(id).style.overflow_y),
            stretched_ratio ? std::optional<ResolvedBox>(child_box) : std::nullopt});
    }
    if (inner_height && std::any_of(contributions.begin(), contributions.end(),
        [](const ItemContribution& item) { return item.stretched_ratio_box.has_value(); })) {
        auto measured_rows = track_rows;
        size_axis(measured_rows, contributions, false, initial_track_height,
            row_query, node.id);
        apply_cross_axis_ratio_contributions(measured_rows, contributions, node.id);
    }
    size_axis(track_columns, contributions, true, initial_track_width,
        column_query, node.id);
    size_axis(track_rows, contributions, false, initial_track_height,
        row_query, node.id);
    const Unit measured_width = track_columns.offsets.empty() ? 0
        : track_columns.offsets.back();
    const Unit measured_height = track_rows.offsets.empty() ? 0
        : track_rows.offsets.back();
    const bool intrinsic_width = !border_width;
    const bool intrinsic_height = !border_height;
    const bool cyclic_columns = intrinsic_width
        && has_cyclic_percentage(track_columns);
    const bool cyclic_rows = intrinsic_height
        && has_cyclic_percentage(track_rows);
    if (intrinsic_width) {
        border_width = constrain_axis(checked(static_cast<double>(measured_width)
            + *decoration_width, node.id), box.minimum.width, box.maximum.width);
        inner_width = std::max(Unit{0}, *border_width - *decoration_width);
    }
    if (intrinsic_height) {
        border_height = constrain_axis(checked(static_cast<double>(measured_height)
            + *decoration_height, node.id), box.minimum.height, box.maximum.height);
        inner_height = std::max(Unit{0}, *border_height - *decoration_height);
    }
    const Unit final_column_gap = resolved_gap(node.style.gap.width, inner_width, node.id);
    const Unit final_row_gap = resolved_gap(node.style.gap.height, inner_height, node.id);
    const bool changed_column_gap = final_column_gap != column_gap;
    const bool changed_row_gap = final_row_gap != row_gap;
    column_gap = final_column_gap;
    row_gap = final_row_gap;
    if (intrinsic_width && (*inner_width != measured_width || cyclic_columns
        || changed_column_gap)) {
        track_columns = make_axis(columns.tracks, node.style.grid.auto_columns,
            min_column, max_column, inner_width, column_gap, node.id);
        collapse_auto_fit(track_columns, columns, placements, true);
        size_axis(track_columns, contributions, true, inner_width,
            column_query, node.id);
    }
    if (intrinsic_height && (*inner_height != measured_height || cyclic_rows
        || changed_row_gap)) {
        track_rows = make_axis(rows.tracks, node.style.grid.auto_rows,
            min_row, max_row, inner_height, row_gap, node.id);
        collapse_auto_fit(track_rows, rows, placements, false);
        size_axis(track_rows, contributions, false, inner_height,
            row_query, node.id);
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
            Rect area{border.left, border.top,
                std::max(Unit{0}, candidate.width - border.left - border.right),
                std::max(Unit{0}, candidate.height - border.top - border.bottom)};
            const auto explicit_span = [](const GridPlacement& placement,
                const AxisPlacement& resolved, const TrackAxis& tracks) {
                if (!resolved.start || placement.start.kind() == GridLineKind::Auto
                    || placement.end.kind() == GridLineKind::Auto) return false;
                const auto finish = *resolved.start + resolved.span;
                return *resolved.start >= tracks.origin
                    && finish <= tracks.origin
                        + static_cast<std::int32_t>(tracks.tracks.size());
            };
            if (explicit_span(child.style.grid.column, absolute_columns[i], track_columns)) {
                const auto start = *absolute_columns[i].start;
                const auto end = start + absolute_columns[i].span;
                area.x = checked(static_cast<double>(content_x)
                    + track_columns.line(start), id);
                area.width = track_columns.span(start, end);
            }
            if (explicit_span(child.style.grid.row, absolute_rows[i], track_rows)) {
                const auto start = *absolute_rows[i].start;
                const auto end = start + absolute_rows[i].span;
                area.y = checked(static_cast<double>(content_y)
                    + track_rows.line(start), id);
                area.height = track_rows.span(start, end);
            }
            fragments[i] = absolute(node, id, candidate, box, depth, area);
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
            ItemAlignment{resolved.aspect_ratio ? ItemPosition::Start
                : ItemPosition::Stretch, AlignmentSafety::Unsafe}));
        const auto& contribution = contributions[contribution_indices[i].value()];
        const bool intrinsic_grid_item = child.style.display == Display::Grid;
        if (horizontal.position == ItemPosition::Stretch
            && child_box.size.width.kind() == LengthKind::Auto
            && resolved.margin.left.kind != EdgeKind::Auto
            && resolved.margin.right.kind != EdgeKind::Auto) {
            final.known_width = constrain_axis(std::max(Unit{0}, checked(
                static_cast<double>(cell_width) - margin.left - margin.right, id)),
                resolved.minimum.width, resolved.maximum.width);
        } else if (child_box.size.width.kind() == LengthKind::Auto
                   && !resolved.preferred.width
                   && intrinsic_grid_item) {
            final.known_width = constrain_axis(fit_content_item_size(
                contribution.min_content.width, contribution.max_content.width,
                cell_width, margin.left, margin.right, id),
                resolved.minimum.width, resolved.maximum.width);
        }
        if (vertical.position == ItemPosition::Stretch
            && child_box.size.height.kind() == LengthKind::Auto
            && resolved.margin.top.kind != EdgeKind::Auto
            && resolved.margin.bottom.kind != EdgeKind::Auto) {
            final.known_height = constrain_axis(std::max(Unit{0}, checked(
                static_cast<double>(cell_height) - margin.top - margin.bottom, id)),
                resolved.minimum.height, resolved.maximum.height);
        } else if (child_box.size.height.kind() == LengthKind::Auto
                   && !resolved.preferred.height
                   && intrinsic_grid_item) {
            final.known_height = constrain_axis(fit_content_item_size(
                contribution.min_content.height, contribution.max_content.height,
                cell_height, margin.top, margin.bottom, id),
                resolved.minimum.height, resolved.maximum.height);
        }
        auto child_fragment = layout(id, final, depth + 1);
        if (vertical.position == ItemPosition::Baseline
            && resolved.margin.top.kind != EdgeKind::Auto
            && resolved.margin.bottom.kind != EdgeKind::Auto) {
            const Unit baseline = child_baseline(child_fragment, child.style)
                .value_or(child_fragment.size.height);
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
            + ((auto_left || auto_right) ? 0 : align_offset(horizontal_free,
                horizontal, true, node.style.box.direction,
                child.style.box.direction, id)), id);
        Unit y = checked(static_cast<double>(cell_y) + used_top
            + ((auto_top || auto_bottom) ? 0 : align_offset(vertical_free,
                vertical, false, node.style.box.direction,
                child.style.box.direction, id)), id);
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
    std::optional<std::int32_t> first_row;
    std::optional<std::size_t> first_item;
    for (std::size_t source = 0; source < placements.size(); ++source) {
        if (placements[source].hidden
            || get(node.children[source]).style.position == Position::Absolute) continue;
        if (!first_row || placements[source].row_start < *first_row) {
            first_row = placements[source].row_start;
            first_item = source;
        }
    }
    if (first_row) {
        std::optional<BaselinePlacement> aligned;
        for (const auto& item : baseline_items) {
            if (item.row == *first_row) {
                aligned = item;
                break;
            }
        }
        const auto source = aligned ? aligned->source : *first_item;
        const auto& fragment = fragments[source];
        const Unit baseline = aligned
            ? checked(static_cast<double>(aligned->baseline)
                + aligned->margin_top, node.id)
            : fragment.size.height;
        result.nodes.front().first_baseline = checked(
            static_cast<double>(fragment.nodes.front().border_box.y)
                + baseline, node.id);
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
