#include <axisly/css_source.hpp>
#include "../../src/css/grid_metrics.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
using namespace axisly;
using namespace axisly::css;
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
struct Cell {
    Style style;
    std::optional<NodeId> parent;
    std::vector<NodeId> children;
};
struct Host final : LayoutSource {
    std::array<Cell, 5> cells;
    std::optional<LayoutNodeView> node(NodeId id) const override {
        if (!id.value || id.value > cells.size()) return std::nullopt;
        const auto& c = cells[static_cast<std::size_t>(id.value - 1)];
        return LayoutNodeView{c.style, c.children, c.parent};
    }
};
void reset_metrics() {
#ifdef AXISLY_ENABLE_GRID_METRICS
    detail::grid_metrics = {};
#endif
}
void require_builds(std::uint64_t expected) {
#ifdef AXISLY_ENABLE_GRID_METRICS
    require(detail::grid_metrics.structure_builds == expected, "Unexpected Grid structure rebuild");
#else
    (void)expected;
#endif
}
std::array<NodeId, 5> populate(LayoutTree& tree, const Host& host) {
    std::array<NodeId, 5> ids;
    for (std::size_t i = 0; i < ids.size(); ++i) ids[i] = tree.create_node(host.cells[i].style);
    for (std::size_t i = 0; i < ids.size(); ++i) {
        std::vector<NodeId> children;
        for (const auto id : host.cells[i].children) children.push_back(ids[static_cast<std::size_t>(id.value - 1)]);
        tree.set_children(ids[i], children);
    }
    return ids;
}
void compare(LayoutResult actual, const LayoutResult& expected,
    const std::array<NodeId, 5>& actual_ids, const std::array<NodeId, 5>& expected_ids) {
    const auto remap = [&](NodeId id) {
        const auto found = std::find(actual_ids.begin(), actual_ids.end(), id);
        require(found != actual_ids.end(), "Unknown published identity");
        return expected_ids[static_cast<std::size_t>(found - actual_ids.begin())];
    };
    for (auto& n : actual.nodes) { n.node = remap(n.node); if (n.parent) n.parent = remap(*n.parent); }
    for (auto& g : actual.grid_tracks) {
        g.node = remap(g.node);
        for (auto& p : g.items) p.node = remap(p.node);
    }
    require(actual.succeeded() && expected.succeeded() && actual.size == expected.size
        && actual.nodes == expected.nodes && actual.grid_tracks == expected.grid_tracks,
        "Retained Grid differs from reconstruction");
}
void mutation_contract(bool borrowed) {
    Host host;
    auto& root = host.cells[0];
    root.style.display = Display::Grid;
    root.style.grid.template_columns = {TrackSizing::fixed(30), TrackSizing::fixed(40)};
    root.style.grid.auto_rows = {TrackSizing::automatic()};
    root.children = {{2}, {3}};
    host.cells[1].parent = host.cells[2].parent = NodeId{1};
    host.cells[1].children = {{4}};
    host.cells[2].children = {{5}};
    host.cells[3].parent = NodeId{2}; host.cells[4].parent = NodeId{3};
    host.cells[3].style.box.size = {Length::length(12), Length::length(9)};
    host.cells[4].style.box.size = {Length::length(18), Length::length(11)};
    LayoutTree tree;
    LayoutSession session(host);
    const auto ids = populate(tree, host);
    const std::array<NodeId, 5> host_ids{NodeId{1}, NodeId{2}, NodeId{3}, NodeId{4}, NodeId{5}};
    const AvailableSize space{AvailableSpace::definite(100), AvailableSpace::max_content()};
    const auto check = [&](std::uint64_t builds) {
        reset_metrics();
        auto result = borrowed ? session.compute({1}, space) : tree.compute(ids[0], space);
        require_builds(builds);
        LayoutTree fresh;
        const auto new_ids = populate(fresh, host);
        compare(std::move(result), fresh.compute(new_ids[0], space), borrowed ? host_ids : ids, new_ids);
    };
    const auto update = [&](std::size_t index) {
        if (borrowed) session.mark_dirty(host_ids[index]);
        else tree.set_style(ids[index], host.cells[index].style);
    };
    check(1);
    host.cells[3].style.box.size.height = Length::length(37);
    update(3); check(0); // Deep size change must retain outer placement.
    host.cells[1].style.box.padding.left = Length::length(3);
    update(1); check(borrowed ? 1 : 0);
    host.cells[1].style.grid.column.start = GridLine::index(2);
    update(1); check(1);
    host.cells[1].style.display = Display::None;
    update(1); check(1);
    host.cells[1].style.display = Display::Block;
    host.cells[1].style.position = Position::Absolute;
    update(1); check(1);
    host.cells[1].style.position = Position::Relative;
    update(1); check(1);
    root.style.grid.template_columns = {TrackSizing::fixed(21), TrackSizing::fixed(53)};
    update(0); check(1);
    std::reverse(root.children.begin(), root.children.end());
    if (borrowed) session.invalidate();
    else { const std::array children{ids[2], ids[1]}; tree.set_children(ids[0], children); }
    check(1);
    // A grandchild moves between direct children without changing root placement.
    host.cells[1].children.clear(); host.cells[2].children.push_back({4}); host.cells[3].parent = NodeId{3};
    if (borrowed) session.invalidate(); else tree.append_child(ids[2], ids[3]);
    check(borrowed ? 1 : 0);
}
void repeat_cache() {
    LayoutTree tree;
    Style grid;
    grid.display = Display::Grid;
    grid.box.size.width = Length::percent(1);
    grid.grid.template_columns = {GridRepeat{RepeatKind::AutoFill, 1, {TrackSizing::fixed(20)}, {}}};
    const auto root = tree.create_node(grid);
    const auto child = tree.create_node(); tree.append_child(root, child);
    const auto check = [&](Unit width, std::uint64_t builds) {
        tree.mark_dirty(root); // Bypass layout results while retaining compiled structure.
        reset_metrics();
        const auto& result = tree.compute(root, {AvailableSpace::definite(width), AvailableSpace::max_content()});
        require(result.succeeded() && result.grid_tracks.front().columns.size() == static_cast<std::size_t>(width / 20),
            "Automatic repeat used stale constraints");
        require_builds(builds);
    };
    check(100, 1); check(140, 1); check(100, 0); check(140, 0);
    check(180, 1); check(140, 0); check(100, 1);
    auto hidden = tree.style(child); hidden.display = Display::None; tree.set_style(child, hidden);
    check(100, 1); check(140, 1); // Placement invalidation must clear both slots.
}

void workspace_recovery() {
    struct Provider final : MeasureProvider {
        bool fail{};
        Unit width{80};
        Measurement measure(NodeId, const Style&, void*, const MeasureRequest& request) const override {
            if (fail) throw std::runtime_error("Intentional host failure");
            return {{request.known_content_width.value_or(width), request.known_content_height.value_or(12.0F)},
                8.0F, 8.0F};
        }
    } provider;
    Host host;
    host.cells[0].children = {{2}, {3}};
    host.cells[1].parent = host.cells[2].parent = NodeId{1};
    host.cells[1].children = {{4}}; host.cells[2].children = {{5}};
    host.cells[3].parent = NodeId{2}; host.cells[4].parent = NodeId{3};
    for (std::size_t i = 0; i < 3; ++i) {
        host.cells[i].style.display = Display::Grid;
        host.cells[i].style.align_items = {ItemPosition::Baseline};
        host.cells[i].style.grid.template_columns.assign(2, TrackSizing::automatic());
    }
    host.cells[3].style.grid.column.end = GridLine::span(2);
    LayoutTree tree;
    const auto ids = populate(tree, host);
    LayoutOptions options; options.measure_provider = &provider;
    const AvailableSize space{AvailableSpace::max_content(), AvailableSpace::max_content()};
    for (std::size_t round = 0; round < 6; ++round) {
        host.cells[0].style.grid.template_columns.assign(round % 2 ? 40 : 2, TrackSizing::automatic());
        tree.set_style(ids[0], host.cells[0].style);
        tree.mark_subtree_dirty(ids[0]);
        provider.width += 7;
        const auto result = tree.compute(ids[0], space, options);
        LayoutTree fresh;
        const auto new_ids = populate(fresh, host);
        compare(result, fresh.compute(new_ids[0], space, options), ids, new_ids);
        provider.fail = true;
        tree.mark_subtree_dirty(ids[0]);
        bool threw = false;
        try { (void)tree.compute(ids[0], space, options); }
        catch (const std::runtime_error&) { threw = true; }
        require(threw, "Expected host exception");
        require(tree.layout(ids[0]) == std::optional<NodeLayout>{*result.find(ids[0])},
            "Host failure changed the published snapshot");
        provider.fail = false;
        compare(tree.compute(ids[0], space, options), fresh.compute(new_ids[0], space, options), ids, new_ids);
    }
}
} // namespace
int main() {
    try { mutation_contract(false); mutation_contract(true); repeat_cache(); workspace_recovery(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
