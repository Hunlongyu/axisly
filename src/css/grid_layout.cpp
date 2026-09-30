#include "grid_internal.hpp"
#include "grid_item_sizing.hpp"
#include "grid_item_contribution.hpp"
#include "item_alignment.hpp"

#include <algorithm>
#include <cstdint>
#include <iterator>
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
    if (detail_ == FragmentDetail::Size && border_width && border_height) {
        Fragment result;
        result.size = constrain_size(Size{*border_width, *border_height}, box);
        if (constraints.preserve_known_grid_size) {
            if (constraints.known_width) result.size.width = *constraints.known_width;
            if (constraints.known_height) result.size.height = *constraints.known_height;
        }
        result.root = make_layout(node, result.size, box);
        return result;
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
    auto baseline_items = measure_grid_baselines(node, placements, {inner_width, inner_height}, depth);
    std::vector<ItemContribution> contributions;
    contributions.reserve(node.children.size());
    std::vector<std::optional<std::size_t>> contribution_indices(node.children.size());
    for (std::size_t i = 0; i < node.children.size(); ++i) {
        if (placements[i].hidden) continue;
        const auto id = node.children[i];
        const auto axis = grid_axis_contribution(id,
            {node, placements[i], track_columns, track_rows, {inner_width, inner_height},
                true, GridCrossSizing::MaximumEstimate, baseline_items[i].shim}, depth);
        const auto scrolls = [](Overflow overflow) {
            return overflow == Overflow::Hidden || overflow == Overflow::Scroll;
        };
        contribution_indices[i] = contributions.size();
        contributions.push_back({placements[i], {axis.minimum, 0},
            {axis.min_content, 0}, {axis.max_content, 0},
            {axis.margin_start, 0, axis.margin_end, 0},
            scrolls(get(id).style.overflow_x), scrolls(get(id).style.overflow_y)});
    }
    size_axis(track_columns, contributions, true, column_space, node.id);
    Unit measured_width = track_columns.offsets.back();
    ResolvedDimensions provisional_inner{
        inner_width.value_or(track_columns.offsets.back()), inner_height};
    // An axis contribution sees only definite tracks in its own grid area.
    // The opposite axis uses either its initial maximum estimate or the
    // provisional sizes from the previous track pass.
    const auto measure_cross_dependent_contributions = [&](bool rows_axis) {
        bool changed = false;
        for (std::size_t i = 0; i < node.children.size(); ++i) {
            if (!contribution_indices[i]) continue;
            const auto axis = grid_axis_contribution(node.children[i],
                {node, placements[i], track_columns, track_rows, provisional_inner,
                    !rows_axis, GridCrossSizing::ResolvedTracks, baseline_items[i].shim}, depth);
            auto& contribution = contributions[*contribution_indices[i]];
            auto& minimum = rows_axis ? contribution.minimum_contribution.height
                : contribution.minimum_contribution.width;
            auto& min_content = rows_axis ? contribution.min_content.height : contribution.min_content.width;
            auto& max_content = rows_axis ? contribution.max_content.height : contribution.max_content.width;
            auto& margin_start = rows_axis ? contribution.margin.top : contribution.margin.left;
            auto& margin_end = rows_axis ? contribution.margin.bottom : contribution.margin.right;
            changed |= minimum != axis.minimum || min_content != axis.min_content
                || max_content != axis.max_content || margin_start != axis.margin_start
                || margin_end != axis.margin_end;
            minimum = axis.minimum;
            min_content = axis.min_content;
            max_content = axis.max_content;
            margin_start = axis.margin_start;
            margin_end = axis.margin_end;
        }
        return changed;
    };
    measure_cross_dependent_contributions(true);
    size_axis(track_rows, contributions, false, row_space, node.id);
    Unit measured_height = track_rows.offsets.back();
    provisional_inner.height = inner_height.value_or(track_rows.offsets.back());
    if (detail_ == FragmentDetail::Size) {
        Size measured{
            border_width.value_or(checked(static_cast<double>(measured_width) + *decoration_width, node.id)),
            border_height.value_or(checked(static_cast<double>(measured_height) + *decoration_height, node.id))};
        measured = constrain_size(measured, box);
        if (constraints.preserve_known_grid_size) {
            if (constraints.known_width) measured.width = *constraints.known_width;
            if (constraints.known_height) measured.height = *constraints.known_height;
        }
        Fragment result;
        result.size = measured;
        result.root = make_layout(node, measured, box);
        return result;
    }
    const bool intrinsic_columns = std::any_of(track_columns.tracks.begin(),
        track_columns.tracks.end(), [](const TrackState& track) {
            return track.intrinsic_min || track.intrinsic_max;
        });
    if (intrinsic_columns && measure_cross_dependent_contributions(false)) {
        // Reflow can reduce an intrinsic contribution. Reinitialize the
        // affected axis so an earlier estimate does not become a permanent
        // lower bound. The selected container size remains independent.
        track_columns = make_axis(columns.tracks, node.style.grid.auto_columns,
            min_column, max_column, initial_track_width, column_gap, node.id);
        collapse_auto_fit(track_columns, columns, placements, true);
        auto reflow_column_space = column_space;
        reflow_column_space.inner_size = provisional_inner.width;
        size_axis(track_columns, contributions, true, reflow_column_space, node.id);
        if (!has_cyclic_percentage(track_columns)) measured_width = track_columns.offsets.back();
        if (measure_cross_dependent_contributions(true)) {
            track_rows = make_axis(rows.tracks, node.style.grid.auto_rows,
                min_row, max_row, initial_track_height, row_gap, node.id);
            collapse_auto_fit(track_rows, rows, placements, false);
            auto reflow_row_space = row_space;
            reflow_row_space.inner_size = provisional_inner.height;
            size_axis(track_rows, contributions, false, reflow_row_space, node.id);
            if (!has_cyclic_percentage(track_rows)) measured_height = track_rows.offsets.back();
        }
    }
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
    // A cyclic gutter participates in reflow only between surviving tracks.
    // A single-column grid has no such gutter, even when its gap is cyclic.
    const bool rerun_columns = intrinsic_width && cyclic_columns
        && (has_cyclic_percentage(track_columns)
            || std::count_if(track_columns.tracks.begin(), track_columns.tracks.end(),
                [](const TrackState& track) { return !track.collapsed; }) > 1);
    if (rerun_columns) {
        const Unit sizing_width = measured_width;
        track_columns = make_axis(columns.tracks, node.style.grid.auto_columns,
            min_column, max_column, sizing_width, column_gap, node.id);
        collapse_auto_fit(track_columns, columns, placements, true);
        provisional_inner.width = sizing_width;
        measure_cross_dependent_contributions(false);
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
    result.root = make_layout(node, candidate, box);
    if (constraints.preserve_known_grid_size && *border_width != candidate.width) {
        result.root_layout().overflow_coordinate_width = *border_width;
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
        report.column_line_names = reported_grid_line_names(columns, track_columns);
        report.row_line_names = reported_grid_line_names(rows, track_rows);
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
        auto margin = edges(resolved.margin, id);
        margin.top = checked(static_cast<double>(margin.top) + baseline_items[i].shim, id);
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
        child_fragment.root_layout().margin = {used_left, used_top, used_right, used_bottom};
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
    std::optional<std::size_t> first_item;
    for (std::size_t source = 0; source < placements.size(); ++source) {
        if (placements[source].hidden
            || get(node.children[source]).style.position == Position::Absolute) continue;
        if (!first_item || std::pair{placements[source].row_start, placements[source].column_start}
            < std::pair{placements[*first_item].row_start, placements[*first_item].column_start}) {
            first_item = source;
        }
    }
    if (first_item) {
        std::optional<std::size_t> aligned;
        for (std::size_t i = 0; i < placements.size(); ++i) {
            if (!placements[i].hidden && placements[i].row_start == placements[*first_item].row_start
                && grid_baseline_participant(node, get(node.children[i]))) {
                if (!aligned || placements[i].column_start < placements[*aligned].column_start) aligned = i;
            }
        }
        const auto source = aligned.value_or(*first_item);
        const auto& fragment = fragments[source];
        const Unit baseline = baseline_items[source].ascent.value_or(fragment.size.height);
        result.root_layout().first_baseline = checked(
            static_cast<double>(fragment.root_layout().border_box.y)
                + baseline, node.id);
    }
    if (detail_ == FragmentDetail::Full) result.reserve_children(fragments.size());
    for (auto& fragment : fragments) {
        if (detail_ == FragmentDetail::Full) {
            result.append_nodes(std::move(fragment));
            result.grid_tracks.insert(result.grid_tracks.end(),
                std::make_move_iterator(fragment.grid_tracks.begin()),
                std::make_move_iterator(fragment.grid_tracks.end()));
        }
    }
    return result;
}

} // namespace axisly::css::detail
