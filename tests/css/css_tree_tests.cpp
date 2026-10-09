#include <axisly/css_tree.hpp>

#include <array>
#include <algorithm>
#include <cmath>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeinfo>

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

void leaf_available_constraints() {
    // Host callbacks see the used content budget after bounds, margins and
    // gutters. These assertions are independent of the Taffy callback model.
    for (const bool styled : {false, true}) {
        for (const bool minimum_wins : {false, true}) {
            LayoutTree tree;
            Style style;
            if (styled) style.box.size.width = Length::length(42);
            style.box.max_size.width = Length::length(30);
            if (minimum_wins) style.box.min_size.width = Length::length(50);
            style.box.padding = {Length::length(3), Length::length(2),
                Length::length(3), Length::length(2)};
            style.box.border = {Length::length(1), Length::length(1),
                Length::length(1), Length::length(1)};
            style.box.margin = {Length::length(5), Length::length(4),
                Length::length(7), Length::length(6)};
            style.overflow_y = Overflow::Scroll;
            style.scrollbar_width = 8;
            const Unit content_width = minimum_wins ? 34.0F : 14.0F;
            std::size_t calls = 0;
            const auto root = tree.create_node(style, [&](const MeasureRequest& request) {
                ++calls;
                require(request.available_width == AvailableSpace::definite(content_width)
                    && request.available_height == AvailableSpace::definite(84),
                    "Leaf measurement budget must resolve bounds, margins and content insets");
                require(request.known_content_width == (styled
                        ? std::optional<Unit>(content_width) : std::nullopt)
                    && !request.known_content_height,
                    "Styled leaf dimension must be clamped before reporting known content");
                return Measurement{{content_width, 14}};
            });
            const auto result = tree.compute(root,
                {AvailableSpace::definite(200), AvailableSpace::definite(100)});
            require(result.succeeded() && result.size == Size{content_width + 16, 20}
                && result.find(root)->content_box.width == content_width && calls == 1,
                "Leaf used geometry must agree with its measurement budget");
        }
    }
    LayoutTree tree;
    Style parent_style;
    parent_style.box.size = {Length::length(100), Length::length(60)};
    const auto parent = tree.create_node(parent_style);
    Style child_style;
    child_style.box.min_size = {Length::length(0), Length::length(0)};
    child_style.flex_basis = Length::length(100);
    child_style.box.padding = {Length::length(2), Length::length(2), Length::length(2), Length::length(2)};
    child_style.box.border = {Length::length(1), Length::length(1), Length::length(1), Length::length(1)};
    std::size_t fixed_queries = 0;
    Unit content_revision = 0;
    const auto child = tree.create_node(child_style, [&](const MeasureRequest& request) {
        if (request.known_content_width && request.known_content_height) {
            ++fixed_queries;
            require(request.known_content_width == 94 && request.known_content_height == 54
                && request.available_width == AvailableSpace::definite(94)
                && request.available_height == AvailableSpace::definite(54),
                "A fixed content query must expose its final budget to the host");
        }
        return Measurement{{200 + content_revision, 100 + content_revision},
            20 + content_revision, 30 + content_revision,
            Size{300 + content_revision, 150 + content_revision}};
    });
    tree.append_child(parent, child);
    const auto& layout = tree.compute(parent,
        {AvailableSpace::definite(100), AvailableSpace::definite(60)});
    const auto* placed = layout.find(child);
    require(layout.succeeded() && fixed_queries == 1 && placed
        && placed->border_box == Rect{0, 0, 100, 60}
        && placed->content_box == Rect{3, 3, 94, 54}
        && placed->first_baseline == 23 && placed->last_baseline == 33
        && placed->scrollable_overflow_rect == Rect{0, 0, 302, 152},
        "Final content dimensions must preserve host baselines and painted overflow");
    const auto snapshot = layout;
    parent_style.box.size.width = Length::length(150);
    parent_style.justify_content = ContentAlignment{ContentPosition::End};
    tree.set_style(parent, parent_style);
    const auto& shifted = tree.compute(parent,
        {AvailableSpace::definite(150), AvailableSpace::definite(60)});
    require(shifted.succeeded() && fixed_queries == 1 && shifted.measurement_count == 0
        && shifted.find(child)->border_box == Rect{50, 0, 100, 60}
        && shifted.find(child)->content_box == Rect{53, 3, 94, 54}
        && shifted.find(child)->first_baseline == 23
        && shifted.find(child)->scrollable_overflow_rect == Rect{0, 0, 302, 152},
        "Parent placement changes must reuse content measurement without moving local overflow");
    content_revision = 5;
    tree.mark_dirty(child);
    const auto& refreshed = tree.compute(parent,
        {AvailableSpace::definite(150), AvailableSpace::definite(60)});
    require(refreshed.succeeded() && fixed_queries == 2
        && refreshed.find(child)->border_box == Rect{50, 0, 100, 60}
        && refreshed.find(child)->first_baseline == 28
        && refreshed.find(child)->last_baseline == 38
        && refreshed.find(child)->scrollable_overflow_rect == Rect{0, 0, 307, 157},
        "Dirty content must refresh baselines and painted overflow even when final dimensions stay fixed");
    require(snapshot.find(child)->border_box == Rect{0, 0, 100, 60}
        && snapshot.find(child)->first_baseline == 23
        && snapshot.find(child)->scrollable_overflow_rect == Rect{0, 0, 302, 152},
        "Retained geometry snapshots must survive placement and content updates");
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
    tree.remove_subtree(second);
    require(!tree.contains(second) && !tree.contains(nested) && tree.contains(hidden),
        "Removing a subtree must invalidate all descendants");
}

void rtl_root_coordinates() {
    for (const auto display : {Display::Block, Display::FlowRoot, Display::Flex, Display::Grid}) {
        LayoutTree rtl_tree;
        Style root_style;
        root_style.display = display;
        root_style.box.direction = Direction::Rtl;
        root_style.box.size = {Length::length(60), Length::length(40)};
        root_style.box.padding = {Length::length(4), Length::length(4),
            Length::length(4), Length::length(4)};
        root_style.box.border = {Length::length(2), Length::length(2),
            Length::length(2), Length::length(2)};
        const auto root = rtl_tree.create_node(root_style);
        Style child_style;
        child_style.display = Display::Block;
        child_style.box.size = {Length::length(20), Length::length(12)};
        child_style.box.padding = {Length::length(1), Length::length(1),
            Length::length(1), Length::length(1)};
        child_style.box.border = child_style.box.padding;
        const auto child = rtl_tree.create_node(child_style);
        rtl_tree.append_child(root, child);
        // Replay the first available space after a resize to exercise both
        // cached local fragments and published root-relative coordinates.
        for (const Unit width : {100.f, 140.f, 100.f}) {
            const auto& shifted = rtl_tree.compute(root,
                {AvailableSpace::definite(width), AvailableSpace::definite(100)});
            require(shifted.succeeded()
                && shifted.find(root)->border_box.x == width - 60
                && shifted.find(root)->content_box.x == width - 54,
                "RTL root shift must translate border and content boxes together");
            const auto* nested = shifted.find(child);
            require(nested && nested->content_box.x == nested->border_box.x + 2
                && nested->content_box.y == nested->border_box.y + 2,
                "RTL descendant content box must remain root-relative after cache replay");
        }
    }
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

void host_exception_types() {
    // The original exception type/value must survive both measurement APIs,
    // nested algorithms and both publication paths.
    std::vector<std::exception_ptr> errors;
    const auto remember = [&](auto error) {
        try { throw error; } catch (...) { errors.push_back(std::current_exception()); }
    };
    remember(std::invalid_argument("host argument"));
    remember(std::overflow_error("host overflow"));
    remember(std::length_error("host length"));
    remember(std::bad_alloc{});
    remember(42);
    const auto describe = [](const std::exception_ptr& error) -> std::string {
        if (!error) return "no exception";
        try { std::rethrow_exception(error); }
        catch (const std::exception& value) {
            return std::string(typeid(value).name()) + ":" + value.what();
        } catch (int value) { return std::to_string(value); }
    };
    for (const auto display : {Display::Block, Display::Flex, Display::Grid}) {
        for (const bool per_node : {false, true}) {
            for (const bool publish : {false, true}) {
                LayoutTree tree;
                Style style;
                style.display = display;
                style.box.size.width = Length::length(100);
                const auto root = tree.create_node(style);
                const auto leaf = tree.create_node();
                tree.append_child(root, leaf);
                struct Provider final : MeasureProvider {
                    NodeId leaf;
                    std::exception_ptr failure;
                    Measurement measure(NodeId id, const Style&, void*, const MeasureRequest&) const override {
                        if (id == leaf && failure) std::rethrow_exception(failure);
                        return {{20, 10}};
                    }
                } provider;
                provider.leaf = leaf;
                if (per_node) tree.set_measure(leaf, [&](const MeasureRequest& request) {
                    return provider.measure(leaf, tree.style(leaf), nullptr, request);
                });
                LayoutOptions options;
                options.publish_nodes = publish;
                options.measure_provider = per_node ? nullptr : &provider;
                const auto& previous = tree.compute(root, {}, options);
                require(previous.succeeded(), "Initial exception fixture failed");
                const auto snapshot = previous;
                const auto geometry = tree.layout(leaf);
                const auto* storage = previous.nodes.data();
                for (const auto& error : errors) {
                    provider.failure = error;
                    tree.mark_dirty(leaf);
                    std::exception_ptr caught;
                    try { (void)tree.compute(root, {}, options); }
                    catch (...) { caught = std::current_exception(); }
                    require(describe(caught) == describe(error), "Host exception type/value must propagate");
                    require(previous.succeeded() && previous.nodes == snapshot.nodes
                        && previous.size == snapshot.size && previous.nodes.data() == storage
                        && tree.layout(leaf) == geometry,
                        "Host failure must preserve the complete publication");
                }
                provider.failure = {};
                require(tree.compute(root, {}, options).succeeded(),
                    "Tree must recover after a host failure");
            }
        }
    }
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
    tree.set_measure(root, [](const MeasureRequest& request) {
        return Measurement{{request.available_width.value, 14}};
    });
    (void)tree.compute(root, {AvailableSpace::definite(80), AvailableSpace::indefinite()});
    const auto& previous = tree.compute(root,
        {AvailableSpace::definite(120), AvailableSpace::indefinite()});
    const auto previous_copy = previous;
    const auto* previous_nodes = previous.nodes.data();
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
    require(previous.nodes.data() == previous_nodes && previous.size == previous_copy.size
        && previous.nodes.size() == previous_copy.nodes.size()
        && previous.nodes.front().border_box == previous_copy.nodes.front().border_box,
        "A throwing callback must preserve the last complete published snapshot");
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

void multi_query_leaf_cache() {
    struct Snapshot {
        Size size;
        std::array<Rect, 4> rectangles;
        std::array<std::optional<Unit>, 2> baselines;
        std::size_t grid_reports;
        std::size_t calls;
        std::size_t layouts;
    };
    const auto run = [](std::size_t layout_limit,
                        std::size_t measurement_limit) {
        TreeLimits limits;
        limits.maximum_cached_layouts = layout_limit;
        limits.maximum_cached_measurements = measurement_limit;
        LayoutTree tree(limits);
        Style grid_style;
        grid_style.display = Display::Grid;
        grid_style.grid.template_columns = {
            TrackSizing::automatic(), TrackSizing::automatic()};
        grid_style.grid.template_rows = {TrackSizing::automatic()};
        const auto grid = tree.create_node(grid_style);
        Style flex_style;
        flex_style.display = Display::Flex;
        flex_style.flex_wrap = FlexWrap::Wrap;
        flex_style.box.max_size.width = Length::length(38);
        const auto flex = tree.create_node(flex_style);
        tree.append_child(grid, flex);
        std::size_t calls = 0;
        const auto add_leaf = [&](Size minimum, Size preferred) {
            const auto leaf = tree.create_node({}, [&, minimum, preferred](const MeasureRequest& request) {
                ++calls;
                const Size queried{
                    request.available_width.kind == AvailableKind::MinContent
                        ? minimum.width : preferred.width,
                    request.available_height.kind == AvailableKind::MinContent
                        ? minimum.height : preferred.height};
                return Measurement{{request.known_content_width.value_or(queried.width),
                    request.known_content_height.value_or(queried.height)}, {}, {}, preferred};
            });
            tree.append_child(flex, leaf);
            return leaf;
        };
        const auto first = add_leaf({10, 8}, {24, 18});
        const auto second = add_leaf({16, 11}, {29, 21});
        const auto result = tree.compute(grid,
            {AvailableSpace::min_content(), AvailableSpace::min_content()});
        require(result.succeeded() && result.measurement_count == calls,
            "Grid intrinsic measurements must count only host calls");
        return Snapshot{result.size, {result.find(grid)->border_box,
            result.find(flex)->border_box, result.find(first)->border_box,
            result.find(second)->border_box},
            {result.find(grid)->first_baseline,
                result.find(flex)->first_baseline},
            result.grid_tracks.size(), calls, result.layout_count};
    };
    const auto uncached = run(1, 1);
    const auto measured = run(1, 100'000);
    const auto cached = run(100'000, 100'000);
    require(cached.size == uncached.size && cached.rectangles == uncached.rectangles
        && cached.baselines == uncached.baselines
        && cached.grid_reports == uncached.grid_reports
        && measured.size == uncached.size
        && measured.rectangles == uncached.rectangles
        && measured.baselines == uncached.baselines
        && measured.grid_reports == uncached.grid_reports,
        "Intrinsic query reuse must preserve final geometry");
    require(measured.calls < uncached.calls,
        "Identical leaf requests should reuse measurements across layout constraints");
    require(cached.layouts < measured.layouts,
        "Repeated layout constraints should reuse staged fragments");
}

void node_storage_lifecycle() {
    LayoutTree tree, foreign;
    Style container;
    container.flex_direction = FlexDirection::Column;
    container.box.size.width = Length::length(64);
    const auto root = tree.create_node(container);
    Style leaf_style;
    leaf_style.box.size = {Length::length(8), Length::length(2)};
    leaf_style.flex_shrink = 0;
    std::vector<NodeId> leaves, outsiders, removed;
    // Interleaved trees must not alias each other's slots during storage growth.
    for (std::size_t i = 0; i < 64; ++i) {
        leaves.push_back(tree.create_node(leaf_style));
        tree.append_child(root, leaves.back());
        for (std::size_t j = 0; j < 31; ++j) outsiders.push_back(foreign.create_node());
    }
    const auto anchor = leaves[1];
    const auto* anchor_style = &tree.style(anchor);
    require(!tree.contains(NodeId{root.value & 0xffffffffULL}),
        "An identity without an epoch must not address an occupied slot");
    for (std::size_t i = 0; i < leaves.size(); i += 3) {
        tree.remove_subtree(leaves[i]);
        removed.push_back(leaves[i]);
        require(!tree.contains(NodeId{leaves[i].value & 0xffffffffULL}),
            "An identity without an epoch must not address a vacant slot");
    }
    for (std::size_t i = 0; i < 96; ++i) {
        const auto child = tree.create_node(leaf_style);
        tree.append_child(root, child);
    }
    for (const auto id : removed) require(!tree.contains(id), "Removed identities must remain absent");
    for (const auto id : outsiders) require(!tree.contains(id), "Foreign identities must remain absent");
    for (const auto id : tree.children(root)) {
        require(tree.contains(id) && tree.parent(id) == root,
            "Deletion and directory growth must preserve surviving relationships");
    }
    require(&tree.style(anchor) == anchor_style && *anchor_style == leaf_style,
        "Node storage growth must retain references to surviving styles");
    const auto count = tree.children(root).size();
    const auto result = tree.compute(root,
        {AvailableSpace::definite(64), AvailableSpace::max_content()});
    require(result.succeeded() && result.nodes.size() == count + 1
        && result.size == Size{64, static_cast<Unit>(2 * count)},
        "Surviving and newly inserted nodes must all participate in layout");
    tree.remove_subtree(root);
    require(!tree.contains(root) && !tree.contains(anchor), "Subtree removal must release all identities");
}

void cache_failure_rollback() {
    for (const auto display : {Display::Block, Display::Flex}) {
        LayoutTree tree;
        Style root_style;
        root_style.display = display;
        root_style.flex_direction = FlexDirection::Column;
        root_style.box.size.width = Length::percent(1);
        const auto root = tree.create_node(root_style);
        std::size_t first_calls{};
        bool reject_second{};
        const auto first = tree.create_node({}, [&](const MeasureRequest& request) {
            ++first_calls;
            return Measurement{{request.known_content_width.value_or(1), 10}};
        });
        const auto second = tree.create_node({}, [&](const MeasureRequest& request) {
            return Measurement{{reject_second ? std::numeric_limits<Unit>::quiet_NaN()
                : request.known_content_width.value_or(1), 10}};
        });
        tree.append_child(root, first);
        tree.append_child(root, second);
        const AvailableSize original{AvailableSpace::definite(100), AvailableSpace::max_content()};
        const AvailableSize resized{AvailableSpace::definite(200), AvailableSpace::max_content()};
        reject_second = true;
        require(tree.compute(root, original).error.code == LayoutErrorCode::InvalidMeasurement,
            "The first transaction must fail after staging the earlier sibling");
        const auto initially_failed_calls = first_calls;
        reject_second = false;
        require(tree.compute(root, original).succeeded() && first_calls > initially_failed_calls,
            "A first-compute failure must discard newly initialized node caches");
        const auto initial_calls = first_calls;
        reject_second = true;
        const auto failure = tree.compute(root, resized);
        require(failure.error.code == LayoutErrorCode::InvalidMeasurement && first_calls > initial_calls,
            "Failure must follow a staged measurement for the earlier sibling");
        const auto failed_calls = first_calls;
        const auto restored = tree.compute(root, original);
        require(restored.succeeded() && restored.cache_hit && first_calls == failed_calls,
            "A failed transaction must preserve previously committed root geometry");
        reject_second = false;
        const auto retried = tree.compute(root, resized);
        require(retried.succeeded() && retried.size == Size{200, 20} && first_calls > failed_calls,
            "A retry must discard measurements staged by the failed transaction");
    }
}

void cache_capacity_churn() {
    const auto run = [](std::size_t capacity) {
        TreeLimits limits;
        limits.maximum_cached_layouts = capacity;
        limits.maximum_cached_measurements = capacity;
        LayoutTree tree(limits);
        Style root_style;
        root_style.display = Display::Block;
        root_style.box.size.width = Length::percent(1);
        const auto root = tree.create_node(root_style);
        std::size_t revision{}, host_calls{};
        std::array<NodeId, 3> children;
        for (std::size_t i = 0; i < children.size(); ++i) {
            children[i] = tree.create_node({}, [&, i](const MeasureRequest& request) {
                ++host_calls;
                const Unit width = request.known_content_width.value_or(1);
                const Unit height = 10 + static_cast<Unit>(i + revision) + width / 8;
                return Measurement{{width, height}, height - 2, height - 1};
            });
            tree.append_child(root, children[i]);
        }
        std::vector<Rect> geometry;
        for (std::size_t frame = 0; frame < 96; ++frame) {
            // More distinct keys than either cache can retain, then reverse
            // their order to exercise collision replacement and reuse.
            const auto phase = frame % 48;
            const Unit width = 64 + static_cast<Unit>(phase < 24 ? phase : 47 - phase) * 3;
            if (frame == 48) {
                ++revision;
                for (const auto child : children) tree.mark_dirty(child);
            }
            const auto& result = tree.compute(root,
                {AvailableSpace::definite(width), AvailableSpace::max_content()});
            require(result.succeeded() && result.nodes.size() == 4,
                "Cache replacement and invalidation must preserve complete output");
            for (const auto& layout : result.nodes) geometry.push_back(layout.border_box);
        }
        if (capacity == 100'000) {
            const auto replay = [&] {
                for (int step = 0; step < 7; ++step) {
                    require(tree.compute(root, {AvailableSpace::definite(64 + step * 3.0f),
                        AvailableSpace::max_content()}).succeeded(), "Hot queries must succeed");
                }
            };
            replay();
            const auto warmed_calls = host_calls;
            replay();
            require(host_calls == warmed_calls,
                "A small resident working set must survive hash collisions and size/full queries");
        }
        tree.remove_subtree(root);
        const auto leaf = tree.create_node({}, [](const MeasureRequest&) {
            return Measurement{{17, 13}};
        });
        require(tree.compute(leaf, {}).size == Size{17, 13},
            "Removing cached nodes must release the tree cache budget");
        return geometry;
    };
    const auto constrained = run(1);
    for (const auto capacity : {std::size_t{3}, std::size_t{16}, std::size_t{100'000}}) {
        require(run(capacity) == constrained,
            "Cache budgets, collisions and eviction must not change layout geometry");
    }
}

void cached_local_overflow() {
    LayoutTree tree;
    Style root_style;
    root_style.box.size = {Length::percent(1), Length::length(50)};
    root_style.justify_content = ContentAlignment{ContentPosition::SpaceBetween};
    const auto root = tree.create_node(root_style);
    Style panel_style;
    panel_style.box.size = {Length::length(20), Length::length(20)};
    panel_style.flex_shrink = 0;
    Style child_style;
    child_style.position = Position::Absolute;
    child_style.inset.left = Length::length(-5.25f);
    child_style.inset.top = Length::length(3.5f);
    child_style.box.size = {Length::length(40.5f), Length::length(30.25f)};
    std::array<NodeId, 2> panels, children;
    for (std::size_t i = 0; i < panels.size(); ++i) {
        panels[i] = tree.create_node(panel_style);
        children[i] = tree.create_node(child_style);
        tree.append_child(panels[i], children[i]);
        tree.append_child(root, panels[i]);
    }
    const auto snapshot = tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::max_content()});
    const Rect expected{-5.25f, 0, 40.5f, 33.75f};
    require(snapshot.succeeded() && snapshot.find(panels[1])->scrollable_overflow_rect == expected,
        "Container overflow must include its positioned child's local extent");
    for (const Unit width : {100'000'000.0f, 140.0f, 120'000'000.0f}) {
        const auto& result = tree.compute(root,
            {AvailableSpace::definite(width), AvailableSpace::max_content()});
        require(result.succeeded() && result.cache_hits > 0,
            "Container translation must reuse unchanged child layout queries");
        for (const auto panel : panels) {
            require(result.find(panel)->scrollable_overflow_rect == expected,
                "Cached local overflow must not lose precision through ancestor translation");
        }
    }
    child_style.box.size.width = Length::length(60.5f);
    tree.set_style(children[1], child_style);
    const auto& changed = tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::max_content()});
    require(changed.succeeded()
        && changed.find(panels[0])->scrollable_overflow_rect == expected
        && changed.find(panels[1])->scrollable_overflow_rect == Rect{-5.25f, 0, 60.5f, 33.75f},
        "An edited descendant must update only its owning container's cached overflow");
    tree.remove_subtree(root);
    require(snapshot.find(panels[1])->scrollable_overflow_rect == expected,
        "Published overflow snapshots must survive cache reuse and tree removal");
}

void geometry_snapshot_lifetime() {
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Flex;
    root_style.box.size = {Length::percent(1), Length::length(80)};
    root_style.align_items = ItemAlignment{ItemPosition::Start};
    root_style.justify_content = ContentAlignment{ContentPosition::SpaceBetween};
    const auto root = tree.create_node(root_style);
    Style leaf_style;
    leaf_style.box.size = {Length::length(20), Length::length(20)};
    const auto left = tree.create_node(leaf_style);
    auto middle_style = root_style;
    middle_style.box.size = {Length::length(40), Length::length(30)};
    middle_style.flex_direction = FlexDirection::Column;
    middle_style.justify_content = ContentAlignment{ContentPosition::Start};
    const auto middle = tree.create_node(middle_style);
    const auto right = tree.create_node(leaf_style);
    leaf_style.box.size = {Length::length(10), Length::length(10)};
    const auto first = tree.create_node(leaf_style);
    const auto last = tree.create_node(leaf_style);
    tree.append_child(middle, first);
    tree.append_child(middle, last);
    tree.append_child(root, left);
    tree.append_child(root, middle);
    tree.append_child(root, right);
    const auto snapshot = tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::definite(80)});
    for (const Unit width : {200.0F, 140.0F, 100.0F, 200.0F}) {
        const auto& result = tree.compute(root,
            {AvailableSpace::definite(width), AvailableSpace::definite(80)});
        const Unit middle_x = 20 + (width - 80) / 2;
        require(result.succeeded() && result.nodes.size() == 6
            && result.find(left)->border_box == Rect{0, 0, 20, 20}
            && result.find(middle)->border_box == Rect{middle_x, 0, 40, 30}
            && result.find(last)->border_box == Rect{middle_x, 10, 10, 10}
            && result.find(right)->border_box == Rect{width - 20, 0, 20, 20},
            "Cached mixed subtrees must preserve order and apply placement only once");
    }
    leaf_style.box.size.width = Length::length(12);
    tree.set_style(first, leaf_style);
    const auto& changed = tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::definite(80)});
    require(changed.succeeded() && changed.find(first)->border_box == Rect{30, 0, 12, 10}
        && snapshot.find(first)->border_box == Rect{30, 0, 10, 10},
        "Publishing changed geometry must preserve previous result snapshots");

    LayoutTree flat;
    root_style.flex_direction = FlexDirection::Column;
    root_style.box.size.height = Length::length(3000);
    root_style.justify_content = ContentAlignment{ContentPosition::Start};
    const auto flat_root = flat.create_node(root_style);
    leaf_style.box.size = {Length::length(20), Length::length(2)};
    for (int i = 0; i < 600; ++i) flat.append_child(flat_root, flat.create_node(leaf_style));
    for (const Unit width : {200.0F, 300.0F, 200.0F}) {
        const auto& result = flat.compute(flat_root,
            {AvailableSpace::definite(width), AvailableSpace::definite(3000)});
        require(result.succeeded() && result.nodes.size() == 601,
            "Publishing a large flat result must retain every node");
        for (std::size_t i = 0; i < 600; ++i) {
            require(result.nodes[i + 1].border_box == Rect{0, static_cast<Unit>(i * 2), 20, 2},
                "Large flat cached leaf results must survive buffer publication");
        }
    }
}

void container_scrollbar_ratio_bounds() {
    for (const auto display : {Display::Flex, Display::Grid}) {
        LayoutTree tree;
        Style style;
        style.display = display;
        style.box.box_sizing = BoxSizing::ContentBox;
        style.box.aspect_ratio = 2.0F;
        style.box.size.height = Length::length(100);
        style.box.max_size.width = Length::length(60);
        style.box.padding = {Length::length(10), Length::length(10),
            Length::length(10), Length::length(10)};
        style.overflow_y = Overflow::Scroll;
        style.scrollbar_width = 10;
        const auto root = tree.create_node(style);
        tree.append_child(root, tree.create_node());
        const auto result = tree.compute(root,
            {AvailableSpace::definite(500), AvailableSpace::definite(500)});
        require(result.succeeded() && result.size == Size{80, 50},
            "Container max-size ratio transfer uses decoration before scrollbar reservation");
        require(result.nodes.front().scrollbar.width == 10,
            "Scrollbar space must still be reserved in the final content box");
    }
    // A cached box's maximum ratio transfer changes when the same node gains
    // or loses children. Style and percentage bases stay unchanged here.
    LayoutTree tree;
    Style style;
    style.box.aspect_ratio = 2.0F;
    style.box.max_size.width = Length::length(100);
    const auto root = tree.create_node(style, [](const MeasureRequest&) {
        return Measurement{{140, 90}};
    });
    const AvailableSize available{AvailableSpace::max_content(), AvailableSpace::max_content()};
    require(tree.compute(root, available).size == Size{100, 90},
        "A leaf must not transfer the maximum width into a maximum height");
    Style child_style;
    child_style.box.size = {Length::length(140), Length::length(90)};
    const auto child = tree.create_node(child_style);
    tree.append_child(root, child);
    require(tree.compute(root, available).size == Size{100, 50},
        "Gaining children must refresh the cached container ratio bound");
    tree.detach(child);
    require(tree.compute(root, available).size == Size{100, 90},
        "Losing the last child must restore the cached leaf ratio bound");

    // Equal box styles on different nodes remain independent when one is a
    // container and another is a measured leaf. Churn unrelated styles before
    // revisiting both, then change only one sibling's style and topology.
    LayoutTree siblings;
    Style column;
    column.flex_direction = FlexDirection::Column;
    column.align_items = ItemAlignment{ItemPosition::Start};
    column.box.size.width = Length::length(400);
    const auto parent = siblings.create_node(column);
    style.flex_shrink = 0;
    const auto measured = siblings.create_node(style, [](const MeasureRequest&) {
        return Measurement{{140, 90}};
    });
    const auto container = siblings.create_node(style);
    siblings.append_child(parent, measured);
    siblings.append_child(parent, container);
    const auto nested = siblings.create_node(child_style);
    siblings.append_child(container, nested);
    std::vector<NodeId> varied;
    for (int i = 0; i < 80; ++i) {
        Style unique;
        unique.flex_shrink = 0;
        unique.box.size = {Length::length(10 + static_cast<Unit>(i)), Length::length(3)};
        varied.push_back(siblings.create_node(unique));
        siblings.append_child(parent, varied.back());
    }
    for (int frame = 0; frame < 3; ++frame) {
        const auto& layout = siblings.compute(parent,
            {AvailableSpace::definite(400 + static_cast<Unit>(frame)), AvailableSpace::max_content()});
        require(layout.succeeded() && layout.find(measured)->border_box.width == 100
            && layout.find(measured)->border_box.height == 50
            && layout.find(container)->border_box.width == 100
            && layout.find(container)->border_box.height == 50,
            "Flex item ratio bounds must survive shared style resolution");
        for (std::size_t i = 0; i < varied.size(); ++i) {
            require(layout.find(varied[i])->border_box.width == 10 + static_cast<Unit>(i),
                "Resolved box ownership must survive unrelated style eviction");
        }
        require(siblings.compute(measured, available).size == Size{100, 90}
            && siblings.compute(container, available).size == Size{100, 50},
            "The same tree must distinguish leaf and container bounds in root queries");
        siblings.mark_dirty(measured);
    }
    auto changed = siblings.style(varied.front());
    changed.box.size.width = Length::calc(Calculation::add(Calculation::length(7), Calculation::percent(0.1F)));
    siblings.set_style(varied.front(), changed);
    const auto copied = siblings.create_node(changed);
    const auto moved = siblings.create_node(std::move(changed));
    siblings.append_child(parent, copied);
    siblings.append_child(parent, moved);
    siblings.detach(nested);
    siblings.set_measure(container, [](const MeasureRequest&) { return Measurement{{140, 90}}; });
    const auto& updated = siblings.compute(parent,
        {AvailableSpace::definite(400), AvailableSpace::max_content()});
    require(updated.succeeded() && updated.find(container)->border_box.height == 50
        && updated.find(measured)->border_box.height == 50,
        "Equivalent measured siblings must retain their parent's Flex item constraints");
    for (const auto id : {varied.front(), copied, moved}) {
        require(updated.find(id)->border_box.width == 47,
            "Copied and moved styles must retain calc ownership across later computations");
    }
    require(siblings.compute(container, available).size == Size{100, 90}
        && siblings.compute(measured, available).size == Size{100, 90},
        "A sibling topology change must refresh only its own container semantics");
}

void cached_subtree_depth_limit() {
    for (const auto display : {Display::Flex, Display::Block, Display::None}) {
        LayoutTree tree(TreeLimits{.maximum_depth = 3});
        Style style;
        style.display = display;
        style.flex_direction = FlexDirection::Column;
        style.box.size = {Length::length(100), Length::length(100)};
        style.box.min_size = {Length::length(0), Length::length(0)};
        std::array<NodeId, 4> nodes;
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            nodes[i] = tree.create_node(style);
            if (i) tree.append_child(nodes[i - 1], nodes[i]);
        }
        const AvailableSize available{AvailableSpace::definite(100), AvailableSpace::definite(100)};
        require(tree.compute(nodes[1], available).succeeded(), "A subtree within the depth limit must compute");
        require(tree.compute(nodes[0], available).error.code == LayoutErrorCode::LimitExceeded,
            "A cached subtree must not bypass the depth limit when computed from a higher root");
        require(tree.compute(nodes[1], available).succeeded(),
            "A rejected deeper query must preserve valid shallower cached results");
    }
}

void deep_flex_query_budget() {
    // Width-dependent measurement below a chain of auto-height containers
    // must not multiply work at every ancestor. Verify geometry as well as
    // the work bound, including resize and measurement invalidation.
    for (const std::size_t depth : {4U, 12U, 24U, 48U}) {
        LayoutTree tree;
        Style wrapper;
        wrapper.display = Display::Flex;
        wrapper.flex_direction = FlexDirection::Column;
        wrapper.box.min_size = {Length::length(0), Length::length(0)};
        wrapper.box.padding = {Length::length(1), Length::length(1),
            Length::length(1), Length::length(1)};
        wrapper.gap = {Length::length(2), Length::length(2)};
        auto root_style = wrapper;
        root_style.box.size.width = Length::percent(1);
        const auto root = tree.create_node(root_style);
        auto parent = root;
        for (std::size_t level = 0; level < depth; ++level) {
            const auto child = tree.create_node(wrapper);
            tree.append_child(parent, child);
            parent = child;
        }
        std::array<Unit, 16> preferred;
        std::array<NodeId, 16> leaves;
        for (std::size_t i = 0; i < leaves.size(); ++i) {
            preferred[i] = 180 + static_cast<Unit>(i % 5) * 40;
            Style leaf_style;
            leaf_style.box.min_size = {Length::length(0), Length::length(0)};
            leaf_style.flex_shrink = 0;
            leaves[i] = tree.create_node(leaf_style, [&, i](const MeasureRequest& request) {
                Unit width = request.available_width.kind == AvailableKind::MinContent
                    ? 40 : preferred[i];
                if (const auto limit = request.available_width.definite_value()) {
                    width = std::min(width, *limit);
                }
                width = request.known_content_width.value_or(width);
                const Unit height = 20 * std::max(Unit{1},
                    std::ceil(preferred[i] / std::max(Unit{1}, width)));
                return Measurement{{width, request.known_content_height.value_or(height)}, {}, {}};
            });
            tree.append_child(parent, leaves[i]);
        }
        for (const Unit width : {120.0F, 480.0F, 133.0F, 120.0F}) {
            preferred[0] += 8;
            tree.mark_dirty(leaves[0]);
            const auto& result = tree.compute(root,
                {AvailableSpace::definite(width), AvailableSpace::max_content()});
            require(result.succeeded(), "Deep Flex must complete within default limits");
            const Unit inset = static_cast<Unit>(depth + 1);
            const Unit leaf_width = width - 2 * inset;
            Unit y = inset;
            for (std::size_t i = 0; i < leaves.size(); ++i) {
                const Unit height = 20 * std::max(Unit{1}, std::ceil(preferred[i] / leaf_width));
                require(result.find(leaves[i])->border_box == Rect{inset, y, leaf_width, height},
                    "Deep Flex wrapping geometry must match the available content width");
                y += height + 2;
            }
            require(result.size == Size{width, y - 2 + inset},
                "Deep Flex must propagate auto heights through every ancestor");
            require(result.measurement_count <= leaves.size() * 8
                && result.layout_count <= result.nodes.size() * 32,
                "Deep Flex query work must remain linear in nesting depth");
            const auto& reused = tree.compute(root,
                {AvailableSpace::definite(width), AvailableSpace::max_content()});
            require(reused.cache_hit && reused.layout_count == 0
                && reused.measurement_count == 0, "Unchanged deep Flex must reuse its result");
        }
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected test case name");
        const std::string_view name = argv[1];
        if (name == "leaf_and_block") leaf_and_block();
        else if (name == "leaf_available_constraints") leaf_available_constraints();
        else if (name == "topology_and_hidden") topology_and_hidden();
        else if (name == "intrinsic_queries") intrinsic_queries();
        else if (name == "rtl_root_coordinates") rtl_root_coordinates();
        else if (name == "intrinsic_container_invalidation") intrinsic_container_invalidation();
        else if (name == "errors_and_exception_atomicity") errors_and_exception_atomicity();
        else if (name == "host_exception_types") host_exception_types();
        else if (name == "cache_and_dirty") cache_and_dirty();
        else if (name == "inherited_float_invalidation") inherited_float_invalidation();
        else if (name == "plain_block_single_measure") plain_block_single_measure();
        else if (name == "multi_query_leaf_cache") multi_query_leaf_cache();
        else if (name == "deep_flex_query_budget") deep_flex_query_budget();
        else if (name == "node_storage_lifecycle") node_storage_lifecycle();
        else if (name == "cache_failure_rollback") cache_failure_rollback();
        else if (name == "cache_capacity_churn") cache_capacity_churn();
        else if (name == "cached_local_overflow") cached_local_overflow();
        else if (name == "cached_subtree_depth_limit") cached_subtree_depth_limit();
        else if (name == "container_scrollbar_ratio_bounds") container_scrollbar_ratio_bounds();
        else if (name == "geometry_snapshot_lifetime") geometry_snapshot_lifetime();
        else throw std::runtime_error("Unknown test case");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
