#include "../../examples/nested_grid_adapter.hpp"

#include <array>
#include <cmath>
#include <exception>
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
#include <string_view>

namespace {

using namespace axisly;
using namespace axisly::examples;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool close(Unit actual, Unit expected) {
    return std::isfinite(actual) && std::abs(static_cast<double>(actual) - expected)
        <= std::max(0.00001, std::abs(static_cast<double>(expected)) * 0.000001);
}

bool same(Rect a, Rect b) {
    return close(a.x, b.x) && close(a.y, b.y) && close(a.width, b.width) && close(a.height, b.height);
}

GridSpec fixed(Unit width = 100, Unit height = 80) {
    return {.columns = {Track::fixed(width)}, .rows = {Track::fixed(height)}};
}

NestedGrid leaf(GridSpec spec = fixed()) {
    return {.spec = std::move(spec), .items = {{.id = 0, .preferred_size = {80, 30}}}};
}

NestedGrid parent(const NestedGrid& child, GridSpec spec = fixed(), ItemId id = 1) {
    return {.spec = std::move(spec), .items = {{.id = id}}, .children = {{id, &child}}};
}

const NestedItemLayout& at(const NestedResult& result, std::initializer_list<ItemId> path) {
    require(result.succeeded(), "Expected a successful nested layout");
    const auto found = std::find_if(result.items.begin(), result.items.end(),
        [&](const NestedItemLayout& item) { return item.path == std::vector<ItemId>(path); });
    require(found != result.items.end(), "Missing scoped item path");
    return *found;
}

void expect_error(const NestedResult& result, NestedErrorCode code, std::initializer_list<ItemId> path) {
    require(!result.succeeded() && result.error.code == code, "Unexpected nested failure code");
    require(result.error.path == std::vector<ItemId>(path), "Error must identify the full nested path, including ID zero");
    require(result.items.empty() && result.size == Size{} && !result.error.message.empty(), "Failure must not expose partial geometry");
}

struct Metrics {
    MeasureResult value{{20, 10}, {80, 30}};
    std::vector<MeasureInput> calls;
    bool wrap{};
    bool invalid{};
    bool throw_error{};
    bool fail_final_height{};

    static MeasureResult measure(ItemId, const MeasureInput& input, void* data) {
        auto& host = *static_cast<Metrics*>(data);
        host.calls.push_back(input);
        if (host.throw_error) throw &host;
        auto result = host.value;
        if (host.wrap && input.known_width) {
            const Unit height = 10 * std::ceil(240.0f / std::max(1.0f, *input.known_width));
            result.minimum.height = result.preferred.height = height;
        }
        if (host.invalid || (host.fail_final_height && input.available.height == 20)) {
            result.minimum.width = std::numeric_limits<Unit>::quiet_NaN();
        }
        return result;
    }
};

NestedGrid measured(Metrics& host, Track column = Track::content()) {
    return {.spec = {.columns = {column}, .rows = {Track::content()}},
            .items = {{.id = 0}}, .measure = Metrics::measure, .user_data = &host};
}

void lambda_capture() {
    const auto grid = leaf({.columns = {Track::content()}, .rows = {Track::content()}});
    int calls = 0;
    const auto result = compute_grid(grid.spec, grid.items, {50, 100},
        [&](ItemId id, const MeasureInput& input) -> MeasureResult {
            require(id == 0, "Capturing callable must receive the original ID");
            if (calls == 1) require(input.known_width == 50, "Capturing callable must receive the final width");
            ++calls;
            return {{20, 10}, {80, 30}};
        });
    require(result.succeeded() && result.size == Size{50, 30} && calls == 2, "Capturing lambda must use ordinary measurement semantics");
}

void lambda_move_only() {
    auto callable = [counter = std::make_unique<int>(0)](ItemId, const MeasureInput&) mutable -> MeasureResult {
        ++*counter;
        return {{}, {80, static_cast<Unit>(*counter)}};
    };
    const auto grid = leaf({.columns = {Track::content()}, .rows = {Track::content()}});
    const auto first = compute_grid(grid.spec, grid.items, {100, 100}, callable);
    const auto second = compute_grid(grid.spec, grid.items, {100, 100}, std::move(callable));
    require(first.size.height == 2 && second.size.height == 4, "Adapter must not copy, move from or retain the callable");
    const auto temporary = compute_grid(grid.spec, grid.items, {100, 100},
        [value = std::make_unique<int>(25)](ItemId, const MeasureInput&) -> MeasureResult { return {{}, {80, static_cast<Unit>(*value)}}; });
    require(temporary.succeeded() && temporary.size.height == 25, "Move-only temporaries must remain alive through synchronous calls");
}

MeasureResult two_arguments(ItemId, const MeasureInput&) { return {{}, {80, 30}}; }

void lambda_const_function() {
    const auto grid = leaf();
    const auto callable = [](ItemId, const MeasureInput&) -> MeasureResult { return {{}, {80, 30}}; };
    require(compute_grid(grid.spec, grid.items, {}, callable).succeeded(), "Const lambda must be accepted");
    require(compute_grid(grid.spec, grid.items, {}, two_arguments).succeeded(), "Function references must be accepted");
    require(compute_grid(grid.spec, grid.items, {}, &two_arguments).succeeded(), "Function pointers must be accepted");
}

void lambda_raw_compatibility() {
    const auto grid = leaf();
    Metrics host;
    const auto raw = compute_grid(grid.spec, grid.items, {}, Metrics::measure, &host);
    const auto empty = compute_grid(grid.spec, grid.items, {}, nullptr);
    const auto no_context = +[](ItemId, const MeasureInput&, void*) -> MeasureResult { return {{}, {80, 30}}; };
    require(raw.succeeded() && raw.measurement_count == 2 && host.calls.size() == 2, "Raw callback with context must retain its overload");
    require(empty.succeeded() && empty.measurement_count == 0, "Null callback must retain static fallback");
    require(compute_grid(grid.spec, grid.items, {}, no_context).measurement_count == 2, "Raw callback without explicit context must retain its overload");
}

void lambda_exceptions() {
    const auto grid = leaf();
    int marker{};
    bool caught = false;
    try {
        (void)compute_grid(grid.spec, grid.items, {}, [&](ItemId, const MeasureInput& input) -> MeasureResult {
            if (input.known_width) throw &marker;
            return {{}, {80, 30}};
        });
    } catch (int* value) { caught = value == &marker; }
    require(caught, "Callable exceptions must propagate without translation");
}

void lambda_reentrant() {
    const auto grid = leaf();
    int inner_calls = 0;
    const auto result = compute_grid(grid.spec, grid.items, {}, [&](ItemId, const MeasureInput&) -> MeasureResult {
        const auto inner = compute_grid(grid.spec, grid.items, {}, [&](ItemId, const MeasureInput&) -> MeasureResult {
            ++inner_calls;
            return {{}, {80, 30}};
        });
        require(inner.succeeded(), "A lambda may perform an independent layout");
        return {{}, inner.size};
    });
    require(result.succeeded() && inner_calls == 4 && result.measurement_count == 2, "Nested lambdas must not share callback state");
}

void flat_equivalence() {
    auto grid = leaf({.columns = {Track::weighted()}, .rows = {Track::content()}, .padding = {1, 2, 3, 4}});
    const auto ordinary = compute_grid(grid.spec, grid.items, {90, 100});
    const auto nested = compute_nested_grid(grid, {90, 100});
    require(nested.succeeded() && nested.size == ordinary.size && nested.items.size() == ordinary.items.size(), "A flat host grid must match the core");
    require(at(nested, {0}).rect == ordinary.items[0].rect && at(nested, {0}).cell_rect == ordinary.items[0].cell_rect, "Adapter must retain flat geometry");
    require(nested.layout_count == 1 && nested.measurement_count == 2, "Counters must include actual core and adapter calls");
    Metrics host;
    grid.measure = Metrics::measure;
    grid.user_data = &host;
    const auto callback_result = compute_nested_grid(grid, {90, 100});
    require(callback_result.size == Size{90, 36} && host.calls.size() == 2, "Ordinary host callbacks must be forwarded");
}

void fixed_child() {
    auto child = leaf(fixed(40, 30));
    child.spec.padding = {2, 3, 4, 5};
    auto root = parent(child);
    root.spec.padding = {10, 20, 30, 40};
    root.items[0].margin = {3, 4, 5, 6};
    const auto result = compute_nested_grid(root, {500, 500});
    require(result.size == Size{140, 140}, "Root occupied extent must remain local to the root grid");
    require(at(result, {1}).rect == Rect{13, 24, 92, 70}, "Parent margins must be applied before child layout");
    require(at(result, {1, 0}).rect == Rect{15, 27, 40, 30}, "Child padding must be translated from the aligned parent origin");
}

void content_child() {
    Metrics host;
    const auto child = measured(host);
    const auto root = parent(child, {.columns = {Track::content()}, .rows = {Track::content()}});
    const auto preferred = compute_nested_grid(root, {200, 200});
    const auto shrunk = compute_nested_grid(root, {50, 200});
    const auto minimum = compute_nested_grid(root, {});
    require(preferred.size == Size{80, 30} && shrunk.size == Size{50, 30} && minimum.size == Size{20, 10}, "Child probes must expose finite preferred and minimum bounds");
    require(at(shrunk, {1, 0}).rect == Rect{0, 0, 50, 30}, "Content child must be recomputed at the parent's allocated size");
}

void width_reflow() {
    Metrics host;
    host.wrap = true;
    host.value.minimum.width = 0;
    auto child = measured(host, Track::weighted());
    child.spec.padding = {5, 3, 5, 7};
    const NestedGrid root{
        .spec = {.columns = {Track::fixed(30), Track::weighted()}, .rows = {Track::content()}, .column_gap = 10},
        .items = {{.id = 1, .preferred_size = {20, 15}}, {.id = 2, .column = 1}}, .children = {{2, &child}}
    };
    const auto narrow = compute_nested_grid(root, {130, 300});
    require(narrow.size == Size{130, 40} && at(narrow, {2, 0}).rect == Rect{45, 3, 80, 30}, "Nested text must reflow at width after all padding");
    require(host.calls.back().known_width == 80, "Last child measurement must use its final width");
    const auto wide = compute_nested_grid(root, {210, 300});
    require(wide.size == Size{210, 30} && at(wide, {2, 0}).rect == Rect{45, 3, 160, 20}, "Resizing must update nested height and geometry");
    require(host.calls.back().known_width == 160, "Resize must not reuse a stale measurement probe");
}

void aligned_child() {
    const auto child = leaf({.columns = {Track::content()}, .rows = {Track::content()}});
    auto root = parent(child, fixed(200, 100));
    root.items[0].margin = {10, 5, 20, 15};
    root.items[0].horizontal_alignment = HorizontalAlignment::Center;
    root.items[0].vertical_alignment = VerticalAlignment::End;
    const auto result = compute_nested_grid(root, {500, 500});
    require(at(result, {1}).cell_rect == Rect{0, 0, 200, 100}, "Keep the parent cell before alignment");
    require(at(result, {1}).rect == Rect{55, 55, 80, 30} && at(result, {1, 0}).rect == Rect{55, 55, 80, 30}, "Child origin and size must come from the aligned rect");
}

void scoped_shared_ids() {
    const auto child = leaf({.columns = {Track::weighted()}, .rows = {Track::weighted()}});
    const NestedGrid root{
        .spec = {.columns = {Track::fixed(40), Track::fixed(60)}, .rows = {Track::fixed(20)}},
        .items = {{.id = 0}, {.id = 1, .column = 1}}, .children = {{0, &child}, {1, &child}}
    };
    const auto result = compute_nested_grid(root, {100, 20});
    require(result.items.size() == 4 && at(result, {0, 0}).rect == Rect{0, 0, 40, 20}
        && at(result, {1, 0}).rect == Rect{40, 0, 60, 20}, "Sibling aliases are not cycles and must have distinct scoped coordinates");
    const std::vector<std::vector<ItemId>> order{{0}, {0, 0}, {1}, {1, 0}};
    for (std::size_t index = 0; index < order.size(); ++index) require(result.items[index].path == order[index], "Result order must be preorder in input order");
}

void three_levels() {
    auto child = leaf(fixed(10, 20));
    child.spec.padding = {3, 3, 3, 3};
    auto middle = parent(child, fixed(40, 50), 0);
    middle.spec.padding = {2, 2, 2, 2};
    auto root = parent(middle, fixed(100, 100), 0);
    root.spec.padding = {1, 1, 1, 1};
    const auto result = compute_nested_grid(root, {200, 200});
    require(at(result, {0, 0, 0}).rect == Rect{6, 6, 10, 20}, "Offsets must accumulate across every nesting level");
    require(result.items.size() == 3 && result.size == Size{102, 102}, "Nested descendants must not replace root sizing");
}

void spanning_child() {
    auto child = leaf({.columns = {Track::weighted(), Track::weighted(2)},
                       .rows = {Track::weighted(), Track::weighted()}, .column_gap = 5, .row_gap = 2});
    child.items[0].column_span = 2;
    child.items[0].row_span = 2;
    auto root = parent(child, {.columns = {Track::fixed(40), Track::fixed(60)}, .rows = {Track::fixed(30), Track::fixed(40)},
                               .column_gap = 10, .row_gap = 7, .padding = {7, 1, 0, 0}});
    root.items[0].column_span = 2;
    root.items[0].row_span = 2;
    root.items[0].margin = {2, 3, 3, 4};
    const auto result = compute_nested_grid(root, {300, 100});
    require(at(result, {1, 0}).rect == Rect{9, 4, 105, 70}, "Nested spans must use the parent's merged and margin-adjusted size on both axes");
}

void advisory_dependent_probes() {
    auto child = leaf({.columns = {Track::content()}, .rows = {Track::content()}});
    child.measure = +[](ItemId, const MeasureInput& input, void*) -> MeasureResult {
        const Size size{input.available.width == 0 ? 100.0f : 20.0f, input.available.height == 0 ? 90.0f : 10.0f};
        return {size, size};
    };
    const auto root = parent(child, {.columns = {Track::content()}, .rows = {Track::content()}});
    const auto result = compute_nested_grid(root, {100, 100});
    require(result.size == Size{20, 10} && at(result, {1, 0}).rect == Rect{0, 0, 20, 10},
            "Cross-axis advisory callbacks must produce ordered sampled bounds, not an invalid minimum greater than preferred");
}

void range_child() {
    Metrics host;
    host.value = {{30, 10}, {120, 20}};
    const auto child = measured(host, Track::range(Track::content(), Track::fixed(100)));
    const auto root = parent(child, {.columns = {Track::range(Track::content(), Track::fixed(80))}, .rows = {Track::content()}});
    const auto result = compute_nested_grid(root, {200, 100});
    require(result.size == Size{80, 20} && at(result, {1, 0}).rect.width == 80, "Parent cap must be forwarded to the child final layout");
}

void known_zero_width() {
    Metrics host;
    host.value = {{0, 12}, {100, 12}};
    const auto child = measured(host, Track::weighted());
    const auto root = parent(child, {.columns = {Track::fixed(0)}, .rows = {Track::content()}});
    const auto result = compute_nested_grid(root, {500, 100});
    require(result.size == Size{0, 12} && at(result, {1, 0}).rect.width == 0, "Known zero width must not fall back to advisory space");
    require(host.calls.back().known_width.has_value() && *host.calls.back().known_width == 0, "Leaf must receive an engaged zero-width constraint");
}

void empty_grids() {
    const auto empty = compute_nested_grid({}, {});
    require(empty.succeeded() && empty.size == Size{} && empty.items.empty() && empty.layout_count == 1 && empty.measurement_count == 0, "Empty root must remain valid");
    const NestedGrid child{.spec = {.padding = {1, 2, 3, 4}}};
    const auto root = parent(child, {.columns = {Track::content()}, .rows = {Track::content()}});
    const auto result = compute_nested_grid(root, {});
    require(result.succeeded() && result.size == Size{4, 6} && result.items.size() == 1, "Empty nested grid still contributes its padding");
}

void overflowing_child() {
    const auto child = leaf(fixed(80, 60));
    const auto root = parent(child, fixed(20, 20));
    const auto result = compute_nested_grid(root, {});
    require(result.size == Size{20, 20} && at(result, {1}).rect == Rect{0, 0, 20, 20}
        && at(result, {1, 0}).rect == Rect{0, 0, 80, 60}, "Hard child sizes may overflow the parent; clipping belongs to the host");
}

void invalid_root() {
    auto root = leaf();
    root.spec.rows.clear();
    const auto result = compute_nested_grid(root, {});
    expect_error(result, NestedErrorCode::GridFailure, {0});
    require(result.error.grid_error.code == GridErrorCode::InvalidPlacement && result.measurement_count == 0, "Root validation must precede measurement");
}

void invalid_child() {
    auto child = leaf();
    child.spec.columns.clear();
    const auto root = parent(child);
    const auto result = compute_nested_grid(root, {});
    expect_error(result, NestedErrorCode::GridFailure, {1, 0});
    require(result.error.grid_error.code == GridErrorCode::InvalidPlacement && result.error.grid_error.item_id == 0,
            "Original child diagnostics must survive propagation");
}

void invalid_measurement() {
    Metrics host;
    host.invalid = true;
    const auto child = measured(host);
    const auto root = parent(child, fixed(), 0);
    const auto result = compute_nested_grid(root, {});
    expect_error(result, NestedErrorCode::GridFailure, {0, 0});
    require(result.error.grid_error.code == GridErrorCode::InvalidMeasurement && result.measurement_count == 2, "Invalid leaf metrics must not be hidden by probing");
}

void invalid_mapping() {
    auto root = leaf();
    root.children[0] = nullptr;
    expect_error(compute_nested_grid(root, {}), NestedErrorCode::InvalidChild, {0});
    root.children.clear();
    const auto child = leaf();
    root.children[7] = &child;
    expect_error(compute_nested_grid(root, {}), NestedErrorCode::InvalidChild, {7});
}

void duplicate_local_ids() {
    auto child = leaf();
    child.items.push_back(child.items.front());
    const auto root = parent(child);
    const auto result = compute_nested_grid(root, {});
    expect_error(result, NestedErrorCode::GridFailure, {1, 0});
    require(result.error.grid_error.code == GridErrorCode::DuplicateItemId, "Path scoping must not allow duplicate IDs inside one grid");
}

void self_cycle() {
    auto root = leaf();
    root.children[0] = &root;
    const auto result = compute_nested_grid(root, {});
    expect_error(result, NestedErrorCode::Cycle, {0});
    require(result.layout_count == 1 && result.measurement_count == 1, "Cycle must be caught before recurring into the same grid");
    root.children.clear();
    require(compute_nested_grid(root, {}).succeeded(), "A failed run must not contaminate the next run");
}

void indirect_cycle() {
    auto root = leaf();
    auto child = leaf();
    root.children[0] = &child;
    child.children[0] = &root;
    expect_error(compute_nested_grid(root, {}), NestedErrorCode::Cycle, {0, 0});
}

void depth_limits() {
    const auto child = leaf();
    const auto middle = parent(child);
    const auto root = parent(middle);
    const auto result = compute_nested_grid(root, {}, {.maximum_depth = 2});
    expect_error(result, NestedErrorCode::DepthLimit, {1, 1});
    require(result.layout_count == 2, "Depth failure must occur at the first excessive node");
    require(compute_nested_grid(root, {}, {.maximum_depth = 3}).succeeded(), "Exact allowed depth must succeed");
    expect_error(compute_nested_grid(root, {}, {.maximum_depth = 0}), NestedErrorCode::DepthLimit, {});
    require(compute_nested_grid(child, {}, {.maximum_depth = 1}).succeeded(), "Root counts as depth one");
}

void work_limits() {
    const auto child = leaf();
    const auto root = parent(child);
    const auto result = compute_nested_grid(root, {}, {.maximum_layouts = 1});
    expect_error(result, NestedErrorCode::WorkLimit, {1});
    require(result.layout_count == 1, "Layout budget counts attempted core calls, not recursion entries");
    expect_error(compute_nested_grid(root, {}, {.maximum_layouts = 0}), NestedErrorCode::WorkLimit, {});
    const auto full = compute_nested_grid(root, {200, 200});
    require(full.succeeded(), "Default budget must fit a small nested scene");
    require(compute_nested_grid(root, {200, 200}, {.maximum_layouts = full.layout_count}).succeeded(), "Exact layout budget must succeed");
    const auto final_failure = compute_nested_grid(root, {200, 200}, {.maximum_layouts = full.layout_count - 1});
    expect_error(final_failure, NestedErrorCode::WorkLimit, {1});
    require(final_failure.layout_count == full.layout_count - 1, "Budget failure must keep actual work counters");
}

void final_failure() {
    Metrics host;
    host.value = {{20, 10}, {80, 20}};
    host.fail_final_height = true;
    const auto child = measured(host);
    auto root = parent(child, fixed(100, 100));
    root.items[0].vertical_alignment = VerticalAlignment::End;
    const auto result = compute_nested_grid(root, {100, 100});
    expect_error(result, NestedErrorCode::GridFailure, {1, 0});
    require(result.error.grid_error.code == GridErrorCode::InvalidMeasurement && host.calls.back().available.height == 20,
            "An error during final child arrangement must discard previously accumulated parent rectangles");
}

void exception_recovery() {
    Metrics host;
    host.throw_error = true;
    const auto child = measured(host);
    const auto root = parent(child);
    bool caught = false;
    try { (void)compute_nested_grid(root, {}); }
    catch (Metrics* value) { caught = value == &host; }
    require(caught, "User exceptions must propagate unchanged through recursive probes");
    host.throw_error = false;
    require(compute_nested_grid(root, {}).succeeded(), "Exception unwinding must leave no retained recursion state");
}

void numeric_overflow() {
    const Unit largest = std::numeric_limits<Unit>::max();
    const auto child = leaf(fixed(largest, 1));
    auto root = parent(child, {.columns = {Track::fixed(largest / 2), Track::fixed(0)}, .rows = {Track::fixed(1)}});
    root.items[0].column = 1;
    expect_error(compute_nested_grid(root, {}), NestedErrorCode::NumericOverflow, {1});
    auto vertical_child = leaf(fixed(1, largest));
    auto vertical_root = parent(vertical_child, {.columns = {Track::fixed(1)}, .rows = {Track::fixed(largest / 2), Track::fixed(0)}});
    vertical_root.items[0].row = 1;
    expect_error(compute_nested_grid(vertical_root, {}), NestedErrorCode::NumericOverflow, {1});
}

void child_core_overflow() {
    const Unit largest = std::numeric_limits<Unit>::max();
    const auto child = leaf({.columns = {Track::fixed(largest), Track::fixed(largest)}, .rows = {Track::fixed(1)}});
    const auto root = parent(child);
    const auto result = compute_nested_grid(root, {});
    expect_error(result, NestedErrorCode::GridFailure, {1});
    require(result.error.grid_error.code == GridErrorCode::NumericOverflow && !result.error.grid_error.item_id,
            "Grid-level child errors must identify the containing grid path without inventing an item ID");
}

void fractional_offsets() {
    auto child = leaf(fixed(2.5f, 4.25f));
    child.spec.padding = {0.125f, 0.75f, 0.25f, 0.5f};
    auto middle = parent(child, fixed(10.5f, 15.25f));
    middle.spec.padding = {1.25f, 2.5f, 0, 0};
    auto root = parent(middle, fixed(40, 40));
    root.spec.padding = {0.5f, 0.25f, 0, 0};
    const auto result = compute_nested_grid(root, {100, 100});
    require(at(result, {1, 1, 0}).rect == Rect{1.875f, 3.5f, 2.5f, 4.25f}, "Nested translation must preserve subpixel coordinates without rounding");
}

void randomized_final_layout() {
    std::mt19937 random{20260930};
    std::uniform_int_distribution<int> values{0, 99};
    for (int sample = 0; sample < 250; ++sample) {
        const Unit width = static_cast<Unit>(100 + values(random) * 3);
        const Unit height = static_cast<Unit>(50 + values(random));
        const NestedGrid child{
            .spec = {.columns = {Track::weighted(1, 5), Track::weighted(2, 10, 80.0f)},
                     .rows = {Track::content()}, .column_gap = 3, .padding = {2, 3, 4, 5}},
            .items = {{.id = 0, .column_span = 2, .margin = {1, 2, 3, 4}, .preferred_size = {120, 30}},
                      {.id = 1, .column = 1, .horizontal_alignment = HorizontalAlignment::End, .preferred_size = {20, 15}}}
        };
        auto root = parent(child, fixed(width, height));
        root.spec.padding = {1, 2, 3, 4};
        root.items[0].margin = {static_cast<Unit>(values(random)), 3, static_cast<Unit>(values(random)), 7};
        root.items[0].horizontal_alignment = static_cast<HorizontalAlignment>(sample % 4);
        root.items[0].vertical_alignment = static_cast<VerticalAlignment>((sample / 4) % 4);
        const auto nested = compute_nested_grid(root, {width + 4, height + 6});
        const auto parent_rect = at(nested, {1}).rect;
        const auto direct = compute_grid(child.spec, child.items, {parent_rect.width, parent_rect.height});
        require(direct.succeeded(), "Independent child reference must succeed");
        for (const auto& item : direct.items) {
            const auto& actual = at(nested, {1, item.id});
            const Rect expected{parent_rect.x + item.rect.x, parent_rect.y + item.rect.y, item.rect.width, item.rect.height};
            const Rect expected_cell{parent_rect.x + item.cell_rect.x, parent_rect.y + item.cell_rect.y, item.cell_rect.width, item.cell_rect.height};
            require(same(actual.rect, expected) && same(actual.cell_rect, expected_cell), "Nested final layout must match a separate core call at the actual allocation");
        }
        const auto repeated = compute_nested_grid(root, {width + 4, height + 6});
        require(nested.layout_count == repeated.layout_count && nested.measurement_count == repeated.measurement_count
            && repeated.items.size() == nested.items.size(), "Repeated runs must have deterministic work and item counts");
        for (std::size_t index = 0; index < nested.items.size(); ++index) {
            require(repeated.items[index].path == nested.items[index].path && repeated.items[index].rect == nested.items[index].rect,
                    "Repeated nesting must not retain state or stale geometry");
        }
    }
}

struct TestCase { std::string_view name; void (*run)(); };

} // namespace

int main(int argc, char** argv) {
    const std::array cases{
        TestCase{"lambda_capture", lambda_capture}, TestCase{"lambda_move_only", lambda_move_only},
        TestCase{"lambda_const_function", lambda_const_function}, TestCase{"lambda_raw_compatibility", lambda_raw_compatibility},
        TestCase{"lambda_exceptions", lambda_exceptions}, TestCase{"lambda_reentrant", lambda_reentrant},
        TestCase{"flat_equivalence", flat_equivalence}, TestCase{"fixed_child", fixed_child},
        TestCase{"content_child", content_child}, TestCase{"width_reflow", width_reflow},
        TestCase{"aligned_child", aligned_child}, TestCase{"scoped_shared_ids", scoped_shared_ids},
        TestCase{"three_levels", three_levels}, TestCase{"spanning_child", spanning_child}, TestCase{"range_child", range_child},
        TestCase{"advisory_dependent_probes", advisory_dependent_probes},
        TestCase{"known_zero_width", known_zero_width}, TestCase{"empty_grids", empty_grids},
        TestCase{"overflowing_child", overflowing_child}, TestCase{"invalid_root", invalid_root},
        TestCase{"invalid_child", invalid_child}, TestCase{"invalid_measurement", invalid_measurement},
        TestCase{"invalid_mapping", invalid_mapping}, TestCase{"duplicate_local_ids", duplicate_local_ids},
        TestCase{"self_cycle", self_cycle}, TestCase{"indirect_cycle", indirect_cycle}, TestCase{"depth_limits", depth_limits},
        TestCase{"work_limits", work_limits}, TestCase{"final_failure", final_failure}, TestCase{"exception_recovery", exception_recovery},
        TestCase{"numeric_overflow", numeric_overflow}, TestCase{"child_core_overflow", child_core_overflow}, TestCase{"fractional_offsets", fractional_offsets},
        TestCase{"randomized_final_layout", randomized_final_layout}
    };
    if (argc != 2) {
        std::cerr << "Provide one nested test name\n";
        return 2;
    }
    for (const auto& test : cases) {
        if (test.name == argv[1]) {
            try {
                test.run();
                std::cout << "PASS " << test.name << '\n';
                return 0;
            } catch (const std::exception& error) {
                std::cerr << "FAIL " << test.name << ": " << error.what() << '\n';
                return 1;
            }
        }
    }
    std::cerr << "Unknown nested test: " << argv[1] << '\n';
    return 2;
}
