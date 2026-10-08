#include <axisly/css_tree.hpp>

#include <array>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
using namespace axisly;
using namespace axisly::css;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F&& operation) {
    bool failed = false;
    try { operation(); } catch (const std::exception&) { failed = true; }
    require(failed, "Invalid operation accepted");
}

void topology() {
    LayoutTree tree;
    const auto a = tree.create_node(), b = tree.create_node(), c = tree.create_node();
    const std::array initial{a, b};
    const auto root = tree.create_node_with_children({}, initial);
    require(tree.node_count() == 4 && tree.child_count(root) == 2, "Wrong counts");
    tree.set_children(root, tree.children(root));
    require(tree.child_at(root, 0) == a, "Aliased children span changed");
    rejects([&] { tree.set_children(a, std::array{root}); });
    rejects([&] { tree.set_children(root, std::array{a, a}); });
    rejects([&] { tree.set_children(root, std::array{NodeId{0}}); });
    require(tree.replace_child(root, 0, c) == a && !tree.parent(a), "Replacement did not detach old child");
    tree.append_child(c, a);
    tree.set_children(root, std::array{a, c, b});
    require(tree.parent(a) == root && tree.child_count(c) == 0, "Nested reparent failed");
    require(tree.detach_child_at(root, 1) == c && !tree.parent(c), "Indexed detach failed");
    tree.detach_children(root, 0, 2);
    require(tree.children(root).empty() && !tree.parent(a) && !tree.parent(b), "Range detach failed");
    rejects([&] { tree.detach_children(root, 0, 1); });
    tree.set_children(root, initial);
    tree.append_child(a, c);
    tree.remove_node(a);
    require(!tree.contains(a) && tree.contains(c) && !tree.parent(c), "Single-node removal deleted descendants");
    require(tree.child_count(root) == 1 && tree.child_at(root, 0) == b, "Single-node removal damaged siblings");
    tree.append_child(b, c);
    tree.remove_subtree(b);
    require(!tree.contains(b) && !tree.contains(c) && tree.node_count() == 1, "Subtree removal is incomplete");
    const auto published = tree.compute(root, {});
    tree.clear();
    require(tree.node_count() == 0 && tree.layout(root) == published.nodes.front(), "Clear changed old snapshot");
    const auto replacement = tree.create_node();
    require(replacement != root && !tree.contains(root), "Clear reused a live identity");
    require(!tree.compute(root, {}).succeeded() && !tree.layout(root), "Invalid root retained geometry");
}

void measurement() {
    struct Provider final : MeasureProvider {
        mutable std::size_t calls{};
        Unit addition{};
        Measurement measure(NodeId, const Style&, void* context, const MeasureRequest& request) const override {
            ++calls;
            const auto width = context ? *static_cast<Unit*>(context) : Unit{7};
            return {{request.known_content_width.value_or(width + addition), 9}};
        }
    } provider;
    LayoutTree tree;
    const auto root = tree.create_node(), a = tree.create_node(), b = tree.create_node();
    tree.set_children(root, std::array{a, b});
    auto width = std::make_shared<Unit>(20);
    tree.set_context(a, width);
    require(tree.context(a) == width, "Context not retained");
    LayoutOptions options;
    options.measure_provider = &provider;
    require(tree.compute(root, {}, options).succeeded(), "Shared measurement failed");
    require(tree.layout(a)->border_box.width == 20, "Context not supplied to provider");
    const auto calls = provider.calls;
    require(tree.compute(root, {}, options).cache_hit && provider.calls == calls, "Shared measurement missed cache");
    tree.mark_dirty(root);
    require(tree.dirty(root) && !tree.dirty(a), "Node invalidation dirtied descendants");
    (void)tree.compute(root, {}, options);
    require(provider.calls == calls, "Ancestor-only invalidation remeasured leaves");
    *width = 35;
    tree.mark_dirty(a);
    (void)tree.compute(root, {}, options);
    require(tree.layout(a)->border_box.width == 35, "Context edit reused stale measurement");
    provider.addition = 4;
    ++options.measurement_revision;
    (void)tree.compute(root, {}, options);
    require(tree.layout(a)->border_box.width == 39, "Provider revision reused stale measurement");
    tree.set_measure(a, [](const MeasureRequest&) { return Measurement{{3, 9}}; });
    (void)tree.compute(root, {}, options);
    require(tree.layout(a)->border_box.width == 3, "Per-node measurement did not take precedence");
    tree.mark_subtree_dirty(root);
    require(tree.dirty(a) && tree.dirty(b), "Subtree invalidation omitted descendants");
    tree.set_measure(a, {});
    (void)tree.compute(root, {});
    require(tree.layout(a)->border_box.width == 0, "Removing provider retained its result");
}

void publication() {
    LayoutTree tree;
    Style grid;
    grid.display = Display::Grid;
    grid.box.size.width = Length::length(101.25F);
    grid.box.padding.left = Length::length(7.5F);
    grid.grid.template_columns = {TrackSizing::fixed(31.25F), TrackSizing::fixed(51.5F)};
    const auto root = tree.create_node(grid);
    Style panel;
    panel.box.padding.left = Length::length(2.25F);
    const auto parent = tree.create_node(panel);
    Style item;
    item.box.size = {Length::length(13.25F), Length::length(8.5F)};
    const auto leaf = tree.create_node(item), sibling = tree.create_node(item);
    tree.append_child(parent, leaf);
    tree.set_children(root, std::array{parent, sibling});
    const auto logical = tree.compute(root, {});
    require(logical.succeeded() && !logical.grid_tracks.empty(), "Default publication lost diagnostics");
    for (bool rounded : {false, true}) {
        LayoutOptions options;
        if (rounded) options.rounding = PixelRounding{1.25F, .2F, -.1F};
        const auto expected = tree.compute(root, {}, options);
        options.publish_nodes = false;
        options.collect_grid_tracks = false;
        const auto& result = tree.compute(root, {}, options);
        require(result.succeeded() && result.nodes.empty() && result.grid_tracks.empty(), "Retained output was flattened");
        require(result.size == expected.size, "Retained root size differs");
        for (const auto& node : expected.nodes) {
            require(tree.layout(node.node) == node, "Sparse publication differs from flat output");
            require(tree.unrounded_layout(node.node) == *logical.find(node.node), "Logical layout lost during rounding");
        }
        const auto local = tree.unrounded_layout(leaf, CoordinateSpace::Parent);
        require(local && local->border_box.x == 2.25F, "Parent-relative coordinates are wrong");
        require(result.nodes.empty(), "Value lookup flattened output");
        require(tree.find_layout(leaf) && result.nodes.size() == expected.nodes.size(), "Pointer lookup did not materialize snapshot");
    }
    LayoutOptions options;
    options.publish_nodes = false;
    (void)tree.compute(root, {}, options);
    const auto saved = tree.layout(leaf);
    tree.remove_subtree(parent);
    require(tree.layout(leaf) == saved, "Mutation invalidated an immutable snapshot");
    (void)tree.compute(root, {}, options);
    require(!tree.layout(leaf), "New snapshot kept a removed leaf");
    options.publish_nodes = true;
    const auto& with_reports = tree.compute(root, {}, options);
    require(!with_reports.grid_tracks.empty(), "Re-enabling diagnostics reused incomplete caches");
    rejects([&] { (void)tree.layout(root, static_cast<CoordinateSpace>(99)); });
    options.rounding = PixelRounding{0};
    rejects([&] { (void)tree.compute(root, {}, options); });
}

void wide_publication() {
    LayoutTree tree;
    Style block;
    block.display = Display::Block;
    block.box.size.width = Length::length(300);
    const auto root = tree.create_node(block);
    Style item;
    item.box.size = {Length::length(10), Length::length(8)};
    std::vector<NodeId> children;
    for (std::size_t i = 0; i < 64; ++i) {
        const auto child = tree.create_node(item);
        children.push_back(child);
        if (i % 16 == 0) {
            for (std::size_t j = 0; j < 24; ++j) tree.append_child(child, tree.create_node(item));
        }
    }
    for (bool reverse : {true, false}) {
        std::reverse(children.begin(), children.end());
        tree.set_children(root, children);
        const auto expected = tree.compute(root, {});
        require(expected.succeeded(), "Wide layout failed");
        LayoutOptions options;
        options.publish_nodes = false;
        (void)tree.compute(root, {}, options);
        for (const auto& node : expected.nodes) {
            require(tree.layout(node.node) == node, "Indexed lookup lost reordered nodes");
        }
        require(tree.compute(root, {}, options).cache_hit, "Value lookup invalidated cache");
        require(!tree.layout(NodeId{reverse ? 1ULL : 2ULL}), "Absent node matched index");
    }
    for (const auto child : children) tree.detach_children(child, 0, tree.child_count(child));
    const auto flat = tree.compute(root, {});
    require(flat.succeeded(), "Flat publication failed");
    for (const auto& node : flat.nodes) {
        require(tree.layout(node.node, CoordinateSpace::Parent) == node,
            "Flat snapshot lost its local coordinates");
    }
}
}

int main(int argc, char** argv) {
    try {
        const std::string_view test = argc > 1 ? argv[1] : "";
        if (test == "topology") topology();
        else if (test == "measurement") measurement();
        else if (test == "publication") { publication(); wide_publication(); }
        else throw std::runtime_error("Unknown test");
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
