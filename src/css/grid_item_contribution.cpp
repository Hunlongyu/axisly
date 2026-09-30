#include "grid_item_contribution.hpp"
#include "grid_item_sizing.hpp"

#include <algorithm>
#include <unordered_map>

namespace axisly::css::detail {
namespace {

std::optional<Unit> definite_grid_area(const TrackAxis& axis,
    std::int32_t start, std::int32_t end, std::optional<Unit> basis,
    bool own_axis, GridCrossSizing cross_sizing, NodeId id) {
    double sum = 0;
    for (auto index = start; index < end; ++index) {
        const auto slot = static_cast<std::size_t>(index - axis.origin);
        const auto& track = axis.tracks[slot];
        if (!track.collapsed) {
            if (own_axis) {
                const auto minimum = track.style.minimum.resolve_numeric(basis);
                const auto maximum = track.style.maximum.resolve_numeric(basis);
                if (!minimum || !maximum || *minimum != *maximum) return std::nullopt;
                sum += track.size;
            } else if (cross_sizing == GridCrossSizing::ResolvedTracks) {
                sum += track.size;
            } else {
                const auto maximum = track.style.maximum.resolve_numeric(basis);
                if (!maximum) return std::nullopt;
                sum += *maximum;
            }
        }
        if (index + 1 < end) sum += axis.gutter_after[slot];
    }
    return checked(sum, id);
}

ItemAlignment item_alignment(const LayoutNode& parent, const LayoutNode& child, bool columns) {
    return columns ? child.style.justify_self.value_or(parent.style.justify_items.value_or(
        ItemAlignment{ItemPosition::Stretch, AlignmentSafety::Unsafe}))
        : child.style.align_self.value_or(parent.style.align_items.value_or(
            ItemAlignment{ItemPosition::Stretch, AlignmentSafety::Unsafe}));
}

std::optional<Unit> intrinsic_stretch(const Length& style, ItemAlignment alignment,
    const ResolvedEdge& start, const ResolvedEdge& end, std::optional<Unit> area,
    NodeId id) {
    if (!area || start.kind == EdgeKind::Auto || end.kind == EdgeKind::Auto) return std::nullopt;
    if (style.kind() != LengthKind::Stretch
        && (style.kind() != LengthKind::Auto || alignment.position != ItemPosition::Stretch)) {
        return std::nullopt;
    }
    return std::max(Unit{0}, checked(static_cast<double>(*area) - start.value - end.value, id));
}
} // namespace

bool grid_baseline_participant(const LayoutNode& parent, const LayoutNode& child) {
    return item_alignment(parent, child, false).position == ItemPosition::Baseline
        && child.style.box.margin.top.kind() != LengthKind::Auto
        && child.style.box.margin.bottom.kind() != LengthKind::Auto;
}

GridAxisContribution Engine::grid_axis_contribution(NodeId id,
    const GridContributionInput& input, std::size_t depth) {
    const auto& child = get(id);
    const auto& place = input.placement;
    const ResolvedDimensions area{
        definite_grid_area(input.columns, place.column_start, place.column_end,
            input.container_inner.width, input.columns_axis, input.cross_sizing, id),
        definite_grid_area(input.rows, place.row_start, place.row_end,
            input.container_inner.height, !input.columns_axis, input.cross_sizing, id)};
    ResolvedBox box;
    try { box = resolve_layout_box(child, area.width, area.height); }
    catch (const std::invalid_argument& error) {
        throw LayoutFailure(LayoutErrorCode::InvalidStyle, id, error.what());
    }
    const ResolvedDimensions stretch{
        intrinsic_stretch(child.style.box.size.width, item_alignment(input.parent, child, true),
            box.margin.left, box.margin.right, area.width, id),
        intrinsic_stretch(child.style.box.size.height, item_alignment(input.parent, child, false),
            box.margin.top, box.margin.bottom, area.height, id)};
    const auto sizing = resolve_grid_item_sizing(box, stretch, GridItemSizingPhase::Intrinsic);
    Constraints constraints;
    constraints.containing_width = area.width;
    constraints.containing_height = area.height;
    constraints.known_width = sizing.size.width;
    constraints.known_height = sizing.size.height;
    const auto cross_space = input.columns_axis ? area.height : area.width;
    auto query = [&](AvailableSpace available) {
        constraints.available = {available, available};
        if (cross_space) {
            (input.columns_axis ? constraints.available.height : constraints.available.width)
                = AvailableSpace::definite(*cross_space);
        }
        return layout_size(id, constraints, depth + 1).size;
    };
    const Size minimum = query(AvailableSpace::min_content());
    const Size maximum = query(AvailableSpace::max_content());
    const Unit min_content = input.columns_axis ? minimum.width : minimum.height;
    const Unit max_content = input.columns_axis ? maximum.width : maximum.height;
    const Unit minimum_size = grid_item_minimum(child, box, place,
        input.columns_axis ? input.columns : input.rows, min_content, input.columns_axis);
    const auto& margin = child.style.box.margin;
    // Inline percentage margins never contribute to intrinsic track widths.
    // Block margins see the provisional grid-area inline width.
    const Unit start = input.columns_axis
        ? margin.left.resolve_numeric(Unit{0}).value_or(Unit{0})
        : checked(static_cast<double>(margin.top.resolve_numeric(area.width).value_or(Unit{0}))
            + input.baseline_shim, id);
    const Unit end = input.columns_axis
        ? margin.right.resolve_numeric(Unit{0}).value_or(Unit{0})
        : margin.bottom.resolve_numeric(area.width).value_or(Unit{0});
    return {minimum_size, min_content, max_content, start, end};
}

std::vector<GridBaselineItem> Engine::measure_grid_baselines(const LayoutNode& parent,
    const std::vector<Placement>& placements, ResolvedDimensions container_inner,
    std::size_t depth) {
    std::vector<GridBaselineItem> result(placements.size());
    std::unordered_map<std::int32_t, std::vector<std::size_t>> groups;
    for (std::size_t i = 0; i < placements.size(); ++i) {
        if (placements[i].hidden) continue;
        const auto& child = get(parent.children[i]);
        if (!grid_baseline_participant(parent, child)) continue;
        groups[placements[i].row_start].push_back(i);
    }
    for (const auto& [row, indices] : groups) {
        (void)row;
        if (indices.size() <= 1) continue;
        std::vector<Unit> baselines;
        baselines.reserve(indices.size());
        std::optional<Unit> largest;
        for (const auto source : indices) {
            const auto& child = get(parent.children[source]);
            Constraints constraints;
            constraints.containing_width = container_inner.width;
            constraints.containing_height = container_inner.height;
            constraints.available = {AvailableSpace::min_content(), AvailableSpace::min_content()};
            const auto fragment = layout_sizing(child.id, constraints, depth + 1);
            const Unit baseline = checked(static_cast<double>(child_baseline(fragment, child.style)
                .value_or(fragment.size.height)) + child.style.box.margin.top
                    .resolve_numeric(container_inner.width).value_or(Unit{0}), child.id);
            baselines.push_back(baseline);
            largest = std::max(largest.value_or(baseline), baseline);
        }
        for (std::size_t i = 0; i < indices.size(); ++i) {
            result[indices[i]] = {checked(static_cast<double>(*largest) - baselines[i], parent.id), baselines[i]};
        }
    }
    return result;
}
} // namespace axisly::css::detail
