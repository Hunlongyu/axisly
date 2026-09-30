#include "grid_internal.hpp"
#include "grid_item_sizing.hpp"
#include "item_alignment.hpp"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <unordered_map>
#include <utility>

namespace axisly::css::detail {
namespace {
Unit physical_track_start(Unit content_start, Unit inner_size,
                          Unit inline_start, Unit span,
                          Direction direction, NodeId node) {
    const double offset = direction == Direction::Rtl
        ? static_cast<double>(inner_size) - inline_start - span
        : inline_start;
    return checked(static_cast<double>(content_start) + offset, node);
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
    const auto maximum_track_width = box.maximum.width
        ? std::optional<Unit>(std::max(Unit{0}, *box.maximum.width - *decoration_width))
        : std::nullopt;
    const auto maximum_track_height = box.maximum.height
        ? std::optional<Unit>(std::max(Unit{0}, *box.maximum.height - *decoration_height))
        : std::nullopt;
    const auto constrained_grid_space = [](AvailableSpace available,
        std::optional<Unit> border_size, std::optional<Unit> minimum,
        std::optional<Unit> maximum, Unit decoration) {
        if (border_size) {
            return AvailableSpace::definite(std::max(Unit{0}, *border_size - decoration));
        }
        if (available.kind == AvailableKind::Definite) {
            return AvailableSpace::definite(std::max(Unit{0},
                constrain_axis(available.value, minimum, maximum) - decoration));
        }
        return available;
    };
    const AxisSizingSpace column_space{
        constrained_grid_space(constraints.available.width, border_width,
            box.minimum.width, box.maximum.width, *decoration_width),
        inner_width, initial_track_width, maximum_track_width,
        node.style.justify_content.value_or(ContentAlignment{})};
    const AxisSizingSpace row_space{
        constrained_grid_space(constraints.available.height, border_height,
            box.minimum.height, box.maximum.height, *decoration_height),
        inner_height, initial_track_height, maximum_track_height,
        node.style.align_content.value_or(ContentAlignment{})};
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
    auto columns = expand_grid_template(node.style.grid.template_columns,
        node.style.grid.column_line_names, repeat_width,
        resolved_gap(node.style.gap.width, repeat_width, node.id),
        !border_width && !box.maximum.width, node.id);
    auto rows = expand_grid_template(node.style.grid.template_rows,
        node.style.grid.row_line_names, repeat_height,
        resolved_gap(node.style.gap.height, repeat_height, node.id),
        !border_height && !box.maximum.height, node.id);
    apply_grid_template_areas(node.style.grid, rows, columns);
    std::vector<Placement> placements(node.children.size());
    std::vector<AbsoluteGridPlacement> absolute_rows(node.children.size());
    std::vector<AbsoluteGridPlacement> absolute_columns(node.children.size());
    for (std::size_t i = 0; i < node.children.size(); ++i) {
        const auto& child = get(node.children[i]);
        if (child.style.display == Display::None) { placements[i].hidden = true; continue; }
        if (child.style.position == Position::Absolute) {
            placements[i].hidden = true;
            absolute_rows[i] = resolve_absolute_grid_placement(child.style.grid.row, rows);
            absolute_columns[i] = resolve_absolute_grid_placement(child.style.grid.column, columns);
            continue;
        }
        placements[i].row = resolve_grid_placement(child.style.grid.row, rows);
        placements[i].column = resolve_grid_placement(child.style.grid.column, columns);
        placements[i].row_estimate = estimate_grid_axis(child.style.grid.row,
            static_cast<std::int32_t>(rows.tracks.size()));
        placements[i].column_estimate = estimate_grid_axis(child.style.grid.column,
            static_cast<std::int32_t>(columns.tracks.size()));
    }
    const auto extent = place_grid_items(placements, static_cast<std::int32_t>(rows.tracks.size()),
        static_cast<std::int32_t>(columns.tracks.size()), node.style.grid.auto_flow, node.id);
    const auto min_row = extent.row_start, max_row = extent.row_end;
    const auto min_column = extent.column_start, max_column = extent.column_end;
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
        const auto min_fragment = layout_size(id, intrinsic, depth + 1);
        intrinsic.available = {AvailableSpace::max_content(), AvailableSpace::max_content()};
        const auto max_fragment = layout_size(id, intrinsic, depth + 1);
        const Size minimum_contribution{
            grid_item_minimum(get(id), child_box, placements[i], track_columns,
                min_fragment.size.width, true),
            grid_item_minimum(get(id), child_box, placements[i], track_rows,
                min_fragment.size.height, false)};
        const auto scrolls = [](Overflow overflow) {
            return overflow == Overflow::Hidden || overflow == Overflow::Scroll;
        };
        const bool ratio_candidate = inner_height && child_box.aspect_ratio
            && get(id).style.box.size.width.kind() == LengthKind::Auto
            && !child_box.preferred.width;
        // A stretched auto row has a used size, but it is still indefinite
        // while computing the item's inline intrinsic contribution.
        const bool ratio_from_rows = ratio_candidate && std::all_of(
            track_rows.tracks.begin()
                + (placements[i].row_start - track_rows.origin),
            track_rows.tracks.begin()
                + (placements[i].row_end - track_rows.origin),
            [inner_height](const TrackState& track) {
                return track.style.minimum.resolve_numeric(inner_height).has_value()
                    && track.style.maximum.resolve_numeric(inner_height).has_value();
            });
        contribution_indices[i] = contributions.size();
        contributions.push_back({placements[i], minimum_contribution,
            min_fragment.size, max_fragment.size, edges(child_box.margin, id),
            scrolls(get(id).style.overflow_x), scrolls(get(id).style.overflow_y),
            ratio_from_rows ? std::optional<ResolvedBox>(child_box) : std::nullopt});
    }
    if (inner_height && std::any_of(contributions.begin(), contributions.end(),
        [](const ItemContribution& item) { return item.ratio_from_rows_box.has_value(); })) {
        auto measured_rows = track_rows;
        size_axis(measured_rows, contributions, false, row_space, node.id);
        apply_cross_axis_ratio_contributions(measured_rows, contributions, node.id);
    }
    size_axis(track_columns, contributions, true, column_space, node.id);
    // Track contributions are axis-specific queries. Row measurements see the
    // provisional column widths; a later column pass can in turn see row
    // heights. Measuring both axes up front loses nested width/height reflow.
    const auto measure_cross_dependent_contributions = [&](bool rows_axis) {
        bool changed = false;
        for (std::size_t i = 0; i < node.children.size(); ++i) {
            if (!contribution_indices[i]) continue;
            const auto id = node.children[i];
            const auto& child = get(id);
            const auto& place = placements[i];
            const Unit cross_size = rows_axis
                ? track_columns.span(place.column_start, place.column_end)
                : track_rows.span(place.row_start, place.row_end);
            const std::optional<Unit> containing_width = rows_axis
                ? std::optional<Unit>{cross_size} : inner_width;
            const std::optional<Unit> containing_height = rows_axis
                ? inner_height : std::optional<Unit>{cross_size};
            ResolvedBox child_box;
            try { child_box = resolve_layout_box(child, containing_width, containing_height); }
            catch (const std::invalid_argument& error) {
                throw LayoutFailure(LayoutErrorCode::InvalidStyle, id, error.what());
            }
            const auto margin = edges(child_box.margin, id);
            Constraints intrinsic;
            intrinsic.containing_width = containing_width;
            intrinsic.containing_height = containing_height;
            auto& own_available = rows_axis ? intrinsic.available.height
                : intrinsic.available.width;
            auto& cross_available = rows_axis ? intrinsic.available.width
                : intrinsic.available.height;
            cross_available = AvailableSpace::definite(cross_size);
            const auto alignment = rows_axis
                ? child.style.justify_self.value_or(node.style.justify_items.value_or(
                    ItemAlignment{ItemPosition::Stretch, AlignmentSafety::Unsafe}))
                : child.style.align_self.value_or(node.style.align_items.value_or(
                    ItemAlignment{ItemPosition::Stretch, AlignmentSafety::Unsafe}));
            const auto& cross_style = rows_axis
                ? child.style.box.size.width : child.style.box.size.height;
            const auto& cross_margin_start = rows_axis
                ? child_box.margin.left : child_box.margin.top;
            const auto& cross_margin_end = rows_axis
                ? child_box.margin.right : child_box.margin.bottom;
            ResolvedDimensions stretch;
            if (alignment.position == ItemPosition::Stretch
                && cross_style.kind() == LengthKind::Auto
                && cross_margin_start.kind != EdgeKind::Auto
                && cross_margin_end.kind != EdgeKind::Auto) {
                const Unit margin_sum = rows_axis
                    ? checked(static_cast<double>(margin.left) + margin.right, id)
                    : checked(static_cast<double>(margin.top) + margin.bottom, id);
                const Unit known = std::max(Unit{0}, checked(
                    static_cast<double>(cross_size) - margin_sum, id));
                if (rows_axis) stretch.width = known;
                else stretch.height = known;
            }
            const auto sizing = resolve_grid_item_sizing(child_box, stretch,
                GridItemSizingPhase::Intrinsic);
            intrinsic.known_width = sizing.size.width;
            intrinsic.known_height = sizing.size.height;
            own_available = AvailableSpace::min_content();
            const auto minimum = layout_size(id, intrinsic, depth + 1);
            own_available = AvailableSpace::max_content();
            const auto maximum = layout_size(id, intrinsic, depth + 1);
            auto& contribution = contributions[*contribution_indices[i]];
            auto& min_content = rows_axis
                ? contribution.min_content.height : contribution.min_content.width;
            auto& max_content = rows_axis
                ? contribution.max_content.height : contribution.max_content.width;
            auto& minimum_contribution = rows_axis
                ? contribution.minimum_contribution.height
                : contribution.minimum_contribution.width;
            const Unit measured_min = rows_axis
                ? minimum.size.height : minimum.size.width;
            const Unit measured_max = rows_axis
                ? maximum.size.height : maximum.size.width;
            const Unit measured_minimum = grid_item_minimum(child, child_box, place,
                rows_axis ? track_rows : track_columns, measured_min, !rows_axis);
            changed |= min_content != measured_min || max_content != measured_max
                || minimum_contribution != measured_minimum;
            min_content = measured_min;
            max_content = measured_max;
            minimum_contribution = measured_minimum;
            if (rows_axis) {
                changed |= contribution.margin.top != margin.top
                    || contribution.margin.bottom != margin.bottom;
                contribution.margin.top = margin.top;
                contribution.margin.bottom = margin.bottom;
            } else {
                changed |= contribution.margin.left != margin.left
                    || contribution.margin.right != margin.right;
                contribution.margin.left = margin.left;
                contribution.margin.right = margin.right;
            }
        }
        return changed;
    };
    measure_cross_dependent_contributions(true);
    size_axis(track_rows, contributions, false, row_space, node.id);
    const bool intrinsic_columns = std::any_of(track_columns.tracks.begin(),
        track_columns.tracks.end(), [](const TrackState& track) {
            return track.intrinsic_min || track.intrinsic_max;
        });
    if (intrinsic_columns) {
        if (measure_cross_dependent_contributions(false)) {
            // Track base sizes grow monotonically across axis passes. A row
            // estimate can increase an inline contribution, but cyclic
            // percentages must not shrink an established intrinsic base.
            // Definite space is redistributed from the updated contributions.
            if (column_space.grid_space.kind == AvailableKind::Definite) {
                track_columns = make_axis(columns.tracks, node.style.grid.auto_columns,
                    min_column, max_column, initial_track_width, column_gap, node.id);
                collapse_auto_fit(track_columns, columns, placements, true);
            }
            size_axis(track_columns, contributions, true, column_space, node.id);
            if (measure_cross_dependent_contributions(true)) {
                if (row_space.grid_space.kind == AvailableKind::Definite) {
                    track_rows = make_axis(rows.tracks, node.style.grid.auto_rows,
                        min_row, max_row, initial_track_height, row_gap, node.id);
                    collapse_auto_fit(track_rows, rows, placements, false);
                }
                size_axis(track_rows, contributions, false, row_space, node.id);
            }
        }
    }
    const Unit measured_width = track_columns.offsets.empty() ? 0
        : track_columns.offsets.back();
    const Unit measured_height = track_rows.offsets.empty() ? 0
        : track_rows.offsets.back();
    const bool intrinsic_width = !border_width;
    const bool intrinsic_height = !border_height;
    const bool cyclic_columns = intrinsic_width
        && column_space.grid_space.kind != AvailableKind::Definite
        && (has_cyclic_percentage(track_columns)
            || !node.style.gap.width.resolve_numeric(std::nullopt));
    const bool cyclic_rows = intrinsic_height
        && row_space.grid_space.kind != AvailableKind::Definite
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
    const Unit final_column_gap = resolved_gap(node.style.gap.width,
        intrinsic_width && column_space.grid_space.kind == AvailableKind::Definite
            ? initial_track_width
            : cyclic_columns ? std::optional<Unit>{measured_width} : inner_width,
        node.id);
    const Unit final_row_gap = resolved_gap(node.style.gap.height,
        intrinsic_height && row_space.grid_space.kind == AvailableKind::Definite
            ? initial_track_height : inner_height, node.id);
    const bool changed_column_gap = final_column_gap != column_gap;
    const bool changed_row_gap = final_row_gap != row_gap;
    column_gap = final_column_gap;
    row_gap = final_row_gap;
    const auto update_gap = [&](TrackAxis& axis, const ExpandedTemplate& definition,
                                bool column_axis, Unit resolved) {
        axis.gap = resolved;
        collapse_auto_fit(axis, definition, placements, column_axis);
        axis.offsets.assign(axis.tracks.size() + 1, 0);
        for (std::size_t index = 0; index < axis.tracks.size(); ++index) {
            axis.offsets[index + 1] = checked(static_cast<double>(axis.offsets[index])
                + axis.tracks[index].size + axis.gutter_after[index], node.id);
        }
    };
    const bool rerun_columns = intrinsic_width && cyclic_columns;
    if (rerun_columns) {
        const Unit sizing_width = measured_width;
        track_columns = make_axis(columns.tracks, node.style.grid.auto_columns,
            min_column, max_column, sizing_width, column_gap, node.id);
        collapse_auto_fit(track_columns, columns, placements, true);
        auto rerun_space = column_space;
        rerun_space.inner_size = sizing_width;
        size_axis(track_columns, contributions, true, rerun_space, node.id);
    } else if (changed_column_gap) {
        update_gap(track_columns, columns, true, column_gap);
    }
    if (intrinsic_height && cyclic_rows && rerun_columns) {
        track_rows = make_axis(rows.tracks, node.style.grid.auto_rows,
            min_row, max_row, measured_height, row_gap, node.id);
        collapse_auto_fit(track_rows, rows, placements, false);
        auto rerun_space = row_space;
        rerun_space.inner_size = measured_height;
        size_axis(track_rows, contributions, false, rerun_space, node.id);
    } else if (intrinsic_height && cyclic_rows) {
        const auto percentage_value = [height = *inner_height](const TrackBreadth& breadth)
            -> std::optional<Unit> {
            return breadth.kind() == TrackBreadthKind::Percent
                || breadth.kind() == TrackBreadthKind::Calc
                ? breadth.resolve_numeric(height) : std::nullopt;
        };
        for (auto& track : track_rows.tracks) {
            track.size = constrain_axis(track.size,
                percentage_value(track.style.minimum),
                percentage_value(track.style.maximum));
        }
        update_gap(track_rows, rows, false, row_gap);
    } else if (changed_row_gap) {
        update_gap(track_rows, rows, false, row_gap);
    }
    Size candidate{*border_width, *border_height};
    candidate = constrain_size(candidate, box);
    if (constraints.preserve_known_grid_size) {
        // Parent Grid item sizing already chose the reported border box. The
        // child's own min/max constraints still sized its internal tracks.
        if (constraints.known_width) candidate.width = *constraints.known_width;
        if (constraints.known_height) candidate.height = *constraints.known_height;
    }
    if (!constraints.preserve_known_grid_size) {
        inner_width = std::max(Unit{0}, candidate.width - *decoration_width);
        inner_height = std::max(Unit{0}, candidate.height - *decoration_height);
    }
    if (detail_ == FragmentDetail::Size) {
        Fragment result;
        result.size = candidate;
        result.nodes.push_back(make_layout(node, candidate, box));
        return result;
    }
    const Unit positioning_width = (constraints.preserve_known_grid_size
        ? *border_width : candidate.width) - *decoration_width;
    const Unit positioning_height = (constraints.preserve_known_grid_size
        ? *border_height : candidate.height) - *decoration_height;
    align_tracks(track_columns, positioning_width,
        node.style.justify_content.value_or(ContentAlignment{}), node.id);
    align_tracks(track_rows, positioning_height,
        node.style.align_content.value_or(ContentAlignment{}), node.id);
    Fragment result;
    result.size = candidate;
    result.nodes.push_back(make_layout(node, candidate, box));
    if (constraints.preserve_known_grid_size && *border_width != candidate.width) {
        result.nodes.front().overflow_coordinate_width = *border_width;
    }
    const auto border = edges(box.border, node.id);
    const auto padding = edges(box.padding, node.id);
    const Unit content_x = content_origin_x(box, border, padding, node.id);
    const Unit content_y = checked(static_cast<double>(border.top) + padding.top, node.id);
    if (detail_ == FragmentDetail::Full) {
        GridTracks report;
        report.node = node.id;
        report.column_origin = track_columns.origin;
        report.row_origin = track_rows.origin;
        report.explicit_columns = columns.tracks.size();
        report.explicit_rows = rows.tracks.size();
        report.columns.reserve(track_columns.tracks.size());
        report.rows.reserve(track_rows.tracks.size());
        report.column_positions.reserve(track_columns.tracks.size());
        report.row_positions.reserve(track_rows.tracks.size());
        report.items.reserve(node.children.size());
        for (std::size_t i = 0; i < track_columns.tracks.size(); ++i) {
            const auto size = track_columns.tracks[i].size;
            report.columns.push_back(size);
            const auto inline_start = track_columns.offsets[i];
            const auto start = physical_track_start(content_x, positioning_width,
                inline_start, size, node.style.box.direction, node.id);
            report.column_positions.push_back({start,
                checked(static_cast<double>(start) + size, node.id)});
        }
        if (node.style.box.direction == Direction::Rtl
            && std::any_of(track_columns.tracks.begin(), track_columns.tracks.end(),
                [](const TrackState& track) { return !track.collapsed; })) {
            Unit physical_cursor = content_x;
            for (std::size_t i = track_columns.tracks.size(); i-- > 0;) {
                if (track_columns.tracks[i].collapsed) {
                    report.column_positions[i] = {physical_cursor, physical_cursor};
                } else {
                    physical_cursor = report.column_positions[i].end;
                }
            }
        }
        for (std::size_t i = 0; i < track_rows.tracks.size(); ++i) {
            const auto size = track_rows.tracks[i].size;
            report.rows.push_back(size);
            const auto start = checked(static_cast<double>(content_y)
                + track_rows.offsets[i], node.id);
            report.row_positions.push_back({start,
                checked(static_cast<double>(start) + size, node.id)});
        }
        for (std::size_t i = 0; i < placements.size(); ++i) {
            const auto& placed = placements[i];
            if (placed.hidden) continue;
            report.items.push_back({node.children[i], placed.row_start, placed.row_end,
                placed.column_start, placed.column_end});
        }
        result.grid_tracks.push_back(std::move(report));
    }
    std::vector<Fragment> fragments(node.children.size());
    struct BaselinePlacement {
        std::size_t source;
        std::int32_t row;
        Unit baseline;
        Unit margin_top;
    };
    std::vector<BaselinePlacement> baseline_items;
    std::unordered_map<std::int32_t, Unit> row_baselines;
    for (std::size_t i = 0; i < node.children.size(); ++i) {
        const auto id = node.children[i];
        const auto& child = get(id);
        if (child.style.position == Position::Absolute
            && child.style.display != Display::None) {
            Rect area{checked(static_cast<double>(border.left)
                    + (node.style.box.direction == Direction::Rtl
                        ? box.scrollbar.width : 0), node.id),
                border.top,
                std::max(Unit{0}, candidate.width - border.left - border.right
                    - box.scrollbar.width),
                std::max(Unit{0}, candidate.height - border.top - border.bottom
                    - box.scrollbar.height)};
            area = resolve_absolute_grid_area(absolute_rows[i], absolute_columns[i],
                track_rows, track_columns, area,
                {content_x, content_y, positioning_width, positioning_height},
                node.style.box.direction, id);
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
            ItemAlignment{resolved.preferred.width ? ItemPosition::Start
                : ItemPosition::Stretch, AlignmentSafety::Unsafe}));
        const auto vertical = child.style.align_self.value_or(node.style.align_items.value_or(
            ItemAlignment{resolved.aspect_ratio ? ItemPosition::Start
                : ItemPosition::Stretch, AlignmentSafety::Unsafe}));
        const auto& contribution = contributions[contribution_indices[i].value()];
        const bool intrinsic_grid_item = child.style.display == Display::Grid;
        ResolvedDimensions stretch;
        if (horizontal.position == ItemPosition::Stretch
            && child_box.size.width.kind() == LengthKind::Auto
            && resolved.margin.left.kind != EdgeKind::Auto
            && resolved.margin.right.kind != EdgeKind::Auto) {
            stretch.width = std::max(Unit{0}, checked(
                static_cast<double>(cell_width) - margin.left - margin.right, id));
        }
        if (!stretch.width && child_box.size.width.kind() == LengthKind::Auto
            && !resolved.preferred.width
            && (child.style.display == Display::Block
                || child.style.display == Display::FlowRoot)) {
            final.content_based_width = true;
        }
        if (vertical.position == ItemPosition::Stretch
            && child_box.size.height.kind() == LengthKind::Auto
            && resolved.margin.top.kind != EdgeKind::Auto
            && resolved.margin.bottom.kind != EdgeKind::Auto) {
            stretch.height = std::max(Unit{0}, checked(
                static_cast<double>(cell_height) - margin.top - margin.bottom, id));
        }
        const auto sizing = resolve_grid_item_sizing(resolved, stretch,
            GridItemSizingPhase::Final);
        final.known_width = sizing.size.width;
        final.known_height = sizing.size.height;
        // Content-fit dimensions come from measurement after stretch and
        // ratio resolution. Feeding them back into ratio transfer would turn
        // an auto-sized Grid container's independent content axes into a
        // preferred ratio, changing its internal rows.
        if (intrinsic_grid_item) {
            if (!final.known_width && child_box.size.width.kind() == LengthKind::Auto) {
                final.known_width = constrain_axis(fit_content_item_size(
                    contribution.min_content.width, contribution.max_content.width,
                    cell_width, margin.left, margin.right, id),
                    sizing.minimum.width, sizing.maximum.width);
            }
            if (!final.known_height && child_box.size.height.kind() == LengthKind::Auto) {
                final.known_height = constrain_axis(fit_content_item_size(
                    contribution.min_content.height, contribution.max_content.height,
                    cell_height, margin.top, margin.bottom, id),
                    sizing.minimum.height, sizing.maximum.height);
            }
        }
        if (resolved.aspect_ratio && final.known_width && final.known_height) {
            // The Grid item sizing stage has resolved these constraints. Its
            // final dimensions are passed to the child as known dimensions.
            if (child.style.display == Display::Grid) {
                final.preserve_known_grid_size = true;
            } else {
                final.ignore_min_max_width = true;
                final.ignore_min_max_height = true;
            }
        }
        auto child_fragment = layout(id, final, depth + 1);
        finalize_grid_item_fragment(child_fragment, resolved, sizing,
            {final.known_width, final.known_height});
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
        const Unit inline_start_margin = node.style.box.direction == Direction::Rtl
            ? used_right : used_left;
        Unit x = checked(static_cast<double>(cell_x) + inline_start_margin
            + ((auto_left || auto_right) ? 0 : align_offset(horizontal_free,
                horizontal, true, node.style.box.direction,
                child.style.box.direction, id)), id);
        Unit y = checked(static_cast<double>(cell_y) + used_top
            + ((auto_top || auto_bottom) ? 0 : align_offset(vertical_free,
                vertical, false, node.style.box.direction,
                child.style.box.direction, id)), id);
        child_fragment.nodes.front().margin = {used_left, used_top, used_right, used_bottom};
        if (node.style.box.direction == Direction::Rtl) {
            x = checked(static_cast<double>(positioning_width) - x - child_fragment.size.width, id);
        }
        translate(child_fragment,
            checked(static_cast<double>(content_x) + x, id),
            checked(static_cast<double>(content_y) + y, id), id);
        const auto [relative_x, relative_y] = relative_offset(child,
            Size{cell_width, cell_height}, node.style.box.direction, node.id);
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
        if (detail_ == FragmentDetail::Full) {
            result.nodes.insert(result.nodes.end(),
                std::make_move_iterator(fragment.nodes.begin()),
                std::make_move_iterator(fragment.nodes.end()));
            result.grid_tracks.insert(result.grid_tracks.end(),
                std::make_move_iterator(fragment.grid_tracks.begin()),
                std::make_move_iterator(fragment.grid_tracks.end()));
        }
    }
    return result;
}

} // namespace axisly::css::detail
