#include "grid_internal.hpp"
#include "grid_metrics.hpp"

#include <algorithm>

namespace axisly::css::detail {
namespace {
void auto_fit_mask(std::pmr::vector<std::uint8_t>& mask, const ExpandedTemplate& definition,
    std::span<const Placement> placements, bool columns) {
    if (std::none_of(definition.auto_fit.begin(), definition.auto_fit.end(), [](bool value) { return value; })) return;
    const auto count = static_cast<std::int32_t>(definition.auto_fit.size());
    std::pmr::vector<std::int64_t> changes(static_cast<std::size_t>(count) + 1, mask.get_allocator().resource());
    for (const auto& placement : placements) {
        AXISLY_GRID_COUNT(auto_fit_visits, 1);
        if (placement.hidden) continue;
        const auto start = std::clamp(columns ? placement.column_start : placement.row_start, 0, count);
        const auto end = std::clamp(columns ? placement.column_end : placement.row_end, 0, count);
        ++changes[static_cast<std::size_t>(start)];
        --changes[static_cast<std::size_t>(end)];
    }
    mask.resize(definition.auto_fit.size());
    std::int64_t occupied = 0;
    for (std::size_t i = 0; i < mask.size(); ++i) {
        occupied += changes[i];
        mask[i] = static_cast<std::uint8_t>(definition.auto_fit[i] && occupied == 0);
    }
}
} // namespace

std::shared_ptr<const GridStructure> Engine::resolve_grid_structure(
    const LayoutNode& node, GridStructureKey key) const {
    if (!node.grid_columns_auto_repeat) {
        key.width.reset();
        key.column_gap = 0;
        key.minimum_width = false;
    }
    if (!node.grid_rows_auto_repeat) {
        key.height.reset();
        key.row_gap = 0;
        key.minimum_height = false;
    }
    if (node.grid_structure) {
        auto& cache = *node.grid_structure;
        if (cache.previous && cache.previous->key == key) std::swap(cache.recent, cache.previous);
        if (cache.recent && cache.recent->key == key) {
            AXISLY_GRID_COUNT(structure_hits, 1);
            return cache.recent;
        }
    }
    AXISLY_GRID_COUNT(structure_builds, 1);
    auto result = std::make_shared<GridStructure>(node.cache_owner->memory());
    result->key = key;
    result->columns = expand_grid_template(node.style.grid.template_columns,
        node.style.grid.column_line_names, key.width, key.column_gap, key.minimum_width, node.id);
    result->rows = expand_grid_template(node.style.grid.template_rows,
        node.style.grid.row_line_names, key.height, key.row_gap, key.minimum_height, node.id);
    apply_grid_template_areas(node.style.grid, result->rows, result->columns);
    const auto& rows = result->rows;
    const auto& columns = result->columns;
    result->placements.resize(node.layout_children().size());
    result->absolute_rows.resize(node.layout_children().size());
    result->absolute_columns.resize(node.layout_children().size());
    for (std::size_t i = 0; i < node.layout_children().size(); ++i) {
        const auto& child = get(node.layout_children()[i]);
        auto& placement = result->placements[i];
        if (child.style.display == Display::None) { placement.hidden = true; continue; }
        if (child.style.position == Position::Absolute) {
            placement.hidden = true;
            result->absolute_rows[i] = resolve_absolute_grid_placement(child.style.grid.row, rows);
            result->absolute_columns[i] = resolve_absolute_grid_placement(child.style.grid.column, columns);
            continue;
        }
        placement.row = resolve_grid_placement(child.style.grid.row, rows);
        placement.column = resolve_grid_placement(child.style.grid.column, columns);
        placement.row_estimate = estimate_grid_axis(child.style.grid.row,
            static_cast<std::int32_t>(rows.tracks.size()));
        placement.column_estimate = estimate_grid_axis(child.style.grid.column,
            static_cast<std::int32_t>(columns.tracks.size()));
    }
    result->extent = place_grid_items(result->placements,
        static_cast<std::int32_t>(rows.tracks.size()), static_cast<std::int32_t>(columns.tracks.size()),
        node.style.grid.auto_flow, node.id, node.cache_owner->memory());
    auto_fit_mask(result->collapsed_columns, columns, result->placements, true);
    auto_fit_mask(result->collapsed_rows, rows, result->placements, false);
    if (!node.grid_structure) node.grid_structure = std::make_shared<GridStructureCache>();
    auto& cache = *node.grid_structure;
    if (node.grid_columns_auto_repeat || node.grid_rows_auto_repeat) cache.previous = std::move(cache.recent);
    cache.recent = result;
    return result;
}

} // namespace axisly::css::detail
