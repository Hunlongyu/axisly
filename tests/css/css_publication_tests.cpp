#include <axisly/css_tree.hpp>
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace axisly;
using namespace axisly::css;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Fixture {
    LayoutTree tree;
    NodeId root;
    std::array<NodeId, 24> leaves;
    std::array<Unit, 24> widths{};
    Style outer;
    Fixture() {
        widths.fill(12.25F);
        outer.display = Display::Flex;
        outer.box.size = {Length::length(400.5F), Length::length(200)};
        outer.justify_content = {ContentPosition::SpaceBetween};
        root = tree.create_node(outer);
        Style panel;
        panel.display = Display::Flex;
        panel.flex_direction = FlexDirection::Column;
        panel.align_items = ItemAlignment{ItemPosition::Start};
        panel.box.size = {Length::length(100.25F), Length::length(180)};
        panel.flex_shrink = 0;
        for (std::size_t p = 0; p < 2; ++p) {
            const auto parent = tree.create_node(panel);
            tree.append_child(root, parent);
            for (std::size_t i = p * 12; i < (p + 1) * 12; ++i) {
                leaves[i] = tree.create_node({}, callback(i));
                tree.append_child(parent, leaves[i]);
            }
        }
    }
    MeasureFunction callback(std::size_t i) {
        return [this, i](const MeasureRequest&) { return Measurement{.content_size = {widths[i], 10.125F}}; };
    }
    const LayoutResult& compute(LayoutOptions options = {}) {
        return tree.compute(root, {AvailableSpace::definite(600), AvailableSpace::definite(300)}, options);
    }
    void edit(std::size_t i) { widths[i] += .25F; tree.mark_dirty(leaves[i]); }
};
void compare(const LayoutResult& result, const LayoutResult& expected) {
    require(result.succeeded() && expected.succeeded() && result.nodes.size() == expected.nodes.size(), "Incomplete publication");
    for (std::size_t i = 0; i < result.nodes.size(); ++i) {
        const auto& a = result.nodes[i]; const auto& b = expected.nodes[i];
        auto normalized = a;
        normalized.node = b.node; normalized.parent = b.parent;
        require(normalized == b,
            "Incremental publication differs from a fresh full calculation");
    }
}

void rounded_publication() {
    for (const bool full : {true, false}) {
        Fixture incremental, reference;
        LayoutOptions options;
        options.publish_nodes = full;
        options.rounding = PixelRounding{1.25F, 0.2F, -0.3F};
        incremental.compute(options); reference.compute(options);
        bool reused = false;
        for (std::size_t frame = 0; frame < 30; ++frame) {
            incremental.edit(frame % 12); reference.edit(frame % 12);
            if (frame == 4 || frame == 8) {
                incremental.outer.box.size.width = reference.outer.box.size.width = Length::length(
                    frame == 4 ? 501.125F : 400.5F);
                incremental.tree.set_style(incremental.root, incremental.outer);
                reference.tree.set_style(reference.root, reference.outer);
            }
            if (frame == 10) options.rounding = PixelRounding{2, -0.5F, 0.5F};
            if (frame == 12) options.rounding.reset();
            if (frame == 14) options.rounding = PixelRounding{1.25F, 0.2F, -0.3F};
            if (frame == 17 || frame == 20) {
                const auto update = [&](Fixture& f) {
                    const auto panel = *f.tree.parent(f.leaves[12]);
                    auto style = f.tree.style(panel);
                    style.display = frame == 17 ? Display::None : Display::Flex;
                    style.inset.left = Length::length(-1.5F);
                    f.tree.set_style(panel, style);
                };
                update(incremental); update(reference);
            }
            if (frame == 24) {
                incremental.tree.append_child(*incremental.tree.parent(incremental.leaves[0]), incremental.leaves[23]);
                reference.tree.append_child(*reference.tree.parent(reference.leaves[0]), reference.leaves[23]);
            }
            reference.tree.mark_subtree_dirty(reference.root);
            const auto& a = incremental.compute(options);
            const auto& b = reference.compute(options);
            require(a.succeeded() && b.succeeded() && a.size == b.size, "Rounded publication failed");
            reused |= a.published_nodes_reused >= 12;
            if (full) compare(a, b);
            else require(a.nodes.empty() && b.nodes.empty(), "Rounded validation materialized nodes");
            for (std::size_t i = 0; i < incremental.leaves.size(); ++i) {
                for (const auto space : {CoordinateSpace::Root, CoordinateSpace::Parent}) {
                    auto actual = *incremental.tree.layout(incremental.leaves[i], space);
                    const auto expected = *reference.tree.layout(reference.leaves[i], space);
                    actual.node = expected.node; actual.parent = expected.parent;
                    require(actual == expected, "Rounded subtree reuse changed a field or coordinate space");
                }
            }
        }
        if (full) require(reused, "Rounded unchanged subtree was never reused");
        // Failure after an earlier subtree was processed must not authorize
        // reuse of a partially published buffer or poison validation hints.
        const auto previous = incremental.tree.layout(incremental.leaves[0]);
        incremental.tree.set_measure(incremental.leaves[22], [](const MeasureRequest&) -> Measurement {
            throw std::runtime_error("rounded host failure");
        });
        bool thrown = false;
        try { incremental.compute(options); } catch (const std::runtime_error&) { thrown = true; }
        require(thrown && incremental.tree.layout(incremental.leaves[0]) == previous,
            "Rounded host failure changed the previous snapshot");
        incremental.tree.set_measure(incremental.leaves[22], incremental.callback(22));
        incremental.compute(options);
        options.rounding = PixelRounding{std::numeric_limits<Unit>::max()};
        require(incremental.compute(options).error.code == LayoutErrorCode::NumericOverflow,
            "Rounded validation cache skipped changed-scale overflow");
        options.rounding = PixelRounding{1.25F, 0.2F, -0.3F};
        const auto& recovered = incremental.compute(options);
        require(recovered.succeeded(), "Rounded publication failed to recover");
        reference.tree.mark_subtree_dirty(reference.root);
        if (full) compare(recovered, reference.compute(options));
    }
}
void publication() {
    Fixture incremental, reference;
    compare(incremental.compute(), reference.compute());
    bool reused = false;
    for (std::size_t frame = 0; frame < 20; ++frame) {
        const auto leaf = frame % 12;
        incremental.edit(leaf); reference.edit(leaf);
        if (frame == 5 || frame == 10) {
            incremental.outer.box.size.width = reference.outer.box.size.width = Length::length(501.125F);
            incremental.tree.set_style(incremental.root, incremental.outer);
            reference.tree.set_style(reference.root, reference.outer);
        }
        if (frame == 15) {
            incremental.tree.detach(incremental.leaves[23]);
            reference.tree.detach(reference.leaves[23]);
        }
        reference.tree.mark_subtree_dirty(reference.root);
        const auto& actual = incremental.compute();
        compare(actual, reference.compute());
        reused = reused || actual.published_nodes_reused >= 12;
    }
    require(reused, "Unchanged translated subtree was never reused");
    const auto& before = incremental.compute();
    const auto* storage = before.nodes.data();
    const auto snapshot = before;
    incremental.tree.set_measure(incremental.leaves[0], [](const MeasureRequest&) -> Measurement {
        throw std::runtime_error("host failure");
    });
    bool thrown = false;
    try { (void)incremental.compute(); } catch (const std::runtime_error&) { thrown = true; }
    require(thrown && before.nodes.data() == storage, "Host failure replaced the published snapshot");
    compare(before, snapshot);
    incremental.tree.set_measure(incremental.leaves[0], incremental.callback(0));
    compare(incremental.compute(), reference.compute());
}

void ordered_translations() {
    LayoutTree tree;
    Style outer;
    outer.display = Display::Block;
    outer.box.size.width = Length::length(33554432);
    outer.box.padding.left = Length::length(16777216);
    const auto root = tree.create_node(outer);
    Style panel;
    panel.box.size = {Length::length(64), Length::length(8)};
    panel.inset.left = Length::length(-16777216);
    const auto parent = tree.create_node(panel);
    tree.append_child(root, parent);
    Style item;
    item.box.size = {Length::length(1), Length::length(1)};
    item.box.margin.left = Length::length(1);
    NodeId first;
    for (std::size_t i = 0; i < 12; ++i) {
        const auto leaf = tree.create_node(item);
        tree.append_child(parent, leaf);
        if (!i) first = leaf;
    }
    const auto initial = tree.compute(root, {});
    require(initial.succeeded(), "Ordered translation layout failed");
    // Each placement rounds back to Unit. The small local coordinate is lost
    // at the first large offset and cannot be recovered by cancelling offsets.
    require(initial.find(parent)->border_box.x == 0
        && initial.find(first)->border_box.x == 0, "Translations were combined before rounding");
    LayoutOptions sparse;
    sparse.publish_nodes = false;
    require(tree.compute(root, {}, sparse).succeeded(), "Sparse translated layout failed");
    for (const auto& node : initial.nodes) {
        require(tree.layout(node.node) == node, "Sparse lookup changed ordered translations");
    }
    require(tree.unrounded_layout(first, CoordinateSpace::Parent)->border_box.x == 1,
        "Local coordinates were reconstructed from rounded root coordinates");
    for (std::size_t frame = 0; frame < 4; ++frame) {
        outer.box.size.width = Length::length(33554432 + static_cast<Unit>(frame + 1) * 8);
        tree.set_style(root, outer);
        const auto retained = tree.compute(root, {});
        tree.mark_subtree_dirty(root);
        compare(retained, tree.compute(root, {}));
        require(retained.find(first)->border_box.x == 0, "Restored translation history changed");
    }
}
}
int main() {
    try { publication(); rounded_publication(); ordered_translations(); return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
