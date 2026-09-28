#include <axisly/axisly.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
using namespace axisly;
constexpr Size viewport{240, 140};

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class Exception, class Function>
void expect_throw(Function function) {
    try { function(); }
    catch (const Exception&) { return; }
    throw std::runtime_error("Expected exception was not propagated");
}
GridSpec spec() {
    return {.columns = {Track::weighted(), Track::fixed(60)},
            .rows = {Track::weighted(), Track::fixed(30)},
            .column_gap = 4, .row_gap = 6, .padding = {1, 2, 3, 4}};
}
GridSpec content_spec() {
    return {.columns = {Track::content()}, .rows = {Track::content()}};
}
std::vector<GridItem> items() { return {{.id = 7, .preferred_size = {40, 20}}}; }

void equal_result(const GridResult& actual, const GridResult& expected) {
    require(actual.error.code == expected.error.code && actual.error.item_id == expected.error.item_id,
            "Cached and fresh error identities differ");
    require(actual.size == expected.size && actual.items.size() == expected.items.size(), "Result shapes differ");
    for (std::size_t i = 0; i < actual.items.size(); ++i) {
        require(actual.items[i].id == expected.items[i].id && actual.items[i].rect == expected.items[i].rect &&
                actual.items[i].cell_rect == expected.items[i].cell_rect, "Cached geometry differs from fresh layout");
    }
}
void miss_matches(GridCache& cache, Size size = viewport) {
    const auto actual = cache.compute(size);
    require(!actual.cache_hit, "Changed input must not hit the cache");
    equal_result(actual.result, compute_grid(cache.spec(), cache.items(), size));
}
struct Measurement {
    Size size{40, 20};
    std::size_t calls{};
    std::size_t throw_on{};
    bool invalid{};
};
MeasureResult measure(ItemId, const MeasureInput&, void* context) {
    auto& state = *static_cast<Measurement*>(context);
    ++state.calls;
    if (state.calls == state.throw_on) throw std::runtime_error("measurement failed");
    if (state.invalid) return {{-1, 0}, state.size};
    return {{}, state.size};
}

void reuse() {
    GridCache cache(spec(), items());
    const auto first = cache.compute(viewport);
    require(!first.cache_hit && first.result.succeeded() && first.measurement_count == 0, "First call must compute");
    const auto* storage = first.result.items.data();
    const auto expected = first.result;
    for (int i = 0; i < 1000; ++i) {
        const auto hit = cache.compute(viewport);
        require(hit.cache_hit && &hit.result == &first.result && hit.result.items.data() == storage,
                "Hit must borrow the existing result and item buffer");
        equal_result(hit.result, expected);
    }
}
void snapshots() {
    auto source_spec = spec();
    auto source_items = items();
    GridCache cache(source_spec, source_items);
    const auto expected = compute_grid(source_spec, source_items, viewport);
    source_spec.columns.clear();
    source_items.front().id = 99;
    equal_result(cache.compute(viewport).result, expected);
    require(cache.items().front().id == 7 && cache.spec().columns.size() == 2, "Cache must own its input snapshots");
    cache.set_items(source_items);
    source_items.clear();
    require(cache.items().size() == 1 && cache.items()[0].id == 99, "Updates must also copy caller storage");
    miss_matches(cache);
}
void unchanged_setters() {
    GridCache cache(spec(), items());
    (void)cache.compute(viewport);
    cache.set_spec(spec());
    cache.set_items(items());
    cache.set_item(0, items()[0]);
    cache.set_measure(nullptr);
    require(cache.compute(viewport).cache_hit, "Equal setters should preserve the cache");
    cache.set_spec(cache.spec());
    cache.set_items(cache.items());
    cache.set_item(0, cache.items()[0]);
    require(cache.compute(viewport).cache_hit, "Self updates should preserve the cache");
}
void spec_changes() {
    using Change = void (*)(GridSpec&);
    const std::array<Change, 9> changes{
        [](auto& s) { s.columns[0] = Track::fixed(20); },
        [](auto& s) { s.rows[0] = Track::content(); },
        [](auto& s) { s.column_gap += 3; }, [](auto& s) { s.row_gap += 3; },
        [](auto& s) { s.padding.left += 3; }, [](auto& s) { s.padding.top += 3; },
        [](auto& s) { s.padding.right += 3; }, [](auto& s) { s.padding.bottom += 3; },
        [](auto& s) { s.columns.push_back(Track::weighted()); }
    };
    for (auto change : changes) {
        GridCache cache(spec(), items());
        (void)cache.compute(viewport);
        auto next = spec();
        change(next);
        cache.set_spec(next);
        miss_matches(cache);
        require(cache.compute(viewport).cache_hit, "New specification should become cacheable");
    }
}
void item_changes() {
    using Change = void (*)(GridItem&);
    const std::array<Change, 13> changes{
        [](auto& i) { i.id = 0; }, [](auto& i) { i.column = 1; }, [](auto& i) { i.row = 1; },
        [](auto& i) { i.column_span = 2; }, [](auto& i) { i.row_span = 2; },
        [](auto& i) { i.margin.left = 3; }, [](auto& i) { i.margin.top = 3; },
        [](auto& i) { i.margin.right = 3; }, [](auto& i) { i.margin.bottom = 3; },
        [](auto& i) { i.horizontal_alignment = HorizontalAlignment::Center; },
        [](auto& i) { i.vertical_alignment = VerticalAlignment::End; },
        [](auto& i) { i.preferred_size.width = 77; }, [](auto& i) { i.preferred_size.height = 66; }
    };
    for (auto change : changes) {
        for (bool single : {false, true}) {
            GridCache cache(spec(), items());
            (void)cache.compute(viewport);
            auto next = items();
            change(next[0]);
            if (single) cache.set_item(0, next[0]); else cache.set_items(next);
            miss_matches(cache);
        }
    }
}
void item_sequence() {
    GridCache cache(spec(), items());
    (void)cache.compute(viewport);
    auto next = items();
    next.push_back({.id = std::numeric_limits<ItemId>::max(), .column = 1});
    cache.set_items(next);
    miss_matches(cache);
    std::ranges::reverse(next);
    cache.set_items(next);
    miss_matches(cache);
    // A subspan of the cache's own storage must survive replacing that storage.
    cache.set_items(cache.items().subspan(1));
    miss_matches(cache);
    require(cache.items()[0].id == 7, "Aliased subspan update lost its input");
    cache.set_items({});
    miss_matches(cache);
    require(cache.compute(viewport).result.items.empty(), "Removed items must not survive in output");
}
void size_changes() {
    GridCache cache(spec(), items());
    for (const auto size : std::array{viewport, Size{120, 140}, Size{120, 80}, Size{}, viewport}) {
        miss_matches(cache, size);
        require(cache.compute(size).cache_hit, "Same two-dimensional size should hit");
    }
}
void measurement_counts() {
    Measurement state;
    GridCache cache(content_spec(), items());
    cache.set_measure(measure, &state);
    auto first = cache.compute(viewport);
    require(first.measurement_count == 2 && first.result.measurement_count == 2 && state.calls == 2, "Fresh count mismatch");
    const auto hit = cache.compute(viewport);
    require(hit.cache_hit && hit.measurement_count == 0 && hit.result.measurement_count == 2 && state.calls == 2,
            "Hit must not measure or rewrite the stored result's original count");
}
void content_version() {
    Measurement state;
    GridCache cache(content_spec(), items());
    cache.set_measure(measure, &state);
    require(cache.compute(viewport, 5).result.size == state.size, "Initial content size mismatch");
    for (auto version : std::array<std::uint64_t, 4>{6, 0, std::numeric_limits<std::uint64_t>::max(), 5}) {
        state.size.width += 5;
        const auto call = cache.compute(viewport, version);
        require(!call.cache_hit && call.result.size == state.size && call.measurement_count == 2, "Content version must invalidate");
        require(cache.compute(viewport, version).cache_hit, "Repeated content version should hit");
    }
}
void invalidation() {
    Measurement state;
    GridCache cache(content_spec(), items());
    cache.set_measure(measure, &state);
    (void)cache.compute(viewport);
    state.size = {80, 30};
    cache.invalidate();
    cache.invalidate();
    const auto call = cache.compute(viewport);
    require(!call.cache_hit && call.result.size == state.size && state.calls == 4, "Explicit invalidation must remeasure");
}
void callback_changes() {
    Measurement a, b{{70, 30}};
    GridCache cache(content_spec(), items());
    (void)cache.compute(viewport);
    cache.set_measure(measure, &a);
    require(!cache.compute(viewport).cache_hit && a.calls == 2, "Adding a callback must invalidate");
    cache.set_measure(measure, &a);
    require(cache.compute(viewport).cache_hit, "Same callback/context should retain the cache");
    cache.set_measure(measure, &b);
    const auto changed_context = cache.compute(viewport);
    require(!changed_context.cache_hit && b.calls == 2 && changed_context.result.size == b.size, "Changed context must invalidate");
    cache.set_measure(+[](ItemId, const MeasureInput&, void*) -> MeasureResult { return {{}, {12, 9}}; }, &b);
    const auto changed_function = cache.compute(viewport);
    require(!changed_function.cache_hit && changed_function.result.size == Size{12, 9}, "Changed function must invalidate");
    cache.set_measure(nullptr, &b);
    miss_matches(cache);
    cache.set_measure(nullptr, &a);
    require(cache.compute(viewport).cache_hit, "Unused context must not affect an absent callback");
}
void advisory_dimensions() {
    GridCache cache(content_spec(), items());
    cache.set_measure(+[](ItemId, const MeasureInput& input, void*) -> MeasureResult {
        return {{}, {input.available.width / 2, input.available.height / 2}};
    });
    for (auto size : std::array{viewport, Size{120, 140}, Size{120, 80}, Size{120, 0}}) {
        const auto call = cache.compute(size);
        require(!call.cache_hit && call.result.size == Size{size.width / 2, size.height / 2},
                "Both advisory dimensions must invalidate even if the other dimension stays constant");
    }
}
void invalid_inputs() {
    GridCache cache(spec(), items());
    (void)cache.compute(viewport);
    const auto infinity = std::numeric_limits<Unit>::infinity();
    const auto nan = std::numeric_limits<Unit>::quiet_NaN();
    for (auto size : std::array{Size{-1, 1}, Size{1, -1}, Size{infinity, 1}, Size{1, nan}}) {
        for (int i = 0; i < 2; ++i) {
            const auto call = cache.compute(size);
            require(!call.cache_hit && call.result.error.code == GridErrorCode::InvalidContainerSize &&
                    call.result.items.empty() && call.result.size == Size{}, "Invalid size must never expose cached geometry");
        }
        miss_matches(cache);
    }
    auto next = spec();
    next.column_gap = -1;
    cache.set_spec(next);
    miss_matches(cache);
    miss_matches(cache);
    cache.set_spec(spec());
    cache.set_items(std::array{GridItem{.id = 1}, GridItem{.id = 1}});
    miss_matches(cache);
    require(cache.compute(viewport).result.error.code == GridErrorCode::DuplicateItemId, "Duplicate IDs must still be validated");
    cache.set_items(items());
    miss_matches(cache);
}
void invalid_measurement() {
    Measurement state;
    GridCache cache(content_spec(), items());
    cache.set_measure(measure, &state);
    (void)cache.compute(viewport);
    state.invalid = true;
    for (int i = 0; i < 2; ++i) {
        const auto bad = cache.compute(viewport, 1);
        require(!bad.cache_hit && bad.result.error.code == GridErrorCode::InvalidMeasurement &&
                bad.result.items.empty() && bad.measurement_count == 1, "Invalid measurement must not be cached");
    }
    state.invalid = false;
    const auto good = cache.compute(viewport, 1);
    require(!good.cache_hit && good.result.succeeded() && good.measurement_count == 2, "Failure must recover without a further version bump");
}
void exceptions() {
    for (std::size_t pass : {1u, 2u}) {
        Measurement state;
        GridCache cache(content_spec(), items());
        cache.set_measure(measure, &state);
        (void)cache.compute(viewport);
        state.throw_on = state.calls + pass;
        expect_throw<std::runtime_error>([&] { (void)cache.compute(viewport, 1); });
        state.throw_on = 0;
        const auto recovered = cache.compute(viewport);
        require(!recovered.cache_hit && recovered.result.succeeded(), "Exceptions must clear old validity and the active guard");
        require(cache.compute(viewport).cache_hit, "Recovered layout must be cacheable");
    }
}
void reentrant_mutation() {
    struct State { GridCache* cache; int action; };
    for (int action = 0; action < 6; ++action) {
        GridCache cache(spec(), items());
        State state{&cache, action};
        cache.set_measure(+[](ItemId, const MeasureInput&, void* context) -> MeasureResult {
            auto& s = *static_cast<State*>(context);
            switch (s.action) {
            case 0: (void)s.cache->compute(viewport); break;
            case 1: s.cache->set_spec(s.cache->spec()); break;
            case 2: s.cache->set_items(s.cache->items()); break;
            case 3: s.cache->set_item(0, s.cache->items()[0]); break;
            case 4: s.cache->set_measure(nullptr); break;
            case 5: s.cache->invalidate(); break;
            }
            return {};
        }, &state);
        expect_throw<std::logic_error>([&] { (void)cache.compute(viewport); });
        cache.set_measure(nullptr);
        miss_matches(cache);
    }
}
void independent_cache() {
    GridCache child(spec(), items());
    GridCache parent(content_spec(), items());
    parent.set_measure(+[](ItemId, const MeasureInput&, void* context) -> MeasureResult {
        auto& nested = *static_cast<GridCache*>(context);
        const auto computed = nested.compute(viewport);
        const auto fresh = compute_grid(nested.spec(), nested.items(), viewport);
        equal_result(computed.result, fresh);
        return {{}, computed.result.size};
    }, &child);
    const auto call = parent.compute(viewport);
    require(call.result.succeeded() && call.measurement_count == 2 && child.compute(viewport).cache_hit,
            "Callbacks must be able to use independent caches and stateless compute");
}
void empty_and_bounds() {
    GridCache empty;
    require(empty.compute({}).result.succeeded() && empty.compute({}).cache_hit, "Empty layouts should cache");
    GridCache cache(spec(), items());
    (void)cache.compute(viewport);
    expect_throw<std::out_of_range>([&] { cache.set_item(1, {}); });
    require(cache.compute(viewport).cache_hit && cache.items()[0].id == 7, "Rejected index must leave previous cache intact");
}
void randomized_updates() {
    std::mt19937 random(0xCAD12u);
    auto current_spec = spec();
    auto current_items = items();
    current_items.push_back({.id = 99, .column = 1, .row = 1});
    GridCache cache(current_spec, current_items);
    for (int step = 0; step < 400; ++step) {
        switch (random() % 5) {
        case 0: current_spec.column_gap = static_cast<Unit>(random() % 12); cache.set_spec(current_spec); break;
        case 1: current_spec.rows[0] = Track::weighted(static_cast<Unit>(1 + random() % 5)); cache.set_spec(current_spec); break;
        case 2: current_items[0].margin.left = static_cast<Unit>(random() % 30); cache.set_item(0, current_items[0]); break;
        case 3: std::ranges::reverse(current_items); cache.set_items(current_items); break;
        case 4: cache.invalidate(); break;
        }
        const Size size{static_cast<Unit>(random() % 400), static_cast<Unit>(random() % 300)};
        equal_result(cache.compute(size).result, compute_grid(current_spec, current_items, size));
        const auto hit = cache.compute(size);
        require(hit.cache_hit, "Repeated randomized layout must hit");
        equal_result(hit.result, compute_grid(current_spec, current_items, size));
    }
}
void span_growth_shrink() {
    GridSpec s{.columns = {Track::content(), Track::content(), Track::content()},
               .rows = {Track::content()}};
    const std::array entries{GridItem{.id = 7, .column_span = 3}};
    Measurement state{{90, 20}};
    GridCache cache(s, entries);
    cache.set_measure(measure, &state);
    for (auto width : {90.0f, 30.0f, 150.0f, 0.0f, 90.0f}) {
        state.size.width = width;
        cache.invalidate();
        const auto call = cache.compute(viewport);
        require(!call.cache_hit && call.measurement_count == 2 && call.result.size == Size{width, 20},
                "Prepared spans must grow and shrink from fresh contributions");
        require(call.result.items[0].cell_rect == Rect{0, 0, width, 20}, "Spanning rectangle retained old growth");
    }
}

void prepared_validation_priority() {
    Measurement state;
    GridCache cache(spec(), std::array{GridItem{.id = 7}, GridItem{.id = 8}});
    cache.set_measure(measure, &state);
    (void)cache.compute(viewport);
    const auto old_calls = state.calls;
    cache.set_item(1, {.id = 7, .column_span = 0});
    require(cache.compute({-1, 1}).result.error.code == GridErrorCode::InvalidContainerSize,
            "Container validation must precede invalid prepared inputs");
    const auto bad_span = cache.compute(viewport);
    require(bad_span.result.error.code == GridErrorCode::InvalidSpan && bad_span.result.error.item_id == 7,
            "Item errors must retain their priority before duplicate ID errors");
    cache.set_item(1, {.id = 7});
    require(cache.compute(viewport).result.error.code == GridErrorCode::DuplicateItemId && state.calls == old_calls,
            "Same-sized item update must revalidate IDs before measuring");
    cache.set_item(1, {.id = 8});
    require(cache.compute(viewport).result.succeeded() && state.calls == old_calls + 4,
            "Prepared input must recover after validation failure");
}

void prepared_axis_transitions() {
    auto s = spec();
    const std::array entries{GridItem{.id = 7, .column_span = 2, .row_span = 2,
        .horizontal_alignment = HorizontalAlignment::Center, .vertical_alignment = VerticalAlignment::End}};
    Measurement state{{90, 55}};
    GridCache cache(s, entries);
    cache.set_measure(measure, &state);
    const std::array tracks{Track::fixed(40), Track::content(), Track::weighted(),
        Track::range(Track::content(), Track::fixed(80)), Track::range(Track::fixed(10), Track::weighted(2)),
        Track::fixed(0)};
    for (const auto& column : tracks) {
        for (const auto& row : tracks) {
            s.columns.assign(2, column);
            s.rows.assign(2, row);
            cache.set_spec(s);
            const auto call = cache.compute(viewport);
            const auto expected = compute_grid(s, entries, viewport, measure, &state);
            equal_result(call.result, expected);
            require(!call.cache_hit && call.measurement_count == 2, "Fixed paths must retain both callback passes");
        }
    }
}

void prepared_numeric_recovery() {
    GridSpec s{.columns = {Track::fixed(10)}, .rows = {Track::content()}};
    const std::array entries{GridItem{.id = 7}, GridItem{.id = 8}};
    Measurement state;
    GridCache cache(s, entries);
    cache.set_measure(measure, &state);
    (void)cache.compute(viewport);
    s.rows = {Track::fixed(std::numeric_limits<Unit>::max()), Track::fixed(std::numeric_limits<Unit>::max())};
    cache.set_spec(s);
    for (auto size : {viewport, Size{0, 0}, viewport}) {
        const auto call = cache.compute(size);
        require(!call.cache_hit && call.result.error.code == GridErrorCode::NumericOverflow &&
                call.result.items.empty() && call.result.size == Size{} && call.measurement_count == 4,
                "Numeric errors must be rechecked with prepared inputs and expose no old output");
    }
    s.rows = {Track::content()};
    cache.set_spec(s);
    equal_result(cache.compute(viewport).result, compute_grid(s, entries, viewport, measure, &state));
}

void prepared_second_pass_failure() {
    struct State { int failure{}; Unit width{90}; } state;
    auto callback = +[](ItemId, const MeasureInput& input, void* context) -> MeasureResult {
        auto& s = *static_cast<State*>(context);
        if (input.known_width && s.failure == 1) throw std::runtime_error("second pass");
        if (input.known_width && s.failure == 2) return {{-1, 0}, {s.width, 20}};
        return {{}, {s.width, 20}};
    };
    GridSpec s{.columns = {Track::content(), Track::content()}, .rows = {Track::content()}};
    const std::array entries{GridItem{.id = 7, .column_span = 2}};
    GridCache cache(s, entries);
    cache.set_measure(callback, &state);
    (void)cache.compute(viewport);
    for (int failure : {1, 2}) {
        cache.invalidate();
        state.failure = failure;
        if (failure == 1) expect_throw<std::runtime_error>([&] { (void)cache.compute(viewport); });
        else {
            const auto call = cache.compute(viewport);
            require(call.result.error.code == GridErrorCode::InvalidMeasurement && call.measurement_count == 2 &&
                    call.result.items.empty(), "Invalid second pass must not publish geometry");
        }
        state.failure = 0;
        state.width /= 2;
        const auto recovery = cache.compute(viewport);
        require(!recovery.cache_hit && recovery.result.size == Size{state.width, 20},
                "Partially used workspace must recover with smaller content");
    }
}

void prepared_randomized_spans() {
    std::mt19937 random(0xA110C13u);
    GridSpec s{.columns = {Track::content(), Track::weighted(2), Track::range(Track::content(), Track::fixed(80)), Track::fixed(17)},
               .rows = {Track::content(), Track::weighted(), Track::content()}, .column_gap = 3, .row_gap = 5};
    std::vector<GridItem> entries;
    for (ItemId id = 0; id < 24; ++id) entries.push_back({.id = id,
        .column = static_cast<std::uint32_t>(id % 4), .row = static_cast<std::uint32_t>(id % 3)});
    Measurement state;
    GridCache cache(s, entries);
    cache.set_measure(measure, &state);
    for (std::uint64_t step = 0; step < 400; ++step) {
        const auto index = static_cast<std::size_t>(random() % entries.size());
        auto& item = entries[index];
        item.column_span = 1 + static_cast<std::uint32_t>(random() % (4 - item.column));
        item.row_span = 1 + static_cast<std::uint32_t>(random() % (3 - item.row));
        item.margin.left = static_cast<Unit>(random() % 11);
        if (step % 11 == 0) {
            std::shuffle(entries.begin(), entries.end(), random);
            cache.set_items(entries);
        } else cache.set_item(index, item);
        if (step % 13 == 0) {
            s.columns[0] = step % 2 ? Track::content() : Track::fixed(20);
            s.padding.bottom = static_cast<Unit>(step % 7);
            cache.set_spec(s);
        }
        for (int resize = 0; resize < 3; ++resize) {
            state.size = {static_cast<Unit>(random() % 250), static_cast<Unit>(random() % 70)};
            const Size size{static_cast<Unit>(random() % 500), static_cast<Unit>(random() % 300)};
            const auto call = cache.compute(size, step * 3 + static_cast<std::uint64_t>(resize));
            const auto fresh = compute_grid(s, entries, size, measure, &state);
            equal_result(call.result, fresh);
            require(call.measurement_count == 2 * entries.size(), "Prepared layout changed callback count");
        }
    }
}

struct TestCase { std::string_view name; void (*run)(); };
} // namespace

int main(int argc, char** argv) {
    const std::array cases{
        TestCase{"reuse", reuse}, TestCase{"snapshots", snapshots}, TestCase{"unchanged_setters", unchanged_setters},
        TestCase{"spec_changes", spec_changes}, TestCase{"item_changes", item_changes}, TestCase{"item_sequence", item_sequence},
        TestCase{"size_changes", size_changes}, TestCase{"measurement_counts", measurement_counts},
        TestCase{"content_version", content_version}, TestCase{"invalidation", invalidation},
        TestCase{"callback_changes", callback_changes}, TestCase{"advisory_dimensions", advisory_dimensions},
        TestCase{"invalid_inputs", invalid_inputs}, TestCase{"invalid_measurement", invalid_measurement},
        TestCase{"exceptions", exceptions}, TestCase{"reentrant_mutation", reentrant_mutation},
        TestCase{"independent_cache", independent_cache}, TestCase{"empty_and_bounds", empty_and_bounds},
        TestCase{"randomized_updates", randomized_updates}, TestCase{"span_growth_shrink", span_growth_shrink},
        TestCase{"prepared_validation_priority", prepared_validation_priority}, TestCase{"prepared_axis_transitions", prepared_axis_transitions},
        TestCase{"prepared_numeric_recovery", prepared_numeric_recovery}, TestCase{"prepared_second_pass_failure", prepared_second_pass_failure},
        TestCase{"prepared_randomized_spans", prepared_randomized_spans}
    };
    if (argc != 2) return 2;
    for (const auto& test : cases) {
        if (test.name != argv[1]) continue;
        try { test.run(); std::cout << "PASS " << test.name << '\n'; return 0; }
        catch (const std::exception& error) { std::cerr << "FAIL " << test.name << ": " << error.what() << '\n'; return 1; }
    }
    return 2;
}
