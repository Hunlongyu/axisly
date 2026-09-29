#include <axisly/css_tree.hpp>

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
using namespace axisly;
using namespace axisly::css;

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

void leaf_and_block() {
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Block;
    root_style.box.size.width = Length::length(200);
    root_style.box.padding = {Length::length(10), Length::length(10),
                              Length::length(10), Length::length(10)};
    root_style.box.border = {Length::length(2), Length::length(2),
                             Length::length(2), Length::length(2)};
    const auto root = tree.create_node(root_style);
    Style child_style;
    child_style.box.size.height = Length::length(20);
    child_style.box.margin.left = Length::length(3);
    child_style.box.margin.top = Length::length(5);
    child_style.box.margin.bottom = Length::length(7);
    std::size_t calls = 0;
    const auto child = tree.create_node(child_style, [&](const MeasureRequest& request) {
        ++calls;
        require(request.known_content_width == 173 && request.known_content_height == 20,
            "Block child must receive its final known content size");
        return Measurement{{173, 20}, 15.0f, 15.0f};
    });
    tree.append_child(root, child);
    const auto& result = tree.compute(root,
        {AvailableSpace::definite(200), AvailableSpace::indefinite()});
    require(result.succeeded() && result.size == Size{200, 56},
        "Block must size from padding, border and child outer height");
    require(result.nodes.size() == 2 && result.nodes.front().node == root,
        "Tree result must be root-first");
    require(result.find(root)->content_box == Rect{12, 12, 176, 32},
        "Block content box must exclude padding and border");
    require(result.find(child)->border_box == Rect{15, 17, 173, 20},
        "Child box must be placed within parent content box");
    require(result.find(child)->first_baseline == 15 && calls == 1,
        "Leaf baseline and measurement count must be retained");
    require(result.find(root)->first_baseline == 32,
        "Block must export its first in-flow child's baseline in local coordinates");
}

void topology_and_hidden() {
    LayoutTree tree;
    Style hidden_style;
    hidden_style.display = Display::None;
    const auto hidden = tree.create_node(hidden_style);
    const auto nested = tree.create_node();
    tree.append_child(hidden, nested);
    const auto result = tree.compute(hidden,
        {AvailableSpace::definite(100), AvailableSpace::definite(100)});
    require(result.succeeded() && result.nodes.size() == 2 && result.size == Size{},
        "display:none must suppress subtree geometry");
    require(result.find(nested)->border_box == Rect{}, "Hidden descendant must be zeroed");
    const auto second = tree.create_node();
    tree.append_child(second, nested);
    require(tree.parent(nested).has_value(), "Reparented child must have a parent");
    require(tree.parent(nested) == second && tree.children(hidden).empty(),
        "Reparenting must remove the old child link");
    bool cycle = false;
    try { tree.append_child(nested, second); }
    catch (const std::invalid_argument&) { cycle = true; }
    require(cycle, "Cycle insertion must be rejected");
    tree.remove(second);
    require(!tree.contains(second) && !tree.contains(nested) && tree.contains(hidden),
        "Removing a subtree must invalidate all descendants");
}

void intrinsic_queries() {
    LayoutTree tree;
    Style style;
    style.box.size.width = Length::fit_content_length(50);
    style.box.size.height = Length::length(10);
    std::size_t min_queries = 0, max_queries = 0, ordinary_queries = 0;
    const auto leaf = tree.create_node(style, [&](const MeasureRequest& request) {
        if (request.available_width.kind == AvailableKind::MinContent) {
            ++min_queries;
            return Measurement{{30, 10}, {}, {}};
        }
        if (request.available_width.kind == AvailableKind::MaxContent) {
            ++max_queries;
            return Measurement{{90, 10}, {}, {}};
        }
        ++ordinary_queries;
        return Measurement{{70, 10}, {}, {}};
    });
    const auto& result = tree.compute(leaf,
        {AvailableSpace::definite(100), AvailableSpace::indefinite()});
    require(result.succeeded() && result.size == Size{50, 10}
        && min_queries == 1 && max_queries == 1 && ordinary_queries == 1,
        "fit-content must query min/max content then clamp the limit");
    style.box.size.width = Length::min_content();
    tree.set_style(leaf, style);
    require(tree.compute(leaf,
        {AvailableSpace::definite(100), AvailableSpace::indefinite()}).size.width == 30,
        "min-content keyword must use the host's min-content query");
}

void intrinsic_container_invalidation() {
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Flex;
    root_style.box.size = {Length::length(160), Length::length(80)};
    root_style.align_items = {ItemPosition::Start};
    const auto root = tree.create_node(root_style);
    Style grid_style;
    grid_style.display = Display::Grid;
    grid_style.box.size.width = Length::min_content();
    grid_style.box.size.height = Length::length(60);
    grid_style.grid.template_columns = {
        TrackSizing::automatic(), TrackSizing::automatic()};
    grid_style.grid.template_rows = {TrackSizing::automatic()};
    grid_style.box.padding.left = Length::length(4);
    grid_style.box.padding.right = Length::length(4);
    grid_style.gap.width = Length::length(4);
    grid_style.justify_items = {ItemPosition::Start};
    const auto grid = tree.create_node(grid_style);
    tree.append_child(root, grid);
    Unit first_minimum = 20;
    const auto first = tree.create_node({}, [&](const MeasureRequest& request) {
        const Unit width = request.available_width.kind == AvailableKind::MinContent
            ? first_minimum : Unit{60};
        return Measurement{{request.known_content_width.value_or(width), 12}, {}, {}};
    });
    const auto second = tree.create_node({}, [](const MeasureRequest& request) {
        const Unit width = request.available_width.kind == AvailableKind::MinContent
            ? Unit{30} : Unit{40};
        return Measurement{{request.known_content_width.value_or(width), 12}, {}, {}};
    });
    tree.append_child(grid, first);
    tree.append_child(grid, second);
    const AvailableSize space{AvailableSpace::definite(160), AvailableSpace::definite(80)};
    const auto initial = tree.compute(root, space);
    require(initial.succeeded() && initial.find(grid)->border_box.width == 62,
        "Nested min-content Grid width must use the measured child contributions");
    const auto reused = tree.compute(root, space);
    require(reused.cache_hit && reused.layout_count == 0 && reused.measurement_count == 0,
        "Unchanged intrinsic container must reuse its complete result");
    first_minimum = 35;
    tree.mark_dirty(first);
    const auto updated = tree.compute(root, space);
    require(updated.succeeded() && !updated.cache_hit
        && updated.find(grid)->border_box.width == 77,
        "Dirty measured content must invalidate the intrinsic container size");
    grid_style.box.size.width = Length::max_content();
    tree.set_style(grid, grid_style);
    const auto maximum = tree.compute(root, space);
    require(maximum.succeeded() && maximum.find(grid)->border_box.width == 112,
        "Switching the intrinsic keyword must recompute the container width");
}

void errors_and_exception_atomicity() {
    LayoutTree tree;
    Style indefinite_style;
    indefinite_style.display = Display::Block;
    indefinite_style.box.padding.left = Length::percent(0.5f);
    const auto root = tree.create_node(indefinite_style);
    const auto child = tree.create_node();
    tree.append_child(root, child);
    const auto& indefinite = tree.compute(root,
        {AvailableSpace::indefinite(), AvailableSpace::definite(100)});
    require(indefinite.succeeded() && indefinite.find(root)->padding.left == 0,
        "Indefinite percentage padding must resolve to zero in layout");
    tree.detach(child);
    tree.set_style(root, Style{});
    tree.set_measure(root, [](const MeasureRequest&) { return Measurement{{-1, 0}, {}, {}}; });
    const auto& invalid = tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::indefinite()});
    require(invalid.error.code == LayoutErrorCode::InvalidMeasurement && invalid.nodes.empty(),
        "Invalid host measurement must publish no geometry");
    tree.set_measure(root, [&](const MeasureRequest&) -> Measurement {
        bool rejected = false;
        try { tree.mark_dirty(root); }
        catch (const std::logic_error&) { rejected = true; }
        require(rejected, "Callback mutation must be rejected");
        throw std::runtime_error("host error");
    });
    bool thrown = false;
    try {
        (void)tree.compute(root,
            {AvailableSpace::definite(100), AvailableSpace::indefinite()});
    } catch (const std::runtime_error&) { thrown = true; }
    require(thrown, "Host exception must propagate");
    require(tree.compute({999999}, {AvailableSpace::indefinite(), AvailableSpace::indefinite()})
        .error.code == LayoutErrorCode::InvalidRoot,
        "Tree must remain usable after callback exception");
}

void cache_and_dirty() {
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Block;
    root_style.box.size.width = Length::length(100);
    const auto root = tree.create_node(root_style);
    std::size_t first_calls = 0, second_calls = 0;
    const auto first = tree.create_node({}, [&](const MeasureRequest&) {
        ++first_calls;
        return Measurement{{30, 10}, {}, {}};
    });
    const auto second = tree.create_node({}, [&](const MeasureRequest&) {
        ++second_calls;
        return Measurement{{30, 10}, {}, {}};
    });
    tree.append_child(root, first);
    tree.append_child(root, second);
    const AvailableSize space{AvailableSpace::definite(100), AvailableSpace::indefinite()};
    const auto initial = tree.compute(root, space);
    require(initial.succeeded() && initial.measurement_count == 2
        && first_calls == 1 && second_calls == 1, "Initial layout must measure both leaves");
    const auto* published_nodes = tree.compute(root, space).nodes.data();
    const auto reused = tree.compute(root, space);
    require(reused.cache_hit && reused.layout_count == 0 && reused.measurement_count == 0
        && tree.compute(root, space).nodes.data() == published_nodes,
        "Unchanged tree must reuse its published result storage");
    tree.mark_dirty(first);
    const auto refreshed = tree.compute(root, space);
    require(refreshed.succeeded() && !refreshed.cache_hit && refreshed.cache_hits >= 1
        && first_calls == 2 && second_calls == 1,
        "Dirty leaf must remeasure only its own path");
    const auto resized = tree.compute(root,
        {AvailableSpace::definite(120), AvailableSpace::indefinite()});
    require(resized.succeeded() && resized.measurement_count == 0
        && first_calls == 2 && second_calls == 1,
        "Changed root space may reuse children with identical constraints");
}

void inherited_float_invalidation() {
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Block;
    root_style.box.size.width = Length::length(100);
    root_style.box.padding = {Length::length(2), Length::length(2),
        Length::length(2), Length::length(2)};
    const auto root = tree.create_node(root_style);
    Style float_style;
    float_style.display = Display::Block;
    float_style.float_side = FloatSide::Left;
    float_style.box.size = {Length::length(30), Length::length(25)};
    const auto floating = tree.create_node(float_style);
    tree.append_child(root, floating);
    Style block_style;
    block_style.display = Display::Block;
    block_style.box.size.height = Length::length(12);
    block_style.box.margin.left = Length::length(3);
    block_style.box.margin.top = Length::length(5);
    const auto block = tree.create_node(block_style);
    tree.append_child(root, block);
    Style content_style;
    content_style.display = Display::Flex;
    content_style.box.size = {Length::length(10), Length::length(8)};
    const auto content = tree.create_node(content_style);
    tree.append_child(block, content);

    const AvailableSize space{AvailableSpace::definite(100),
        AvailableSpace::max_content()};
    const auto first = tree.compute(root, space);
    require(first.succeeded() && first.find(content)->border_box.x == 32,
        "Descendant formatting context must avoid the ancestor's float");
    const auto reused = tree.compute(root, space);
    require(reused.cache_hit && reused.layout_count == 0,
        "Unchanged shared Block context must reuse the root result");
    float_style.box.size.width = Length::length(45);
    tree.set_style(floating, float_style);
    const auto refreshed = tree.compute(root, space);
    require(refreshed.succeeded() && !refreshed.cache_hit
        && refreshed.find(content)->border_box.x == 47,
        "Changing a float must invalidate inherited placement");
}

void plain_block_single_measure() {
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Block;
    root_style.box.size.width = Length::length(100);
    const auto root = tree.create_node(root_style);
    Style block_style;
    block_style.display = Display::Block;
    const auto block = tree.create_node(block_style);
    tree.append_child(root, block);
    std::size_t calls = 0;
    const auto leaf = tree.create_node({}, [&](const MeasureRequest&) {
        ++calls;
        return Measurement{{100, 10}, {}, {}};
    });
    tree.append_child(block, leaf);
    const auto result = tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::max_content()});
    require(result.succeeded() && result.size == Size{100, 10}
        && result.measurement_count == 1 && calls == 1,
        "A Block subtree without floats must use one layout pass");
}
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected test case name");
        const std::string_view name = argv[1];
        if (name == "leaf_and_block") leaf_and_block();
        else if (name == "topology_and_hidden") topology_and_hidden();
        else if (name == "intrinsic_queries") intrinsic_queries();
        else if (name == "intrinsic_container_invalidation") intrinsic_container_invalidation();
        else if (name == "errors_and_exception_atomicity") errors_and_exception_atomicity();
        else if (name == "cache_and_dirty") cache_and_dirty();
        else if (name == "inherited_float_invalidation") inherited_float_invalidation();
        else if (name == "plain_block_single_measure") plain_block_single_measure();
        else throw std::runtime_error("Unknown test case");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
