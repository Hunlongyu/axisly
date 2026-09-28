#include <gridly/gridly.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string_view>

namespace {
using namespace gridly;
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void near(Unit a, Unit b) { check(std::abs(a - b) < 0.002f, "Unexpected geometry"); }
template<class E, class F> void throws(F f) {
    try { f(); } catch (const E&) { return; }
    throw std::runtime_error("Missing exception");
}
GridSpec automatic(AutoFlow flow = AutoFlow::Row) {
    GridSpec s{.columns = {Track::fixed(10), Track::fixed(10), Track::fixed(10)}, .rows = {Track::fixed(20)}};
    s.auto_flow = flow; s.allow_implicit_tracks = true;
    s.implicit_row = Track::fixed(20); s.implicit_column = Track::fixed(10);
    return s;
}
GridItem automatic_item(ItemId id, std::uint32_t span = 1) {
    return {.id = id, .column = auto_position, .row = auto_position, .column_span = span};
}
void placement_row() {
    auto s = automatic();
    std::vector<GridItem> items{automatic_item(1, 2), automatic_item(2, 2), automatic_item(3)};
    auto r = compute_grid(s, items, {100, 100});
    check(r.succeeded(), "Row flow failed");
    check(r.items[0].column == 0 && r.items[0].row == 0 && r.items[1].row == 1
          && r.items[2].column == 2 && r.items[2].row == 1, "Sparse cursor moved backwards");
    s.auto_flow = AutoFlow::RowDense;
    r = compute_grid(s, items, {100, 100});
    check(r.succeeded() && r.items[2].column == 2 && r.items[2].row == 0, "Dense flow did not fill hole");
    check(items[0].column == auto_position && s.rows.size() == 1, "Inputs were modified");
}
void placement_column() {
    auto s = automatic(AutoFlow::Column);
    s.columns = {Track::fixed(10)}; s.rows.assign(3, Track::fixed(20));
    std::vector<GridItem> items(3);
    for (std::size_t i = 0; i < items.size(); ++i) { items[i] = automatic_item(i); items[i].row_span = i == 2 ? 1u : 2u; }
    auto r = compute_grid(s, items, {100, 100});
    check(r.succeeded() && r.items[2].column == 1 && r.items[2].row == 2, "Column sparse failed");
    s.auto_flow = AutoFlow::ColumnDense;
    r = compute_grid(s, items, {100, 100});
    check(r.succeeded() && r.items[2].column == 0 && r.items[2].row == 2, "Column dense failed");
}
void placement_locked() {
    auto s = automatic();
    const std::vector<GridItem> items{
        automatic_item(1), {.id = 2, .column = auto_position, .row = 0, .column_span = 2},
        {.id = 3, .column = 0, .row = 0}, {.id = 4, .column = 1, .row = auto_position, .row_span = 2}};
    const auto r = compute_grid(s, items, {100, 100});
    check(r.succeeded(), "Partially locked placement failed");
    check(r.items[1].column == 1 && r.items[1].row == 0, "Locked row did not reserve before automatic");
    check(r.items[0].row == 1 && r.items[0].column == 0 && r.items[3].row == 1, "Locked column placement failed");
}
void implicit_tracks() {
    auto s = automatic(); s.auto_flow = AutoFlow::Explicit;
    const std::array items{GridItem{.id = 7, .column = 4, .row = 3, .column_span = 2}};
    const auto r = compute_grid(s, items, {200, 200});
    check(r.succeeded(), "Explicit implicit growth failed"); near(r.size.width, 60); near(r.size.height, 80);
    near(r.items[0].rect.x, 40); near(r.items[0].rect.width, 20);
    auto empty = automatic(); empty.columns.clear(); empty.rows.clear();
    const std::array autos{automatic_item(0, 2)};
    const auto grown = compute_grid(empty, autos, {});
    check(grown.succeeded(), "Empty implicit grid failed"); near(grown.size.width, 20); near(grown.size.height, 20);
}
void placement_limits() {
    auto s = automatic(); s.allow_implicit_tracks = false;
    const std::array items{automatic_item(1, 3), automatic_item(2)};
    auto r = compute_grid(s, items, {100, 100});
    check(!r.succeeded() && r.items.empty(), "Full explicit grid should fail atomically");
    s.allow_implicit_tracks = true; s.max_implicit_tracks = 0;
    r = compute_grid(s, items, {100, 100});
    check(r.error.code == GridErrorCode::TrackLimit, "Missing track growth guard");
    s.max_implicit_tracks = 4096; s.max_placement_work = 1;
    r = compute_grid(s, items, {100, 100});
    check(r.error.code == GridErrorCode::PlacementLimit, "Missing placement work guard");
    s = automatic(); s.auto_flow = static_cast<AutoFlow>(255);
    check(!compute_grid(s, items, {}).succeeded(), "Invalid flow accepted");
    s = automatic(); auto bad = items; bad[0].row_span = auto_position;
    check(compute_grid(s, bad, {}).error.code == GridErrorCode::TrackLimit, "Huge span should fail without allocation");
}
void placement_randomized() {
    std::mt19937 rng(74013);
    for (const auto flow : {AutoFlow::Row, AutoFlow::RowDense, AutoFlow::Column, AutoFlow::ColumnDense}) {
        for (int run = 0; run < 60; ++run) {
            auto s = automatic(flow); s.rows.assign(4, Track::fixed(20));
            s.columns.assign(4, Track::fixed(10));
            std::vector<GridItem> items;
            for (ItemId id = 0; id < 35; ++id) {
                auto i = automatic_item(id, 1 + rng() % 3); i.row_span = 1 + rng() % 3; items.push_back(i);
            }
            const auto r = compute_grid(s, items, {100, 100});
            check(r.succeeded(), "Random auto layout failed");
            // Independent bitmap oracle scans every cell, without interval jumps.
            bool occupied[160][160]{};
            unsigned cursor_major = 0, cursor_minor = 0;
            const bool column = flow == AutoFlow::Column || flow == AutoFlow::ColumnDense;
            const bool dense = flow == AutoFlow::ColumnDense || flow == AutoFlow::RowDense;
            for (std::size_t i = 0; i < items.size(); ++i) {
                const unsigned major_span = column ? items[i].column_span : items[i].row_span;
                const unsigned minor_span = column ? items[i].row_span : items[i].column_span;
                unsigned major = dense ? 0 : cursor_major, minor = dense ? 0 : cursor_minor;
                for (;;) {
                    if (minor + minor_span > 4) { ++major; minor = 0; }
                    bool free = true;
                    for (unsigned a = major; a < major + major_span; ++a)
                        for (unsigned b = minor; b < minor + minor_span; ++b) free = free && !occupied[a][b];
                    if (free) break;
                    ++minor;
                }
                check(r.items[i].column == (column ? major : minor) && r.items[i].row == (column ? minor : major), "Placement differs from bitmap oracle");
                for (unsigned a = major; a < major + major_span; ++a)
                    for (unsigned b = minor; b < minor + minor_span; ++b) occupied[a][b] = true;
                cursor_major = major; cursor_minor = minor + minor_span;
            }
        }
    }
}
void percentage() {
    GridSpec s{.columns = {Track::percentage(0.25f), Track::weighted()}, .rows = {Track::percentage(0.5f)},
               .column_gap = 10, .padding = {10, 5, 10, 5}};
    const std::array items{GridItem{}, GridItem{.id = 1, .column = 1}};
    GridCache cache(s, items);
    for (const Unit width : {220.f, 120.f, 20.f, 0.f}) {
        const auto& r = cache.compute({width, 110}).result;
        check(r.succeeded(), "Percentage resize failed");
        near(r.items[0].rect.width, std::max(0.f, width - 20) * 0.25f);
        near(r.items[0].rect.height, 50);
        const auto fresh = compute_grid(s, items, {width, 110});
        check(r.items[0].rect == fresh.items[0].rect && r.items[1].rect == fresh.items[1].rect, "Percentage basis was cached");
    }
}
void percentage_ranges() {
    GridSpec s{.columns = {Track::range(Track::percentage(0.5f), Track::fixed(40)),
                          Track::range(Track::content(), Track::percentage(0.25f))}, .rows = {Track::fixed(10)}};
    const std::array items{GridItem{.preferred_size = {300, 10}}, GridItem{.id = 1, .column = 1, .preferred_size = {300, 10}}};
    const auto r = compute_grid(s, items, {200, 100});
    check(r.succeeded(), "Mixed range failed"); near(r.items[0].rect.width, 100); near(r.items[1].rect.width, 50);
    throws<std::invalid_argument>([] { (void)Track::percentage(-1); });
    throws<std::invalid_argument>([] { (void)Track::percentage(std::numeric_limits<float>::infinity()); });
    throws<std::invalid_argument>([] { (void)Track::range(Track::percentage(1), Track::percentage(.5f)); });
    s.columns = {Track::percentage(std::numeric_limits<Unit>::max())};
    check(compute_grid(s, std::span{items}.first(1), {10, 10}).error.code == GridErrorCode::NumericOverflow, "Percentage overflow accepted");
}
void baseline_static() {
    GridSpec s{.columns = {Track::fixed(50), Track::fixed(50)}, .rows = {Track::content()}};
    const std::array items{
        GridItem{.id = 1, .vertical_alignment = VerticalAlignment::Baseline, .preferred_size = {20, 20}, .baseline = 15.f},
        GridItem{.id = 2, .column = 1, .margin = {0, 2, 0, 3}, .vertical_alignment = VerticalAlignment::Baseline,
                 .preferred_size = {20, 30}, .baseline = 10.f}};
    const auto r = compute_grid(s, items, {100, 100});
    check(r.succeeded(), "Baseline layout failed"); near(r.size.height, 38);
    near(r.items[0].rect.y + 15, r.items[1].rect.y + 10);
    near(r.items[1].rect.y, 5);
}
void baseline_reflow() {
    GridSpec s{.columns = {Track::weighted(), Track::weighted()}, .rows = {Track::content()}};
    const std::array items{GridItem{.id = 1, .vertical_alignment = VerticalAlignment::Baseline},
        GridItem{.id = 2, .column = 1, .vertical_alignment = VerticalAlignment::Baseline}};
    const auto r = compute_grid(s, items, {100, 100}, [](ItemId id, const MeasureInput& input) {
        return MeasureResult{{}, {30, 30}, input.known_width ? (id == 1 ? 20.f : 10.f) : 2.f};
    });
    check(r.succeeded() && r.measurement_count == 4, "Baseline callback contract changed");
    near(r.items[0].rect.y + 20, r.items[1].rect.y + 10); near(r.size.height, 40);
    const auto invalid = compute_grid(s, items, {100, 100}, [](ItemId, const MeasureInput&) {
        return MeasureResult{{}, {10, 10}, 11.f};
    });
    check(invalid.error.code == GridErrorCode::InvalidMeasurement && invalid.items.empty(), "Invalid baseline accepted");
}
void baseline_spans_and_clipping() {
    auto s = automatic(); s.rows = {Track::fixed(10), Track::fixed(10)};
    const std::array items{
        GridItem{.id = 1, .row_span = 2, .vertical_alignment = VerticalAlignment::Baseline, .preferred_size = {5, 30}, .baseline = 25.f},
        GridItem{.id = 2, .column = 1, .vertical_alignment = VerticalAlignment::Baseline, .preferred_size = {5, 4}}};
    const auto r = compute_grid(s, items, {100, 100});
    check(r.succeeded(), "Clipped baseline failed");
    for (const auto& i : r.items) check(i.rect.y >= i.cell_rect.y && i.rect.y + i.rect.height <= i.cell_rect.y + i.cell_rect.height, "Baseline escaped hard cell");
}
void rounding() {
    GridSpec s{.columns = {Track::weighted(), Track::weighted(), Track::weighted()}, .rows = {Track::weighted()}};
    const std::array items{GridItem{}, GridItem{.id = 1, .column = 1}, GridItem{.id = 2, .column = 2}};
    const auto raw = compute_grid(s, items, {100, 15.3f});
    for (const Unit scale : {1.f, 1.25f, 2.f}) {
        const auto r = round_layout(raw, {scale, .3f, -.2f});
        check(r.succeeded(), "Rounding failed");
        for (std::size_t i = 1; i < items.size(); ++i) near(r.items[i - 1].rect.x + r.items[i - 1].rect.width, r.items[i].rect.x);
        near(r.items.back().rect.x + r.items.back().rect.width, r.size.width);
        near(r.size.height * scale, std::round((15.3f - .2f) * scale) - std::round(-.2f * scale));
    }
    check(round_layout(raw, {0}).error.code == GridErrorCode::InvalidRounding, "Invalid scale accepted");
    check(round_layout(raw, {1, std::numeric_limits<float>::infinity(), 0}).items.empty(), "Invalid origin produced geometry");
    check(raw.items[0].rect.width != 33, "Rounding mutated source");
}
void rounding_shared_edges() {
    for (std::uint32_t count = 2; count <= 30; ++count) {
        GridSpec spec{.columns = std::vector<Track>(count, Track::weighted()), .rows = {Track::fixed(10)}};
        std::vector<GridItem> items;
        for (std::uint32_t i = 0; i < count; ++i) items.push_back({.id = i, .column = i});
        for (int width = 1; width < 240; ++width) {
            const auto raw = compute_grid(spec, items, {static_cast<Unit>(width), 10});
            const auto rounded = round_layout(raw);
            check(rounded.succeeded(), "Shared-edge rounding failed");
            for (std::size_t i = 1; i < rounded.items.size(); ++i) {
                const auto& a = rounded.items[i - 1].cell_rect; const auto& b = rounded.items[i].cell_rect;
                if (a.x + a.width != b.x) {
                    std::cerr << "count=" << count << " width=" << width << " item=" << i << '\n';
                    throw std::runtime_error("Rounding introduced a gap or overlap at a shared edge");
                }
            }
        }
    }
}
void baseline_extreme_caps() {
    const Unit huge = std::numeric_limits<Unit>::max();
    GridSpec spec{.columns = {Track::fixed(10), Track::fixed(10)},
        .rows = {Track::range(Track::content(), Track::fixed(30))}};
    const std::array items{GridItem{.vertical_alignment = VerticalAlignment::Baseline, .preferred_size = {10, huge}, .baseline = huge},
        GridItem{.id = 1, .column = 1, .vertical_alignment = VerticalAlignment::Baseline, .preferred_size = {10, huge}, .baseline = 0.f}};
    const auto result = compute_grid(spec, items, {20, 40});
    check(result.succeeded(), "A hard row cap should bound enormous baseline contributions");
    near(result.size.height, 30);
    spec.rows[0] = Track::content();
    check(compute_grid(spec, items, {20, 40}).succeeded(), "Large preferred height should shrink to representable minimum");
    const auto overflow = compute_grid(spec, items, {20, 40}, [huge](ItemId id, const MeasureInput&) {
        return MeasureResult{{10, huge}, {10, huge}, id == 0 ? huge : 0.f};
    });
    check(overflow.error.code == GridErrorCode::NumericOverflow, "Unbounded baseline minimum overflow accepted");
}
struct Context { std::size_t calls{}; Unit height{10}; bool fail{}, invalid{}; };
MeasureResult measure(ItemId, const MeasureInput& input, void* data) {
    auto& c = *static_cast<Context*>(data); ++c.calls;
    if (c.fail) throw std::runtime_error("Measurement exception");
    return {{}, {input.known_width.value_or(input.available.width), c.invalid ? -1.f : c.height}};
}
void measurement_keys() {
    MeasurementCache cache; Context c; cache.set_measure(measure, &c);
    const std::array<MeasureInput, 6> inputs{{{{}, {}, {20, 30}}, {0.f, {}, {20, 30}}, {{}, 0.f, {20, 30}},
        {{}, {}, {21, 30}}, {{}, {}, {20, 31}}, {10.f, 12.f, {20, 30}}}};
    for (const auto& i : inputs) (void)cache.measure(7, i);
    check(c.calls == inputs.size(), "Constraint keys alias");
    for (const auto& i : inputs) (void)cache.measure(7, i);
    check(c.calls == inputs.size() && cache.hit_count() == inputs.size(), "Identical key missed");
    (void)cache.measure(8, inputs[0]); check(c.calls == inputs.size() + 1, "Different IDs alias");
    cache.invalidate(7); c.height = 20; near(cache.measure(7, inputs[0]).preferred.height, 20);
    check(cache.entry_count() == 2, "Invalidation retained entries");
}
void measurement_lru() {
    MeasurementCache cache(2); Context c; cache.set_measure(measure, &c);
    const MeasureInput a{{}, {}, {10, 10}}, b{{}, {}, {20, 10}}, d{{}, {}, {30, 10}};
    (void)cache.measure(1, a); (void)cache.measure(1, b); (void)cache.measure(1, a); (void)cache.measure(1, d);
    check(cache.entry_count() == 2 && cache.hit_count() == 1, "LRU is unbounded");
    (void)cache.measure(1, b); check(c.calls == 4, "LRU evicted the wrong entry");
    throws<std::invalid_argument>([] { MeasurementCache invalid(0); });
}
void measurement_failures() {
    MeasurementCache cache; Context c; cache.set_measure(measure, &c);
    c.invalid = true; (void)cache.measure(1, {}); (void)cache.measure(1, {});
    check(cache.entry_count() == 0 && c.calls == 2, "Invalid result cached");
    c.invalid = false; c.fail = true;
    throws<std::runtime_error>([&] { (void)cache.measure(1, {}); });
    c.fail = false; check(cache.measure(1, {}).preferred.height == 10, "Exception poisoned cache");
    cache.set_measure(+[](ItemId, const MeasureInput&, void* context) -> MeasureResult {
        static_cast<MeasurementCache*>(context)->clear(); return {};
    }, &cache);
    throws<std::logic_error>([&] { (void)cache.measure(1, {}); });
    cache.set_measure(measure, &c); (void)cache.measure(1, {});
}
void grid_measurement_cache() {
    GridSpec s{.columns = {Track::fixed(50), Track::fixed(50)}, .rows = {Track::content()}};
    const std::array items{GridItem{.id = 1}, GridItem{.id = 2, .column = 1}};
    GridCache cache(s, items); Context c; cache.set_measure(measure, &c); cache.enable_measurement_cache();
    check(cache.compute({100, 100}).measurement_count == 4, "First measurements missing");
    cache.invalidate_item(1); c.height = 20;
    const auto update = cache.compute({100, 100});
    check(!update.cache_hit && update.measurement_count == 2, "Unrelated item was measured again");
    near(update.result.size.height, 20);
    check(cache.compute({100, 101}).measurement_count == 4, "Available height absent from key");
    check(cache.compute({100, 101}, 1).measurement_count == 4, "Content version did not invalidate all");
    check(cache.compute({100, 101}, 1).cache_hit, "Layout hit regressed");
    cache.invalidate(); check(cache.compute({100, 101}, 1).measurement_count == 4, "Invalidate reused stale content");
    cache.enable_measurement_cache(false); check(cache.compute({100, 101}, 1).measurement_count == 4, "Disable failed");
}
void cache_exception_identity() {
    struct HostError { int code; };
    GridCache cache({.columns = {Track::content()}, .rows = {Track::content()}}, std::array{GridItem{}});
    cache.enable_measurement_cache();
    cache.set_measure(+[](ItemId, const MeasureInput&, void*) -> MeasureResult { throw HostError{42}; });
    bool caught = false;
    try { (void)cache.compute({100, 100}); } catch (const HostError& error) { caught = error.code == 42; }
    check(caught, "Callback exception type or payload changed");
    Context context; cache.set_measure(measure, &context);
    check(cache.compute({100, 100}).measurement_count == 2 && context.calls == 2, "Partial measurements survived exception");
    context.fail = true; cache.invalidate_item(0);
    throws<std::runtime_error>([&] { (void)cache.compute({100, 100}); });
    context.fail = false;
    check(cache.compute({100, 100}).measurement_count == 2, "Callback guard remained active after failure");
}
void advanced_cache_equivalence() {
    auto s = automatic(AutoFlow::RowDense); s.columns = {Track::percentage(.3f), Track::weighted(), Track::content()};
    s.implicit_row = Track::content();
    std::vector<GridItem> items;
    for (ItemId id = 0; id < 40; ++id) {
        auto i = automatic_item(id, id % 5 == 0 ? 2u : 1u); i.preferred_size = {10, 12};
        i.vertical_alignment = VerticalAlignment::Baseline; i.baseline = static_cast<Unit>(id % 10); items.push_back(i);
    }
    GridCache cache(s, items);
    for (const Unit width : {300.f, 80.f, 0.f, 220.f}) {
        const auto& r = cache.compute({width, 100}).result;
        const auto f = compute_grid(s, items, {width, 100});
        check(r.succeeded() && f.succeeded() && r.size == f.size, "Advanced cache result differs");
        for (std::size_t i = 0; i < items.size(); ++i) check(r.items[i].rect == f.items[i].rect, "Advanced cached rectangles differ");
    }
    items[0].column_span = 3; cache.set_items(items);
    const auto f = compute_grid(s, items, {220, 100});
    check(cache.compute({220, 100}).result.items[1].row == f.items[1].row, "Placement topology was stale");
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("Expected case name");
        const std::pair<std::string_view, void(*)()> cases[]{
            {"placement_row", placement_row}, {"placement_column", placement_column}, {"placement_locked", placement_locked},
            {"implicit_tracks", implicit_tracks}, {"placement_limits", placement_limits}, {"placement_randomized", placement_randomized},
            {"percentage", percentage}, {"percentage_ranges", percentage_ranges}, {"baseline_static", baseline_static},
            {"baseline_reflow", baseline_reflow}, {"baseline_spans_and_clipping", baseline_spans_and_clipping}, {"rounding", rounding},
            {"rounding_shared_edges", rounding_shared_edges}, {"baseline_extreme_caps", baseline_extreme_caps},
            {"measurement_keys", measurement_keys}, {"measurement_lru", measurement_lru}, {"measurement_failures", measurement_failures},
            {"grid_measurement_cache", grid_measurement_cache}, {"cache_exception_identity", cache_exception_identity},
            {"advanced_cache_equivalence", advanced_cache_equivalence}};
        for (const auto& [name, run] : cases) if (name == argv[1]) { run(); return 0; }
        throw std::runtime_error("Unknown case");
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
