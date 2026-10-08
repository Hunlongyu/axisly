#include <axisly/axisly.hpp>
#include "console_session.hpp"

#include <cstdio>
#include <stdexcept>

int main() {
    axisly::examples::ConsoleSession console;
    namespace css = axisly::css;
    css::LayoutTree tree;
    css::Style grid;
    grid.display = css::Display::Grid;
    grid.box.size = {css::Length::length(640), css::Length::length(240)};
    grid.gap = {css::Length::length(12), css::Length::length(8)};
    grid.grid.template_columns = {css::TrackSizing::fixed(160),
        css::TrackSizing::minmax(css::TrackBreadth::length(100), css::TrackBreadth::fraction(1))};
    grid.grid.template_rows = {css::TrackSizing::automatic(), css::TrackSizing::fraction(1)};
    const auto root = tree.create_node(grid);
    css::Style title;
    title.grid.column = {css::GridLine::index(1), css::GridLine::span(2)};
    title.grid.row.start = css::GridLine::index(1);
    float title_width = 220;
    const auto header = tree.create_node(title, [&](const css::MeasureRequest& request) {
        return css::Measurement{{request.known_content_width.value_or(title_width), 32}, 24, 24};
    });
    tree.append_child(root, header);
    css::Style sidebar;
    sidebar.grid.row.start = css::GridLine::index(2);
    sidebar.grid.column.start = css::GridLine::index(1);
    sidebar.justify_self = css::ItemAlignment{css::ItemPosition::Center};
    sidebar.box.size.width = css::Length::length(80);
    tree.append_child(root, tree.create_node(sidebar));
    // Nested grids use the same tree and the same CSS algorithm.
    css::Style nested;
    nested.display = css::Display::Grid;
    nested.grid.column.start = css::GridLine::index(2);
    nested.grid.row.start = css::GridLine::index(2);
    nested.grid.template_columns = {css::TrackSizing::fraction(1), css::TrackSizing::fraction(2)};
    const auto panel = tree.create_node(nested);
    tree.append_child(root, panel);
    tree.append_child(panel, tree.create_node());
    tree.append_child(panel, tree.create_node());
    const css::AvailableSize available{css::AvailableSpace::definite(640), css::AvailableSpace::definite(240)};
    const auto& result = tree.compute(root, available, axisly::PixelRounding{1.25F});
    if (!result.succeeded() || result.nodes.size() != 6 || result.grid_tracks.size() != 2) return 1;
    for (const auto& item : result.nodes) {
        const auto& r = item.border_box;
        std::printf("%llu: %.1f %.1f %.1f %.1f\n", static_cast<unsigned long long>(item.node.value), r.x, r.y, r.width, r.height);
    }
    if (!tree.compute(root, available, axisly::PixelRounding{1.25F}).cache_hit) return 2;
    title_width = 260;
    tree.mark_dirty(header);
    return tree.compute(root, available).succeeded() ? 0 : 3;
}
