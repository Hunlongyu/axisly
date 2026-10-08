#include <axisly/css_tree.hpp>
#include <array>
#include <iostream>
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
    const LayoutResult& compute() { return tree.compute(root, {AvailableSpace::definite(600), AvailableSpace::definite(300)}); }
    void edit(std::size_t i) { widths[i] += .25F; tree.mark_dirty(leaves[i]); }
};
void compare(const LayoutResult& result, const LayoutResult& expected) {
    require(result.succeeded() && expected.succeeded() && result.nodes.size() == expected.nodes.size(), "Incomplete publication");
    for (std::size_t i = 0; i < result.nodes.size(); ++i) {
        const auto& a = result.nodes[i]; const auto& b = expected.nodes[i];
        require(a.border_box == b.border_box && a.content_box == b.content_box
            && a.scrollable_overflow_rect == b.scrollable_overflow_rect && a.first_baseline == b.first_baseline,
            "Incremental publication differs from a fresh full calculation");
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
    try { publication(); ordered_translations(); return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
