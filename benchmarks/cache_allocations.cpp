#include "allocation_probe.hpp"
#include <axisly/axisly.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void run(std::size_t count, bool measured) {
    axisly::GridSpec spec{.columns = {axisly::Track::weighted()}, .rows = {axisly::Track::weighted()}};
    std::vector<axisly::GridItem> items;
    items.reserve(count);
    for (std::size_t i = 0; i < count; ++i) items.push_back({.id = i});
    axisly::GridCache cache(spec, items);
    std::size_t calls{};
    if (measured) cache.set_measure(+[](axisly::ItemId, const axisly::MeasureInput&, void* context) -> axisly::MeasureResult {
        ++*static_cast<std::size_t*>(context);
        return {{}, {10, 20}};
    }, &calls);
    const auto warm = cache.compute({640, 480}, 7);
    require(warm.result.succeeded() && !warm.cache_hit, "Warm layout failed");
    const auto* storage = warm.result.items.data();
    const auto warm_calls = calls;
    axisly::bench::begin_allocations();
    bool correct = true;
    for (int i = 0; i < 1000; ++i) {
        const auto hit = cache.compute({640, 480}, 7);
        correct = correct && hit.cache_hit && hit.measurement_count == 0 &&
            hit.result.items.data() == storage && hit.result.items.size() == count;
    }
    const auto stats = axisly::bench::end_allocations();
    require(correct && calls == warm_calls, "Cache hit recomputed, copied output or measured content");
    require(stats.allocations == 0 && stats.requested_bytes == 0 && stats.peak_live_bytes == 0,
            "Cache hit must not allocate, even for a large grid");
    std::cout << "overlapping_items," << count << ',' << measured << ",same,1000," << stats.allocations << ','
              << stats.requested_bytes << ',' << stats.peak_live_bytes << ",0," << calls-warm_calls << '\n';
}

void equal_geometry(const axisly::GridResult& actual, const axisly::GridResult& expected) {
    require(actual.succeeded() && expected.succeeded() && actual.size == expected.size &&
            actual.items.size() == expected.items.size() && actual.measurement_count == expected.measurement_count,
            "Reused workspace changed result metadata");
    for (std::size_t i = 0; i < actual.items.size(); ++i) {
        require(actual.items[i].id == expected.items[i].id && actual.items[i].rect == expected.items[i].rect &&
                actual.items[i].cell_rect == expected.items[i].cell_rect, "Reused workspace changed geometry");
    }
}

void run_workspace(std::uint32_t side, bool spans) {
    using namespace axisly;
    GridSpec spec;
    for (std::uint32_t i = 0; i < side; ++i) {
        spec.columns.push_back(!spans ? Track::weighted() : i % 3 == 0 ? Track::content() :
            i % 3 == 1 ? Track::range(Track::content(), Track::fixed(200)) :
                         Track::range(Track::fixed(30), Track::weighted()));
        spec.rows.push_back(spans ? Track::content() : Track::weighted());
    }
    std::vector<GridItem> items;
    for (std::uint32_t row = 0; row < side; ++row) {
        for (std::uint32_t column = 0; column < side; ++column) {
            items.push_back({.id = items.size(), .column = column, .row = row,
                .column_span = spans ? std::min(2 + column % 3, side - column) : 1,
                .row_span = spans ? std::min(2 + row % 2, side - row) : 1});
        }
    }
    struct State { std::size_t calls{}; Unit width{}; } state;
    const auto callback = +[](ItemId id, const MeasureInput& input, void* context) -> MeasureResult {
        auto& s = *static_cast<State*>(context);
        ++s.calls;
        const auto width = s.width + static_cast<Unit>(id % 5);
        const auto height = input.known_width ? s.width / (1 + *input.known_width) : s.width;
        return {{}, {width, height}};
    };
    GridCache cache(spec, items);
    if (spans) cache.set_measure(callback, &state);
    const std::string_view name = spans ? "mixed_spans" : "weighted_grid";
    bench::begin_allocations();
    const auto warm = cache.compute({1280, 900});
    const auto initial = bench::end_allocations();
    require(warm.result.succeeded() && !warm.cache_hit, "Workspace warm-up failed");
    const auto* storage = warm.result.items.data();
    std::cout << name << ',' << items.size() << ',' << spans << ",warm,1," << initial.allocations << ','
              << initial.requested_bytes << ',' << initial.peak_live_bytes << ',' << initial.live_bytes << ','
              << warm.measurement_count << '\n';
    std::uint64_t version{};
    for (const std::string_view mode : {"resize", "content_version", "invalidate", "set_item"}) {
        constexpr int iterations = 64;
        std::size_t calls{};
        for (int i = 0; i < iterations; ++i) {
            const Size size = mode == "resize" ? Size{320 + static_cast<Unit>(i) * 20, 400 + static_cast<Unit>(i % 7) * 30}
                                                : Size{1280, 900};
            // Grow after a small warm-up, then shrink again. Never cache measured
            // values, and reserve candidates even when their first growth is zero.
            state.width = static_cast<Unit>((i % 4) * 90);
            if (mode == "content_version") ++version;
            if (mode == "invalidate") cache.invalidate();
            if (mode == "set_item") {
                items[0].margin.left = static_cast<Unit>(i % 2 + 1);
                cache.set_item(0, items[0]);
            }
            const auto before = state.calls;
            bench::begin_allocations();
            const auto computed = cache.compute(size, version);
            const auto stats = bench::end_allocations();
            require(!computed.cache_hit && computed.result.succeeded() && computed.result.items.data() == storage,
                    "Workspace miss must recompute into retained output storage");
            if (stats.allocations != 0 || stats.requested_bytes != 0 || stats.peak_live_bytes != 0) {
                std::cerr << name << ',' << items.size() << ',' << mode << ",iteration=" << i
                          << ",allocations=" << stats.allocations << ",bytes=" << stats.requested_bytes << '\n';
            }
            require(stats.allocations == 0 && stats.requested_bytes == 0 && stats.peak_live_bytes == 0,
                    "Warm workspace miss allocated for an unchanged-capacity layout");
            require(computed.measurement_count == (spans ? 2 * items.size() : 0) &&
                    state.calls - before == computed.measurement_count, "Workspace miss skipped measurements");
            calls += computed.measurement_count;
            // Independent stateless execution is outside allocation measurement.
            equal_geometry(computed.result, compute_grid(spec, items, size, spans ? callback : nullptr, &state));
        }
        std::cout << name << ',' << items.size() << ',' << spans << ',' << mode << ',' << iterations
                  << ",0,0,0,0," << calls << '\n';
    }
}
}
int main() {
    try {
        std::cout << "scenario,items,measured,mode,iterations,allocations,requested_bytes,peak_live_bytes,retained_bytes,measurement_calls\n";
        for (std::size_t count : {0u, 1u, 100u, 10000u}) {
            run(count, false);
            run(count, true);
        }
        for (std::uint32_t side : {20u, 100u}) {
            run_workspace(side, false);
            run_workspace(side, true);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
