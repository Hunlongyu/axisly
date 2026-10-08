#include "grid_internal.hpp"

namespace axisly::css::detail {

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
    if (node.grid_structure && node.grid_structure->key == key) return node.grid_structure;
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
        node.style.grid.auto_flow, node.id);
    node.grid_structure = result;
    return result;
}

} // namespace axisly::css::detail
