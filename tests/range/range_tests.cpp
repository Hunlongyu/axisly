#include <axisly/axisly.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <random>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

using namespace axisly;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool close(Unit actual, Unit expected) {
    return std::isfinite(actual) && std::abs(static_cast<double>(actual) - expected)
        <= std::max(0.00001, std::abs(static_cast<double>(expected)) * 0.000001);
}

struct Call { ItemId id{}; MeasureInput input; };

struct Host {
    std::map<ItemId, MeasureResult> sizes{{1, {{40, 10}, {200, 10}}}};
    std::vector<Call> calls;
    std::optional<MeasureResult> constrained;
    bool wrap{};

    static MeasureResult measure(ItemId id, const MeasureInput& input, void* context) {
        auto& host = *static_cast<Host*>(context);
        host.calls.push_back({id, input});
        require(!input.known_height, "Range measurement must keep row height unknown until rows are resolved");
        const auto found = host.sizes.find(id);
        auto result = found == host.sizes.end() ? MeasureResult{} : found->second;
        if (input.known_width) {
            require(input.available.width == *input.known_width, "Range remeasurement must use final item width");
            if (host.constrained && id == 1) return *host.constrained;
            if (host.wrap && *input.known_width > 0) {
                const Unit lines = std::max(1.0f, std::ceil(result.preferred.width / *input.known_width));
                result.minimum.height *= lines;
                result.preferred.height *= lines;
            }
        }
        return result;
    }
};

Track bounded(Unit minimum, Unit maximum) {
    return Track::range(Track::fixed(minimum), Track::fixed(maximum));
}

GridSpec one(const Track& column) {
    return {.columns = {column}, .rows = {Track::fixed(20)}};
}

GridResult layout(const GridSpec& spec, const std::vector<GridItem>& items, Size available, Host& host) {
    return compute_grid(spec, items, available, Host::measure, &host);
}

GridResult layout(const Track& column, Unit available, MeasureResult metrics = {{40, 10}, {200, 10}}) {
    Host host;
    host.sizes[1] = metrics;
    return layout(one(column), {{.id = 1}}, {available, 100}, host);
}

void expect_width(const GridResult& result, Unit width) {
    require(result.succeeded() && result.items.size() == 1, "Expected a successful one-range layout");
    require(close(result.size.width, width) && close(result.items[0].cell_rect.width, width), "Unexpected ranged track width");
    require(result.measurement_count == 2, "Range sizing must retain two measurement calls per item");
}

std::vector<GridItem> probes(const GridSpec& spec, std::vector<GridItem> items = {}) {
    for (std::size_t index = 0; index < spec.columns.size(); ++index) {
        items.push_back({.id = 1000 + index, .column = static_cast<std::uint32_t>(index)});
    }
    return items;
}

void expect_columns(const GridResult& result, std::initializer_list<Unit> widths) {
    require(result.succeeded() && result.items.size() >= widths.size(), "Expected successful range column probes");
    std::size_t index = result.items.size() - widths.size();
    for (const auto width : widths) {
        require(close(result.items[index++].cell_rect.width, width), "Unexpected ranged column allocation");
    }
}

void expect_error(const GridResult& result, GridErrorCode code, std::optional<ItemId> id, std::size_t calls) {
    require(!result.succeeded() && result.error.code == code && result.error.item_id == id, "Unexpected range error");
    require(result.size == Size{} && result.items.empty() && !result.error.message.empty(), "Range errors must not expose partial geometry");
    require(result.measurement_count == calls, "Range errors must retain actual measurement count");
}

void fixed_fixed() {
    const auto track = bounded(80, 160);
    expect_width(layout(track, 500), 160);
    expect_width(layout(track, 100), 100);
    expect_width(layout(track, 0), 80);
    expect_width(layout(track, 500, {{10, 10}, {120, 10}}), 120);
}

void fixed_content() {
    const auto track = Track::range(Track::fixed(80), Track::content());
    expect_width(layout(track, 500), 200);
    expect_width(layout(track, 100), 100);
    expect_width(layout(track, 0), 80);
    expect_width(layout(track, 500, {{10, 10}, {20, 10}}), 80);
}

void fixed_weighted() {
    const GridSpec spec{.columns = {Track::range(Track::fixed(80), Track::weighted()), Track::weighted(2)},
                        .rows = {Track::fixed(20)}};
    const auto items = probes(spec, {{.id = 1}});
    Host host;
    expect_columns(layout(spec, items, {300, 100}, host), {100, 200});
    expect_columns(layout(spec, items, {90, 100}, host), {80, 10});
    expect_columns(layout(spec, items, {}, host), {80, 0});
}

void content_fixed() {
    const auto track = Track::range(Track::content(), Track::fixed(160));
    expect_width(layout(track, 500), 160);
    expect_width(layout(track, 100), 100);
    expect_width(layout(track, 0), 40);
}

void content_content() {
    const auto track = Track::range(Track::content(), Track::content());
    expect_width(layout(track, 500), 200);
    expect_width(layout(track, 100), 100);
    expect_width(layout(track, 0), 40);
}

void content_weighted() {
    const GridSpec spec{.columns = {Track::range(Track::content(), Track::weighted()), Track::weighted(2)},
                        .rows = {Track::fixed(20)}};
    Host host;
    host.sizes[1] = {{120, 10}, {200, 10}};
    const auto items = probes(spec, {{.id = 1}});
    expect_columns(layout(spec, items, {300, 100}, host), {120, 180});
    expect_columns(layout(spec, items, {}, host), {120, 0});
    expect_columns(layout(spec, items, {600, 100}, host), {200, 400});
}

void empty_ranges() {
    const GridSpec spec{
        .columns = {bounded(80, 160), Track::range(Track::fixed(30), Track::content()),
                    Track::range(Track::content(), Track::fixed(90)), Track::range(Track::content(), Track::content())},
        .rows = {Track::fixed(20)}, .column_gap = 5, .padding = {1, 2, 3, 4}
    };
    Host host;
    const auto result = layout(spec, {}, {1000, 100}, host);
    require(result.succeeded() && result.size == Size{129, 26} && result.items.empty(), "Empty non-weighted ranges must remain at their lower bounds");
    require(result.measurement_count == 0 && host.calls.empty(), "Empty ranges need no host measurements");
}

void equal_bounds() {
    for (const Unit available : {0.0f, 40.0f, 500.0f}) {
        expect_width(layout(bounded(100, 100), available), 100);
        expect_width(layout(bounded(0, 0), available), 0);
    }
    const GridSpec spec{.columns = {bounded(100, 100), Track::weighted()}, .rows = {Track::fixed(20)}};
    Host host;
    host.sizes[1] = {{300, 10}, {500, 10}};
    expect_columns(layout(spec, probes(spec, {{.id = 1, .column_span = 2}}), {}, host), {100, 200});
}

void fixed_floor_priority() {
    const MeasureResult metrics{{200, 10}, {240, 10}};
    expect_width(layout(bounded(80, 160), 100, metrics), 100);
    expect_width(layout(Track::range(Track::fixed(80), Track::content()), 100, metrics), 100);
    expect_width(layout(Track::range(Track::fixed(80), Track::weighted()), 100, metrics), 100);
    expect_width(layout(Track::range(Track::fixed(80), Track::content()), 500, {{10, 10}, {20, 10}}), 80);
}

void fixed_cap_priority() {
    const auto track = Track::range(Track::content(), Track::fixed(160));
    for (const Unit available : {0.0f, 100.0f, 500.0f}) {
        expect_width(layout(track, available, {{200, 10}, {240, 10}}), 160);
    }
}

void margins_alignment() {
    const GridSpec spec{.columns = {Track::range(Track::content(), Track::fixed(120))}, .rows = {bounded(60, 100)}};
    Host host;
    host.sizes[1] = {{40, 10}, {80, 20}};
    const GridItem item{.id = 1, .margin = {10, 5, 20, 15},
                        .horizontal_alignment = HorizontalAlignment::Center, .vertical_alignment = VerticalAlignment::End};
    const auto result = layout(spec, {item}, {300, 300}, host);
    require(result.succeeded() && result.size == Size{110, 60} && result.items[0].cell_rect == Rect{0, 0, 110, 60},
            "Ranged intrinsic bounds must include margins exactly once");
    require(result.items[0].rect == Rect{10, 25, 80, 20} && host.calls[1].input.known_width == 80,
            "Range alignment must feed actual item width to the host");
}

void padding_gaps() {
    const GridSpec spec{.columns = {bounded(80, 100), Track::range(Track::fixed(50), Track::weighted())},
                        .rows = {bounded(20, 30)}, .column_gap = 10, .padding = {2, 3, 8, 7}};
    const auto result = compute_grid(spec, std::array{GridItem{.preferred_size = {200, 40}}, GridItem{.id = 2, .column = 1}}, {300, 100});
    require(result.succeeded() && result.size == Size{300, 40}, "Range sizing must reserve gaps and padding");
    require(result.items[0].cell_rect == Rect{2, 3, 100, 30} && result.items[1].cell_rect == Rect{112, 3, 180, 30},
            "Range offsets must include surrounding fixed geometry");
}

void mixed_growth() {
    const GridSpec spec{.columns = {bounded(20, 60), Track::range(Track::content(), Track::content()),
                                    Track::range(Track::fixed(30), Track::weighted(2)), Track::weighted()},
                        .rows = {Track::fixed(20)}};
    Host host;
    host.sizes = {{1, {{10, 10}, {100, 10}}}, {2, {{20, 10}, {80, 10}}}};
    const auto items = probes(spec, {{.id = 1}, {.id = 2, .column = 1}});
    expect_columns(layout(spec, items, {320, 100}, host), {60, 80, 120, 60});
    expect_columns(layout(spec, items, {70, 100}, host), {20, 20, 30, 0});
}

void multiple_items() {
    const auto track = Track::range(Track::content(), Track::fixed(160));
    Host host;
    host.sizes = {{1, {{100, 10}, {120, 10}}}, {2, {{40, 10}, {200, 10}}}};
    const auto result = layout(one(track), {{.id = 1}, {.id = 2}}, {}, host);
    require(result.succeeded() && result.size.width == 100 && result.measurement_count == 4,
            "Ranged intrinsic minima must take a maximum over items");
    const auto large = layout(one(track), {{.id = 2}, {.id = 1}}, {500, 100}, host);
    require(large.succeeded() && large.size.width == 160, "Preferred and minimum may come from different items, subject to the same cap");
}

void content_shrink() {
    const GridSpec spec{.columns = {bounded(20, 100), bounded(40, 200)}, .rows = {Track::fixed(20)}};
    const std::array items{GridItem{.preferred_size = {200, 10}}, GridItem{.id = 2, .column = 1, .preferred_size = {400, 10}}};
    const auto result = compute_grid(spec, items, {180, 100});
    require(result.succeeded() && result.size.width == 180 && result.items[0].cell_rect.width == 60
            && result.items[1].cell_rect.width == 120, "Bounded content must share shrinkage by available intrinsic capacity");
}

void span_capped_content() {
    const GridSpec spec{.columns = {bounded(50, 100), Track::content()}, .rows = {Track::fixed(20)}};
    Host host;
    host.sizes[1] = {{160, 10}, {300, 10}};
    const auto items = probes(spec, {{.id = 1, .column_span = 2}});
    expect_columns(layout(spec, items, {}, host), {100, 60});
    expect_columns(layout(spec, items, {500, 100}, host), {100, 200});
}

void span_cascade() {
    const GridSpec spec{.columns = {bounded(0, 10), bounded(0, 30), bounded(0, 100)}, .rows = {Track::fixed(20)}};
    const auto items = probes(spec, {{.column_span = 3, .preferred_size = {90, 10}}});
    expect_columns(compute_grid(spec, items, {300, 100}), {10, 30, 50});
    expect_columns(compute_grid(spec, items, {45, 100}), {5, 15, 25});
}

void span_weighted_fallback() {
    const GridSpec spec{.columns = {bounded(0, 100), Track::weighted(1, 0, 30.0f),
                                    Track::range(Track::fixed(0), Track::weighted(3))}, .rows = {Track::fixed(20)}};
    Host host;
    host.sizes[1] = {{300, 10}, {500, 10}};
    expect_columns(layout(spec, probes(spec, {{.id = 1, .column_span = 3}}), {}, host), {100, 30, 170});
}

void span_all_capped() {
    const GridSpec spec{.columns = {bounded(0, 30), Track::range(Track::content(), Track::fixed(70))}, .rows = {Track::fixed(20)}};
    Host host;
    host.sizes[1] = {{200, 10}, {300, 10}};
    const auto result = layout(spec, probes(spec, {{.id = 1, .column_span = 2}}), {500, 100}, host);
    expect_columns(result, {30, 70});
    require(result.size.width == 100 && result.items[0].rect.width == 100, "Unsatisfiable span minima must not break explicit range caps");
}

void span_dynamic_minimum() {
    const GridSpec spec{.columns = {Track::range(Track::content(), Track::weighted()), Track::range(Track::content(), Track::weighted(3))},
                        .rows = {Track::fixed(20)}};
    Host host;
    host.sizes = {{1, {{60, 10}, {100, 10}}}, {2, {{100, 10}, {200, 10}}}};
    const auto items = probes(spec, {{.id = 1}, {.id = 2, .column_span = 2}});
    expect_columns(layout(spec, items, {}, host), {70, 30});
}

void both_axes() {
    const GridSpec spec{.columns = {Track::range(Track::fixed(40), Track::content())},
                        .rows = {Track::range(Track::content(), Track::fixed(60))}};
    Host host;
    host.sizes[1] = {{70, 70}, {200, 80}};
    const auto result = layout(spec, {{.id = 1}}, {100, 30}, host);
    require(result.succeeded() && result.size == Size{100, 60} && result.items[0].rect == Rect{0, 0, 100, 60},
            "Range limits must apply independently on both axes");
}

void row_remeasurement() {
    const GridSpec spec{.columns = {Track::range(Track::fixed(0), Track::fixed(150))},
                        .rows = {Track::range(Track::content(), Track::weighted())}};
    Host host;
    host.sizes[1] = {{40, 20}, {400, 20}};
    host.wrap = true;
    const auto result = layout(spec, {{.id = 1}}, {500, 0}, host);
    require(result.succeeded() && result.size == Size{150, 60} && result.items[0].rect == Rect{0, 0, 150, 60},
            "A Content lower bound on a row must use constrained height, not intrinsic height");
    require(host.calls.size() == 2 && host.calls[1].input.known_width == 150 && result.measurement_count == 2,
            "Range row sizing must keep the two-pass measurement contract");
}

void known_zero_width() {
    Host host;
    const auto result = layout(one(Track::range(Track::content(), Track::fixed(0))), {{.id = 1}}, {500, 100}, host);
    expect_width(result, 0);
    require(!host.calls[0].input.known_width && host.calls[1].input.known_width.has_value()
            && *host.calls[1].input.known_width == 0, "A zero range cap must reach the host as known zero width");
}

void callback_validation() {
    Host host;
    host.sizes[1].preferred.width = std::numeric_limits<Unit>::infinity();
    expect_error(layout(one(bounded(0, 0)), {{.id = 1}}, {100, 100}, host), GridErrorCode::InvalidMeasurement, 1, 1);
    host.sizes[1] = {{40, 10}, {200, 10}};
    host.constrained = MeasureResult{{}, {-1, 0}};
    expect_error(layout(one(bounded(0, 0)), {{.id = 1}}, {100, 100}, host), GridErrorCode::InvalidMeasurement, 1, 2);
}

void numeric_overflow() {
    constexpr Unit maximum = std::numeric_limits<Unit>::max();
    Host host;
    host.sizes[1] = {{maximum, 10}, {maximum, 10}};
    const GridItem item{.id = 1, .margin = {maximum, 0, 0, 0}};
    expect_error(layout(one(Track::range(Track::content(), Track::weighted())), {item}, {maximum, 100}, host),
                 GridErrorCode::NumericOverflow, {}, 1);
    expect_error(layout(one(Track::range(Track::content(), Track::content())), {item}, {maximum, 100}, host),
                 GridErrorCode::NumericOverflow, {}, 1);
    const GridSpec spec{.columns = {bounded(maximum, maximum), Track::range(Track::fixed(maximum), Track::weighted())}};
    expect_error(compute_grid(spec, {}, {maximum, 100}), GridErrorCode::NumericOverflow, {}, 0);
}

void cap_prevents_overflow() {
    constexpr Unit maximum = std::numeric_limits<Unit>::max();
    Host host;
    host.sizes[1] = {{maximum, 10}, {maximum, 10}};
    const GridItem item{.id = 1, .margin = {maximum, 0, 0, 0}};
    const auto result = layout(one(Track::range(Track::content(), Track::fixed(maximum))), {item}, {maximum, 100}, host);
    expect_width(result, maximum);
    require(result.items[0].rect.width == 0, "A finite explicit cap may clip oversized intrinsic sums before final geometry");
    expect_width(layout(bounded(0, maximum), maximum, {{0, 10}, {maximum, 10}}), maximum);
}

void fractional_limits() {
    expect_width(layout(bounded(0.25f, 1.25f), 0.75f, {{0.5f, 10}, {2.5f, 10}}), 0.75f);
    constexpr Unit tiny = std::numeric_limits<Unit>::denorm_min();
    for (const auto& track : {bounded(tiny, tiny), Track::range(Track::content(), Track::fixed(tiny)),
                              Track::range(Track::fixed(tiny), Track::weighted())}) {
        const auto result = layout(track, 0, {{tiny, 10}, {tiny, 10}});
        require(result.succeeded() && result.items[0].rect.width == tiny && result.size.width == tiny,
                "Subnormal ranged bounds must survive allocation");
    }
}

void extreme_weights() {
    constexpr Unit huge = std::numeric_limits<Unit>::max();
    constexpr Unit tiny = std::numeric_limits<Unit>::denorm_min();
    const GridSpec spec{.columns = {Track::weighted(huge, 0, 1.0f), Track::range(Track::fixed(0), Track::weighted(tiny))},
                        .rows = {Track::fixed(20)}};
    Host host;
    expect_columns(layout(spec, probes(spec), {100, 100}, host), {1, 99});
    host.sizes[1] = {{100, 10}, {100, 10}};
    expect_columns(layout(spec, probes(spec, {{.id = 1, .column_span = 2}}), {}, host), {1, 99});
}

void require_same_by_id(const GridResult& actual, const GridResult& expected) {
    require(actual.succeeded() && expected.succeeded() && actual.size == expected.size && actual.items.size() == expected.items.size(),
            "Equivalent range layouts must preserve occupancy and result shape");
    for (const auto& item : actual.items) {
        const auto found = std::find_if(expected.items.begin(), expected.items.end(), [&](const ItemLayout& other) { return other.id == item.id; });
        require(found != expected.items.end() && found->cell_rect == item.cell_rect && found->rect == item.rect,
                "Equivalent layouts must preserve every item's geometry");
    }
}

void deterministic_resize() {
    const GridSpec spec{.columns = {Track::range(Track::content(), Track::fixed(150)), Track::range(Track::fixed(40), Track::weighted())},
                        .rows = {Track::range(Track::content(), Track::content())}, .column_gap = 10};
    Host host;
    host.wrap = true;
    host.sizes = {{1, {{40, 20}, {400, 20}}}, {2, {{20, 10}, {60, 10}}}};
    const std::vector<GridItem> items{{.id = 1}, {.id = 2, .column = 1}};
    const auto original_columns = spec.columns;
    const auto original_rows = spec.rows;
    const auto expected = layout(spec, items, {400, 200}, host);
    require(layout(spec, items, {100, 200}, host).succeeded(), "Small ranged resize must succeed");
    const auto repeated = layout(spec, items, {400, 200}, host);
    require_same_by_id(repeated, expected);
    require(spec.columns == original_columns && spec.rows == original_rows && repeated.measurement_count == 4,
            "Ranged resize must not mutate bounds or retain measurement state");
}

void compatibility_reference() {
    std::mt19937 random{20260928};
    std::uniform_int_distribution<int> values{1, 80};
    for (int sample = 0; sample < 500; ++sample) {
        GridSpec ranged{.rows = {Track::range(Track::content(), Track::content())}, .column_gap = 3, .padding = {2, 1, 4, 3}};
        GridSpec ordinary{.rows = {Track::content()}, .column_gap = 3, .padding = {2, 1, 4, 3}};
        const auto count = static_cast<std::uint32_t>(2 + values(random) % 5);
        for (std::uint32_t index = 0; index < count; ++index) {
            const Unit floor = static_cast<Unit>(values(random));
            const Unit weight = static_cast<Unit>(values(random));
            switch (values(random) % 3) {
            case 0:
                ranged.columns.push_back(bounded(floor, floor));
                ordinary.columns.push_back(Track::fixed(floor));
                break;
            case 1:
                ranged.columns.push_back(Track::range(Track::fixed(floor), Track::weighted(weight)));
                ordinary.columns.push_back(Track::weighted(weight, floor));
                break;
            default:
                ranged.columns.push_back(Track::range(Track::content(), Track::content()));
                ordinary.columns.push_back(Track::content());
                break;
            }
        }
        std::vector<GridItem> items;
        Host range_host;
        range_host.sizes.clear();
        range_host.wrap = true;
        for (std::uint32_t index = 0; index < count + 2; ++index) {
            const auto column = static_cast<std::uint32_t>(values(random)) % count;
            const auto span = 1 + static_cast<std::uint32_t>(values(random)) % (count - column);
            items.push_back({.id = index, .column = column, .column_span = span, .margin = {2, 1, 3, 2}});
            const Unit minimum = static_cast<Unit>(values(random));
            range_host.sizes[index] = {{minimum, 10}, {minimum + static_cast<Unit>(values(random)), 20}};
        }
        Host ordinary_host;
        ordinary_host.sizes = range_host.sizes;
        ordinary_host.wrap = true;
        const Size available{static_cast<Unit>(values(random) * 10), 300};
        require_same_by_id(layout(ranged, items, available, range_host), layout(ordinary, items, available, ordinary_host));
    }
}

void randomized_bounds() {
    std::mt19937 random{20260929};
    std::uniform_int_distribution<int> values{0, 100};
    for (int sample = 0; sample < 500; ++sample) {
        const GridSpec spec{
            .columns = {bounded(20, 80), Track::range(Track::fixed(20), Track::content()),
                        Track::range(Track::fixed(20), Track::weighted(2)), Track::range(Track::content(), Track::fixed(80)),
                        Track::range(Track::content(), Track::content()), Track::range(Track::content(), Track::weighted(3))},
            .rows = {bounded(20, 100)}, .column_gap = 3, .padding = {2, 0, 4, 0}
        };
        Host host;
        host.sizes.clear();
        std::vector<GridItem> items;
        for (std::uint32_t index = 0; index < 9; ++index) {
            const auto column = index < 6 ? index : static_cast<std::uint32_t>(values(random)) % 5;
            const auto span = index < 6 ? 1u : 2 + static_cast<std::uint32_t>(values(random)) % (5 - column);
            const Unit minimum = static_cast<Unit>(values(random));
            host.sizes[index] = {{minimum, 10}, {minimum + static_cast<Unit>(values(random)), 10}};
            items.push_back({.id = index, .column = column, .column_span = span, .margin = {3, 0, 4, 0}});
        }
        items = probes(spec, items);
        const auto small = layout(spec, items, {}, host);
        const Size available{static_cast<Unit>(100 + values(random) * 8), 100};
        const auto large = layout(spec, items, available, host);
        require(small.succeeded() && large.succeeded() && large.measurement_count == 2 * items.size(),
                "Random bounded layouts must terminate with the normal callback count");
        for (std::size_t index = 0; index < 6; ++index) {
            const Unit small_width = small.items[9 + index].cell_rect.width;
            const Unit large_width = large.items[9 + index].cell_rect.width;
            const Unit minimum = index < 3 ? 20.0f
                : index == 3 ? std::min(80.0f, host.sizes.at(index).minimum.width + 7)
                : host.sizes.at(index).minimum.width + 7;
            require(small_width + 0.0001f >= minimum && large_width + 0.0001f >= minimum, "Each resolved lower bound must survive allocation");
            if (index == 0 || index == 3) require(small_width <= 80 && large_width <= 80, "Explicit upper bounds must survive all span contributions");
            require(large_width + 0.0001f >= small_width, "Increasing available width must not shrink a range with unchanged intrinsic metrics");
        }
        for (std::size_t index = 0; index < items.size(); ++index) {
            const auto& item = large.items[index];
            require(item.rect.width == *host.calls[3 * items.size() + index].input.known_width,
                    "Range geometry and constrained measurements must use identical final widths");
            require(item.rect.x >= item.cell_rect.x && item.rect.x + item.rect.width <= item.cell_rect.x + item.cell_rect.width + 0.001f,
                    "Ranged span alignment must stay within its merged cell");
        }
        std::shuffle(items.begin(), items.end(), random);
        require_same_by_id(layout(spec, items, available, host), large);
    }
}

struct TestCase { std::string_view name; void (*run)(); };

} // namespace

int main(int argc, char** argv) {
    const std::array cases{
        TestCase{"fixed_fixed", fixed_fixed}, TestCase{"fixed_content", fixed_content}, TestCase{"fixed_weighted", fixed_weighted},
        TestCase{"content_fixed", content_fixed}, TestCase{"content_content", content_content}, TestCase{"content_weighted", content_weighted},
        TestCase{"empty_ranges", empty_ranges}, TestCase{"equal_bounds", equal_bounds}, TestCase{"fixed_floor_priority", fixed_floor_priority},
        TestCase{"fixed_cap_priority", fixed_cap_priority}, TestCase{"margins_alignment", margins_alignment}, TestCase{"padding_gaps", padding_gaps},
        TestCase{"mixed_growth", mixed_growth}, TestCase{"multiple_items", multiple_items}, TestCase{"content_shrink", content_shrink},
        TestCase{"span_capped_content", span_capped_content}, TestCase{"span_cascade", span_cascade},
        TestCase{"span_weighted_fallback", span_weighted_fallback}, TestCase{"span_all_capped", span_all_capped},
        TestCase{"span_dynamic_minimum", span_dynamic_minimum}, TestCase{"both_axes", both_axes}, TestCase{"row_remeasurement", row_remeasurement},
        TestCase{"known_zero_width", known_zero_width}, TestCase{"callback_validation", callback_validation},
        TestCase{"numeric_overflow", numeric_overflow}, TestCase{"cap_prevents_overflow", cap_prevents_overflow},
        TestCase{"fractional_limits", fractional_limits}, TestCase{"extreme_weights", extreme_weights},
        TestCase{"deterministic_resize", deterministic_resize}, TestCase{"compatibility_reference", compatibility_reference},
        TestCase{"randomized_bounds", randomized_bounds}
    };
    if (argc != 2) {
        std::cerr << "Provide one range test name\n";
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
    std::cerr << "Unknown range test: " << argv[1] << '\n';
    return 2;
}
