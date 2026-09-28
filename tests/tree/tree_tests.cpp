#include <axisly/axisly.hpp>
#include "../../examples/nested_grid_adapter.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string_view>

namespace {
using namespace axisly;
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void near(Unit a, Unit b) { check(std::abs(a - b) < .002f, "Unexpected geometry"); }
template<class E, class F> void throws(F f) {
    try { f(); } catch (const E&) { return; }
    throw std::runtime_error("Expected exception");
}
GridSpec fixed() { return {.columns = {Track::fixed(100), Track::fixed(100)}, .rows = {Track::fixed(80)}}; }
GridSpec content() { return {.columns = {Track::content()}, .rows = {Track::content()}}; }
struct Text {
    Unit width{90}, height{10}; std::size_t calls{};
    bool invalid{}, fail{};
};
MeasureResult measure(ItemId, const MeasureInput& input, void* context) {
    auto& text = *static_cast<Text*>(context); ++text.calls;
    if (text.fail) throw std::runtime_error("host failure");
    const Unit width = input.known_width.value_or(text.width);
    const Unit lines = width > 0 ? std::max(1.f, std::ceil(text.width / width)) : 1;
    return {{0, text.height}, {text.width, text.invalid ? -1.f : text.height * lines}, text.height * .8f};
}
void flat_equivalence() {
    GridTree tree;
    auto s = fixed(); s.columns[0] = Track::percentage(.25f); s.columns[1] = Track::weighted(); s.rows[0] = Track::content();
    const auto root = tree.create_grid(s);
    Text text;
    const std::array items{GridItem{.id = 4}, GridItem{.id = 8, .column = 1, .margin = {2, 3, 4, 5}}};
    const auto a = tree.create_leaf(items[0], measure, &text), b = tree.create_leaf(items[1], measure, &text);
    tree.append_child(root, a); tree.append_child(root, b);
    for (const Unit width : {200.f, 100.f, 0.f, 330.f}) {
        const auto& actual = tree.compute(root, {width, 200});
        const auto expected = compute_grid(s, items, {width, 200}, measure, &text);
        check(actual.succeeded() && actual.size == expected.size && actual.items.size() == 3, "Flat tree differs");
        check(actual.find(a)->rect == expected.items[0].rect && actual.find(b)->rect == expected.items[1].rect, "Flat rectangle mismatch");
    }
}
void unchanged() {
    GridTree tree; Text text;
    const auto root = tree.create_grid(content()); const auto leaf = tree.create_leaf({.id = 6}, measure, &text);
    tree.append_child(root, leaf);
    const auto first = tree.compute(root, {100, 100});
    check(first.succeeded() && first.measurement_count == 2 && first.layout_count == 1, "First tree compute missing");
    const auto* storage = tree.compute(root, {100, 100}).items.data();
    for (int i = 0; i < 200; ++i) {
        tree.set_item(leaf, tree.item(leaf)); tree.set_spec(root, tree.spec(root)); tree.append_child(root, leaf);
        const auto& hit = tree.compute(root, {100, 100});
        check(hit.cache_hit && hit.measurement_count == 0 && hit.layout_count == 0 && hit.items.data() == storage, "Unchanged tree did work");
    }
    check(!tree.is_dirty(root) && !tree.is_dirty(leaf) && text.calls == 2, "Successful nodes remain dirty");
}
void targeted_dirty() {
    GridTree tree; Text left, right;
    const auto root = tree.create_grid(fixed());
    const auto a = tree.create_leaf({.id = 1}, measure, &left), b = tree.create_leaf({.id = 2, .column = 1}, measure, &right);
    tree.append_child(root, a); tree.append_child(root, b);
    check(tree.compute(root, {200, 100}).succeeded(), "Initial tree failed");
    const auto right_calls = right.calls;
    left.height = 20; tree.mark_dirty(a);
    check(tree.is_dirty(root) && tree.is_dirty(a) && !tree.is_dirty(b), "Dirty path includes unrelated sibling");
    const auto& changed = tree.compute(root, {200, 100});
    check(changed.succeeded() && changed.measurement_count == 2 && right.calls == right_calls, "Sibling was remeasured");
    tree.mark_dirty(root);
    check(tree.compute(root, {200, 100}).measurement_count == 4, "Grid content invalidation did not reach descendants");
}
void nested_reference() {
    using namespace axisly::examples;
    GridTree tree; Text text;
    auto root_spec = fixed(); root_spec.columns = {Track::weighted()}; root_spec.rows = {Track::content()}; root_spec.padding = {3, 4, 5, 6};
    auto child_spec = content(); child_spec.columns = {Track::weighted()}; child_spec.padding = {2, 3, 4, 5};
    const GridItem child_item{.id = 20, .margin = {1, 2, 3, 4}};
    const GridItem leaf_item{.id = 30, .horizontal_alignment = HorizontalAlignment::Stretch};
    const auto root = tree.create_grid(root_spec), child = tree.create_grid(child_spec, child_item);
    const auto leaf = tree.create_leaf(leaf_item, measure, &text);
    tree.append_child(root, child); tree.append_child(child, leaf);
    NestedGrid reference_child{.spec = child_spec, .items = {leaf_item}, .measure = measure, .user_data = &text};
    NestedGrid reference_root{.spec = root_spec, .items = {child_item}, .children = {{20, &reference_child}}};
    for (const Unit width : {300.f, 60.f, 30.f, 140.f, 0.f}) {
        const auto& actual = tree.compute(root, {width, 200});
        const auto expected = compute_nested_grid(reference_root, {width, 200});
        check(actual.succeeded() && expected.succeeded() && actual.size == expected.size, "Nested occupied extent mismatch");
        check(actual.find(child)->rect == expected.items[0].rect && actual.find(leaf)->rect == expected.items[1].rect, "Nested final-size reflow differs from host adapter");
    }
}
void sibling_subtree_reuse() {
    GridTree tree; Text a, b;
    auto inner = fixed(); inner.columns = {Track::fixed(100)}; inner.rows[0] = Track::fixed(80);
    const auto root = tree.create_grid(fixed()), left = tree.create_grid(inner), right = tree.create_grid(inner, {.column = 1});
    const auto leaf_a = tree.create_leaf({}, measure, &a), leaf_b = tree.create_leaf({}, measure, &b);
    tree.append_child(root, left); tree.append_child(root, right); tree.append_child(left, leaf_a); tree.append_child(right, leaf_b);
    check(tree.compute(root, {200, 100}).succeeded(), "Nested setup failed");
    const auto count_b = b.calls;
    a.height = 16; tree.mark_dirty(leaf_a);
    check(tree.is_dirty(left) && tree.is_dirty(root) && !tree.is_dirty(right), "Nested dirty path wrong");
    const auto& result = tree.compute(root, {200, 100});
    check(result.succeeded() && b.calls == count_b && result.cache_hits > 0, "Unrelated subtree did not reuse cache");
}
void topology() {
    GridTree tree;
    auto spec = fixed(); spec.auto_flow = AutoFlow::Row; spec.allow_implicit_tracks = true; spec.implicit_row = Track::fixed(20);
    const auto root = tree.create_grid(spec), other = tree.create_grid(spec);
    const auto a = tree.create_leaf({.column = auto_position, .row = auto_position}), b = tree.create_leaf({.column = auto_position, .row = auto_position});
    tree.append_child(root, a); tree.append_child(root, b);
    check(tree.compute(root, {200, 100}).find(a)->rect.x == 0, "Initial order wrong");
    tree.insert_child(root, 0, b);
    check(tree.compute(root, {200, 100}).find(a)->rect.x == 100, "Reorder did not invalidate placement");
    tree.append_child(other, a);
    check(tree.parent(a) == other && tree.children(root).size() == 1, "Reparent retained old edge");
    check(tree.compute(root, {200, 100}).items.size() == 2 && tree.compute(other, {200, 100}).find(a), "Reparent layout failed");
    tree.detach(a); check(!tree.parent(a) && tree.children(other).empty(), "Detach failed");
    tree.append_child(root, other); tree.append_child(other, a);
    throws<std::invalid_argument>([&] { tree.append_child(other, root); });
    throws<std::invalid_argument>([&] { tree.append_child(root, root); });
    throws<std::invalid_argument>([&] { tree.append_child(a, b); });
    check(tree.parent(other) == root, "Rejected cycle changed topology");
    tree.remove(other); check(!tree.contains(other) && !tree.contains(a) && tree.contains(b), "Subtree removal failed");
    throws<std::out_of_range>([&] { (void)tree.item(a); });
}
void handles_and_validation() {
    GridTree a, b; const auto x = a.create_grid(), y = b.create_grid();
    check(x != y && !a.contains(y) && !a.contains({}), "Foreign handles alias");
    throws<std::out_of_range>([&] { a.append_child(x, y); });
    const auto leaf = a.create_leaf();
    throws<std::invalid_argument>([&] { (void)a.compute(leaf, {}); });
    throws<std::out_of_range>([&] { a.insert_child(x, 1, leaf); });
    check(a.children(x).empty() && !a.parent(leaf), "Failed insert mutated graph");
    a.remove(x); const auto next = a.create_grid(); check(next != x && !a.contains(x), "Removed ID reused");
    throws<std::invalid_argument>([] { GridTree bad({0, 1}); });
}
void error_atomicity() {
    GridTree tree; Text text;
    const auto root = tree.create_grid(content()), child = tree.create_grid(content());
    const auto leaf = tree.create_leaf({}, measure, &text);
    tree.append_child(root, child); tree.append_child(child, leaf);
    check(tree.compute(root, {100, 100}).succeeded(), "Setup failed");
    text.invalid = true; tree.mark_dirty(leaf);
    const auto& failure = tree.compute(root, {100, 100});
    check(failure.error.code == GridErrorCode::InvalidMeasurement && failure.items.empty() && failure.size == Size{}, "Partial geometry escaped");
    check(failure.error_node == leaf, "Error lost leaf identity");
    text.invalid = false; tree.mark_dirty(leaf); check(tree.compute(root, {100, 100}).succeeded(), "Error poisoned retry");
    text.fail = true; tree.mark_dirty(leaf);
    throws<std::runtime_error>([&] { (void)tree.compute(root, {100, 100}); });
    text.fail = false; check(tree.compute(root, {100, 100}).succeeded(), "Exception poisoned retry");
    const auto& invalid = tree.compute(root, {-1, 100});
    check(invalid.error.code == GridErrorCode::InvalidContainerSize && invalid.items.empty(), "Invalid size accepted");
}
void callback_guards() {
    GridTree tree; const auto root = tree.create_grid(content());
    struct Context { GridTree* tree; NodeId root; } context{&tree, root};
    const auto leaf = tree.create_leaf({}, +[](ItemId, const MeasureInput&, void* raw) -> MeasureResult {
        auto& c = *static_cast<Context*>(raw); c.tree->mark_dirty(c.root); return {};
    }, &context);
    tree.append_child(root, leaf);
    throws<std::logic_error>([&] { (void)tree.compute(root, {100, 100}); });
    tree.set_measure(leaf, +[](ItemId, const MeasureInput&, void* raw) -> MeasureResult {
        auto& c = *static_cast<Context*>(raw); (void)c.tree->compute(c.root, {}); return {};
    }, &context);
    throws<std::logic_error>([&] { (void)tree.compute(root, {100, 100}); });
    tree.set_measure(leaf, nullptr); check(tree.compute(root, {100, 100}).succeeded(), "Guard remained active");
}
void limits() {
    GridTree depth({2, 1000}); const auto root = depth.create_grid(content()), child = depth.create_grid(content()), grandchild = depth.create_grid(content());
    depth.append_child(root, child); depth.append_child(child, grandchild);
    const auto& r = depth.compute(root, {100, 100});
    check(r.error.code == GridErrorCode::TreeLimit && r.items.empty(), "Depth guard failed");
    GridTree work({64, 1}); const auto a = work.create_grid(content()), b = work.create_grid(content()); work.append_child(a, b);
    check(work.compute(a, {100, 100}).error.code == GridErrorCode::TreeLimit, "Work guard failed");
    GridTree overflow;
    auto huge = content(); huge.columns = {Track::fixed(std::numeric_limits<Unit>::max())};
    const auto outer = overflow.create_grid({.columns = {Track::fixed(std::numeric_limits<Unit>::max()), Track::fixed(0)}, .rows = {Track::fixed(10)}});
    const auto inner = overflow.create_grid(huge, {.column = 1}); overflow.append_child(outer, inner);
    check(overflow.compute(outer, {100, 100}).error.code == GridErrorCode::NumericOverflow, "Absolute child overflow accepted");
}
void rounding_tree() {
    GridTree tree;
    const GridSpec spec{.columns = {Track::weighted(), Track::weighted(), Track::weighted()}, .rows = {Track::weighted()}};
    const auto root = tree.create_grid(spec), child = tree.create_grid(spec, {.column = 1}); tree.append_child(root, child);
    std::array<NodeId, 3> leaves;
    for (std::size_t i = 0; i < 3; ++i) { leaves[i] = tree.create_leaf({.column = static_cast<std::uint32_t>(i)}); tree.append_child(child, leaves[i]); }
    const auto raw = tree.compute(root, {100, 100});
    const auto& rounded = tree.compute(root, {100, 100}, PixelRounding{1});
    check(rounded.succeeded(), "Tree rounding failed");
    for (const auto leaf : leaves) near(rounded.find(leaf)->rect.x, std::round(raw.find(leaf)->rect.x));
    near(rounded.find(leaves[2])->rect.x + rounded.find(leaves[2])->rect.width,
         rounded.find(child)->rect.x + rounded.find(child)->rect.width);
    check(tree.compute(root, {100, 100}, PixelRounding{0}).error.code == GridErrorCode::InvalidRounding, "Invalid tree rounding accepted");
}
void randomized_updates() {
    GridTree tree; std::mt19937 random(952);
    auto spec = fixed(); spec.columns = {Track::percentage(.25f), Track::weighted()}; spec.rows = {Track::content()};
    spec.auto_flow = AutoFlow::RowDense; spec.allow_implicit_tracks = true; spec.implicit_row = Track::content();
    const auto root = tree.create_grid(spec);
    std::vector<NodeId> nodes; std::vector<GridItem> items;
    for (ItemId id = 0; id < 20; ++id) {
        GridItem item{.id = id, .column = auto_position, .row = auto_position, .preferred_size = {10, 10}};
        items.push_back(item); nodes.push_back(tree.create_leaf(item)); tree.append_child(root, nodes.back());
    }
    for (int frame = 0; frame < 150; ++frame) {
        const auto index = random() % items.size(); items[index].preferred_size.height = static_cast<Unit>(random() % 30);
        items[index].column_span = 1 + random() % 2; tree.set_item(nodes[index], items[index]);
        const Size available{static_cast<Unit>(random() % 300), 200};
        const auto& actual = tree.compute(root, available); const auto expected = compute_grid(spec, items, available);
        check(actual.succeeded() && actual.size == expected.size, "Randomized tree extent mismatch");
        for (std::size_t i = 0; i < nodes.size(); ++i) check(actual.find(nodes[i])->rect == expected.items[i].rect, "Randomized tree reused stale geometry");
    }
}
}
int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("Expected case name");
        const std::pair<std::string_view, void(*)()> cases[]{
            {"flat_equivalence", flat_equivalence}, {"unchanged", unchanged}, {"targeted_dirty", targeted_dirty},
            {"nested_reference", nested_reference}, {"sibling_subtree_reuse", sibling_subtree_reuse}, {"topology", topology},
            {"handles_and_validation", handles_and_validation}, {"error_atomicity", error_atomicity}, {"callback_guards", callback_guards},
            {"limits", limits}, {"rounding_tree", rounding_tree}, {"randomized_updates", randomized_updates}};
        for (const auto& [name, run] : cases) if (name == argv[1]) { run(); return 0; }
        throw std::runtime_error("Unknown case");
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
