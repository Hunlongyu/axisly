#include <axisly/axisly.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
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
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool close(Unit actual, Unit expected) {
    return std::isfinite(actual) && std::abs(static_cast<double>(actual) - expected)
        <= std::max(0.00001, std::abs(static_cast<double>(expected)) * 0.000001);
}

struct Call {
    ItemId id{};
    MeasureInput input;
};

struct Host {
    std::map<ItemId, MeasureResult> natural{{0, {{40, 20}, {400, 20}}}};
    std::vector<Call> calls;
    bool wrap{};
    std::optional<MeasureResult> constrained;
    std::size_t invalid_call{};
    MeasureResult invalid_result{};

    static MeasureResult measure(ItemId id, const MeasureInput& input, void* context) {
        auto& host = *static_cast<Host*>(context);
        host.calls.push_back({id, input});
        require(!input.known_height, "M4 must not claim a known height before rows are resolved");
        if (host.invalid_call == host.calls.size()) {
            return host.invalid_result;
        }
        auto result = host.natural.at(id);
        if (input.known_width) {
            require(input.available.width == *input.known_width, "Second-pass available width must equal known width");
            if (host.constrained) {
                return *host.constrained;
            }
            if (host.wrap && *input.known_width > 0) {
                const Unit lines = std::max(1.0f, std::ceil(result.preferred.width / *input.known_width));
                result.minimum.height *= lines;
                result.preferred.height *= lines;
            }
        }
        return result;
    }
};

GridSpec content_grid() {
    return {.columns = {Track::content()}, .rows = {Track::content()}};
}

GridResult compute(Host& host, const GridSpec& spec = content_grid(), Size available = {500, 300},
                   const std::vector<GridItem>& items = {GridItem{}}) {
    return compute_grid(spec, items, available, Host::measure, &host);
}

void expect_box(const GridResult& result, Rect cell, Rect rect, Size size) {
    require(result.succeeded() && result.items.size() == 1, "Expected a successful one-item layout");
    require(result.items.front().cell_rect == cell, "Unexpected measured cell rectangle");
    require(result.items.front().rect == rect, "Unexpected measured item rectangle");
    require(result.size == size, "Unexpected measured grid extent");
    require(result.error.message.empty() && !result.error.item_id, "Successful layout must not carry an error");
}

void expect_error(const GridResult& result, GridErrorCode code, std::optional<ItemId> id, std::size_t calls) {
    require(!result.succeeded() && result.error.code == code && result.error.item_id == id,
            "Unexpected measurement error or item ID");
    require(result.size == Size{} && result.items.empty() && !result.error.message.empty(),
            "Measurement failure must not expose partial geometry");
    require(result.measurement_count == calls, "Failure must retain the actual callback count");
}

void empty_content() {
    Host host;
    auto spec = content_grid();
    spec.columns.push_back(Track::content());
    spec.rows.push_back(Track::content());
    spec.column_gap = 5;
    spec.row_gap = 7;
    spec.padding = {1, 2, 3, 4};
    const auto result = compute(host, spec, {100, 100}, {});
    require(result.succeeded() && result.items.empty() && result.size == Size{9, 13},
            "Empty content tracks must be zero-sized but retain gaps and padding");
    require(result.measurement_count == 0 && host.calls.empty(), "An empty grid must not measure");
}

void static_fallback() {
    const GridItem item{.margin = {3, 4, 5, 6}, .preferred_size = {80, 20}};
    const auto result = compute_grid(content_grid(), std::array{item}, {200, 100});
    expect_box(result, {0, 0, 88, 30}, {3, 4, 80, 20}, {88, 30});
    require(result.measurement_count == 0, "Static fallback must not count measurements");
    const auto small = compute_grid(content_grid(), std::array{item}, {});
    expect_box(small, {0, 0, 8, 10}, {3, 4, 0, 0}, {8, 10});
}

void null_callback() {
    const auto spec = content_grid();
    const std::array items{GridItem{.preferred_size = {70, 30}}};
    int unused_context = 7;
    const auto result = compute_grid(spec, items, {50, 50}, nullptr, &unused_context);
    expect_box(result, {0, 0, 50, 30}, {0, 0, 50, 30}, {50, 30});
    require(result.measurement_count == 0 && unused_context == 7, "A null callback must ignore context");
}

void intrinsic_size() {
    Host host;
    const auto result = compute(host);
    expect_box(result, {0, 0, 400, 20}, {0, 0, 400, 20}, {400, 20});
    require(result.measurement_count == 2 && host.calls.size() == 2, "A measured item must be called twice");
    require(!host.calls[0].input.known_width && host.calls[1].input.known_width == 400,
            "Intrinsic measurement must precede final-width measurement");
}

void largest_contribution() {
    Host host;
    host.natural = {{0, {{80, 5}, {100, 10}}}, {1, {{20, 30}, {180, 40}}}};
    const std::vector<GridItem> items{{}, {.id = 1}};
    const auto result = compute(host, content_grid(), {300, 100}, items);
    require(result.succeeded() && result.size == Size{180, 40}, "Shared tracks use maxima, not sums of item sizes");
    require(result.items[0].cell_rect == result.items[1].cell_rect && result.measurement_count == 4,
            "Overlapping items retain the same cell and are measured independently");
    const auto small = compute(host, content_grid(), {}, items);
    require(small.succeeded() && small.size == Size{80, 30}, "Minimum and preferred may come from different items");
}

void content_shrink() {
    Host host;
    host.natural = {{0, {{20, 10}, {100, 10}}}, {1, {{40, 10}, {200, 10}}}};
    const GridSpec spec{.columns = {Track::content(), Track::content()}, .rows = {Track::content()}};
    const auto result = compute(host, spec, {180, 100}, {{}, {.id = 1, .column = 1}});
    require(result.succeeded() && result.size == Size{180, 10}, "Content tracks should consume their shrinking budget");
    require(result.items[0].cell_rect == Rect{0, 0, 60, 10}
            && result.items[1].cell_rect == Rect{60, 0, 120, 10}, "Content shrink must respect each interval's capacity");
}

void minimum_overflow() {
    Host host;
    host.natural[0] = {{60, 40}, {200, 80}};
    const auto result = compute(host, content_grid(), {30, 20});
    expect_box(result, {0, 0, 60, 40}, {0, 0, 60, 40}, {60, 40});
    expect_box(compute(host, content_grid(), {}), {0, 0, 60, 40}, {0, 0, 60, 40}, {60, 40});
}

void rigid_content() {
    Host host;
    host.natural = {{0, {{50, 10}, {50, 10}}}, {1, {{0, 10}, {100, 10}}}};
    const GridSpec spec{.columns = {Track::content(), Track::content()}, .rows = {Track::content()}};
    const auto result = compute(host, spec, {70, 30}, {{}, {.id = 1, .column = 1}});
    require(result.succeeded() && result.items[0].rect.width == 50 && result.items[1].rect.width == 20,
            "A content track with equal bounds cannot donate space");
}

void mixed_tracks() {
    Host host;
    host.natural = {{0, {{0, 0}, {1000, 1000}}}, {1, {{30, 5}, {80, 20}}}, {2, {{0, 0}, {1000, 1000}}}};
    const GridSpec spec{.columns = {Track::fixed(50), Track::content(), Track::weighted()},
                        .rows = {Track::fixed(30)}, .column_gap = 5, .padding = {2, 3, 8, 7}};
    const auto result = compute(host, spec, {300, 100}, {{}, {.id = 1, .column = 1}, {.id = 2, .column = 2}});
    require(result.succeeded() && result.size == Size{300, 40}, "Mixed tracks must include padding and gaps once");
    require(result.items[0].cell_rect == Rect{2, 3, 50, 30}
            && result.items[1].cell_rect == Rect{57, 3, 80, 30}
            && result.items[2].cell_rect == Rect{142, 3, 150, 30}, "Content is resolved before weighted free space");
}

void weighted_minimum() {
    Host host;
    host.natural = {{0, {{20, 10}, {80, 10}}}, {1, {{0, 0}, {0, 0}}}};
    const GridSpec spec{.columns = {Track::content(), Track::weighted(1, 50)}, .rows = {Track::content()}};
    const std::vector<GridItem> items{{}, {.id = 1, .column = 1}};
    const auto result = compute(host, spec, {100, 100}, items);
    require(result.succeeded() && result.items[0].rect.width == 50 && result.items[1].rect.width == 50,
            "Content shrink must reserve numeric weighted minima");
    const auto overflow = compute(host, spec, {10, 100}, items);
    require(overflow.succeeded() && overflow.size.width == 70 && overflow.items[0].rect.width == 20
            && overflow.items[1].rect.width == 50, "Both kinds of minima must survive insufficient space");
}

void weighted_cap() {
    Host host;
    host.natural = {{0, {{20, 10}, {80, 10}}}, {1, {{0, 0}, {0, 0}}}};
    const GridSpec spec{.columns = {Track::content(), Track::weighted(1, 10, 50.0f)}, .rows = {Track::content()}};
    const auto result = compute(host, spec, {300, 100}, {{}, {.id = 1, .column = 1}});
    require(result.succeeded() && result.size.width == 130 && result.items[0].rect.width == 80
            && result.items[1].rect.width == 50, "Content preferred and weighted maximum leave surplus unused");
}

void both_axes() {
    Host host;
    host.natural = {{0, {{20, 10}, {80, 30}}}, {1, {{10, 20}, {50, 70}}}};
    const GridSpec spec{.columns = {Track::content(), Track::content()}, .rows = {Track::content(), Track::content()},
                        .column_gap = 4, .row_gap = 6, .padding = {2, 3, 8, 7}};
    const auto result = compute(host, spec, {500, 500}, {{}, {.id = 1, .column = 1, .row = 1}});
    require(result.succeeded() && result.size == Size{144, 116}, "Both content axes must resolve all occupied tracks");
    require(result.items[1].cell_rect == Rect{86, 39, 50, 70}, "Content offsets must account for prior tracks and gap");
}

void margins() {
    Host host;
    host.natural[0] = {{20, 10}, {80, 30}};
    const GridItem item{.margin = {3, 4, 5, 6}};
    expect_box(compute(host, content_grid(), {200, 100}, {item}), {0, 0, 88, 40}, {3, 4, 80, 30}, {88, 40});
    expect_box(compute(host, content_grid(), {}, {item}), {0, 0, 28, 20}, {3, 4, 20, 10}, {28, 20});
}

void width_reflow() {
    Host host;
    host.wrap = true;
    const auto result = compute(host, content_grid(), {150, 200});
    expect_box(result, {0, 0, 150, 60}, {0, 0, 150, 60}, {150, 60});
    require(result.measurement_count == 2 && host.calls[1].input.known_width == 150,
            "400x20 intrinsic text must be remeasured at width 150 before row sizing");
}

void alignment_width() {
    for (const auto alignment : {HorizontalAlignment::Start, HorizontalAlignment::Center, HorizontalAlignment::End}) {
        Host host;
        host.natural[0] = {{20, 10}, {80, 20}};
        host.constrained = MeasureResult{{10, 30}, {200, 40}};
        const GridSpec spec{.columns = {Track::fixed(200)}, .rows = {Track::content()}};
        const GridItem item{.margin = {10, 5, 20, 7}, .horizontal_alignment = alignment,
                            .vertical_alignment = VerticalAlignment::End, .preferred_size = {999, 999}};
        const auto result = compute(host, spec, {400, 300}, {item});
        const Unit x = alignment == HorizontalAlignment::Start ? 10.0f
            : alignment == HorizontalAlignment::Center ? 55.0f : 100.0f;
        expect_box(result, {0, 0, 200, 52}, {x, 5, 80, 40}, {200, 52});
        require(host.calls[1].input.known_width == 80, "Remeasure at aligned item width, not full cell width");
    }
}

void clipped_width() {
    Host host;
    host.wrap = true;
    const GridSpec spec{.columns = {Track::fixed(170)}, .rows = {Track::content()}};
    const GridItem item{.margin = {8, 0, 12, 0}, .horizontal_alignment = HorizontalAlignment::End};
    expect_box(compute(host, spec, {500, 300}, {item}), {0, 0, 170, 60}, {8, 0, 150, 60}, {170, 60});
    require(host.calls[1].input.known_width == 150, "Clipped non-stretch width must drive height measurement");
}

void zero_width() {
    Host host;
    const GridSpec spec{.columns = {Track::fixed(10)}, .rows = {Track::content()}};
    const GridItem item{.margin = {12, 3, 20, 5}};
    expect_box(compute(host, spec, {}, {item}), {0, 0, 10, 28}, {10, 3, 0, 20}, {10, 28});
    require(!host.calls[0].input.known_width && host.calls[1].input.known_width.has_value()
            && *host.calls[1].input.known_width == 0, "Known zero width must never become an unconstrained measurement");
}

void advisory_space() {
    Host host;
    auto spec = content_grid();
    spec.padding = {10, 20, 30, 40};
    const GridItem item{.margin = {3, 4, 7, 6}};
    const auto result = compute(host, spec, {200, 170}, {item});
    require(result.succeeded(), "Advisory-space fixture must lay out");
    require(host.calls[0].input.available == Size{150, 100}, "Intrinsic available size must exclude padding and margins");
    require(host.calls[1].input.available == Size{150, 100}, "Constrained pass must retain advisory height");
    Host zero_host;
    require(compute(zero_host, spec, {}, {item}).succeeded(), "Zero container is allowed with overflowing content minima");
    require(zero_host.calls[0].input.available == Size{}, "Insufficient advisory space must clamp to zero");
}

void fixed_weighted_compatibility() {
    Host host;
    host.natural[0] = {{400, 400}, {1000, 1000}};
    for (const auto& column : {Track::fixed(30), Track::weighted(1, 30, 30.0f)}) {
        const GridSpec spec{.columns = {column}, .rows = {Track::fixed(20)}};
        expect_box(compute(host, spec, {100, 100}), {0, 0, 30, 20}, {0, 0, 30, 20}, {30, 20});
    }
}

void row_remeasurement() {
    Host host;
    host.constrained = MeasureResult{{40, 80}, {400, 100}};
    const auto result = compute(host, content_grid(), {100, 30});
    expect_box(result, {0, 0, 100, 80}, {0, 0, 100, 80}, {100, 80});
}

void callback_order() {
    Host host;
    const auto maximum_id = std::numeric_limits<ItemId>::max();
    host.natural = {{0, {{}, {10, 20}}}, {maximum_id, {{}, {30, 40}}}};
    const auto result = compute(host, content_grid(), {100, 100}, {{.id = maximum_id}, {}});
    require(result.succeeded() && result.items[0].id == maximum_id && result.items[1].id == 0,
            "Measurement must preserve input order and opaque IDs");
    require(host.calls.size() == 4 && host.calls[0].id == maximum_id && host.calls[1].id == 0
            && host.calls[2].id == maximum_id && host.calls[3].id == 0,
            "Each pass must visit input order exactly once");
    require(!host.calls[0].input.known_width && !host.calls[1].input.known_width
            && host.calls[2].input.known_width && host.calls[3].input.known_width,
            "All intrinsic measurements must finish before final-width measurement begins");
}

void invalid_values(std::size_t call) {
    for (const Unit invalid : {-1.0f, std::numeric_limits<Unit>::infinity(),
                               -std::numeric_limits<Unit>::infinity(), std::numeric_limits<Unit>::quiet_NaN()}) {
        for (int field = 0; field < 4; ++field) {
            Host host;
            host.invalid_call = call;
            host.invalid_result = {{1, 1}, {100, 100}};
            switch (field) {
            case 0: host.invalid_result.minimum.width = invalid; break;
            case 1: host.invalid_result.minimum.height = invalid; break;
            case 2: host.invalid_result.preferred.width = invalid; break;
            default: host.invalid_result.preferred.height = invalid; break;
            }
            expect_error(compute(host), GridErrorCode::InvalidMeasurement, 0, call);
            require(host.calls.size() == call, "Stop immediately after an invalid callback result");
        }
    }
}

void invalid_intrinsic() { invalid_values(1); }
void invalid_constrained() { invalid_values(2); }

void invalid_order() {
    for (const std::size_t call : {std::size_t{1}, std::size_t{2}}) {
        for (const MeasureResult invalid : {MeasureResult{{50, 0}, {40, 10}}, MeasureResult{{0, 50}, {10, 40}}}) {
            Host host;
            host.invalid_call = call;
            host.invalid_result = invalid;
            expect_error(compute(host), GridErrorCode::InvalidMeasurement, 0, call);
        }
    }
    Host host;
    host.natural[7] = {{1, 1}, {10, 10}};
    host.invalid_call = 4;
    host.invalid_result = {{2, 2}, {1, 1}};
    expect_error(compute(host, content_grid(), {100, 100}, {{}, {.id = 7}}), GridErrorCode::InvalidMeasurement, 7, 4);
}

void input_validation() {
    Host host;
    expect_error(compute(host, content_grid(), {100, 100}, {{}, {.id = 8, .column = 9}}),
                 GridErrorCode::InvalidPlacement, 8, 0);
    expect_error(compute(host, content_grid(), {100, 100}, {{}, {}}), GridErrorCode::DuplicateItemId, 0, 0);
    expect_error(compute(host, content_grid(), {-1, 100}), GridErrorCode::InvalidContainerSize, {}, 0);
    expect_error(compute(host, content_grid(), {100, 100}, {{.preferred_size = {-1, 0}}}),
                 GridErrorCode::InvalidPreferredSize, 0, 0);
    auto invalid_grid = content_grid();
    invalid_grid.column_gap = -1;
    expect_error(compute(host, invalid_grid), GridErrorCode::InvalidGap, {}, 0);
    invalid_grid.column_gap = 0;
    invalid_grid.columns = {Track::content(), Track::content()};
    expect_error(compute(host, invalid_grid, {100, 100}, {{.column_span = 3}}), GridErrorCode::InvalidSpan, 0, 0);
    require(host.calls.empty(), "All inputs must be validated before invoking host callbacks");
}

void numeric_overflow() {
    constexpr Unit maximum = std::numeric_limits<Unit>::max();
    Host host;
    host.natural[0] = {{maximum, 1}, {maximum, 1}};
    expect_error(compute(host, content_grid(), {maximum, maximum}, {{.margin = {maximum, 0, 0, 0}}}),
                 GridErrorCode::NumericOverflow, {}, 1);
    host.natural[0] = {{1, maximum}, {1, maximum}};
    expect_error(compute(host, content_grid(), {maximum, maximum}, {{.margin = {0, maximum, 0, 0}}}),
                 GridErrorCode::NumericOverflow, {}, 2);
    host.natural[0] = {{maximum, 1}, {maximum, 1}};
    host.natural[1] = host.natural[0];
    auto spec = content_grid();
    spec.columns.push_back(Track::content());
    expect_error(compute(host, spec, {maximum, maximum}, {{}, {.id = 1, .column = 1}}),
                 GridErrorCode::NumericOverflow, {}, 2);
}

void large_preferred() {
    constexpr Unit maximum = std::numeric_limits<Unit>::max();
    Host host;
    host.natural[0] = {{0, 0}, {maximum, maximum}};
    const GridItem item{.margin = {maximum / 4, maximum / 4, maximum / 4, maximum / 4}};
    const auto result = compute(host, content_grid(), {maximum, maximum}, {item});
    require(result.succeeded() && result.size == Size{maximum, maximum},
            "An oversized preferred contribution may shrink to a representable size");
    require(result.items[0].rect == Rect{maximum / 4, maximum / 4, maximum / 2, maximum / 2},
            "Intermediate preferred plus margins must not overflow Unit");
}

void fractional_limits() {
    Host host;
    host.natural[0] = {{0.25f, 0.5f}, {1.25f, 2.5f}};
    expect_box(compute(host, content_grid(), {0.75f, 1.5f}), {0, 0, 0.75f, 1.5f}, {0, 0, 0.75f, 1.5f}, {0.75f, 1.5f});
    constexpr Unit tiny = std::numeric_limits<Unit>::denorm_min();
    Host tiny_host;
    tiny_host.natural[0] = {{tiny, tiny}, {tiny, tiny}};
    expect_box(compute(tiny_host, content_grid(), {}), {0, 0, tiny, tiny}, {0, 0, tiny, tiny}, {tiny, tiny});
    Host rounding_host;
    const GridSpec spec{.columns = {Track::weighted(), Track::weighted(2)}, .rows = {Track::content()}};
    const GridItem item{.margin = {0.01f, 0, 0.02f, 0}};
    const auto rounded = compute(rounding_host, spec, {1, 100}, {item});
    require(rounded.succeeded() && rounded.items[0].rect.width == *rounding_host.calls[1].input.known_width,
            "Measurement constraints and published rect must use identical float rounding");
}

void exceptions() {
    const auto callback = +[](ItemId, const MeasureInput&, void*) -> MeasureResult { throw std::logic_error("host failure"); };
    bool caught = false;
    try {
        (void)compute_grid(content_grid(), std::array{GridItem{}}, {100, 100}, callback);
    } catch (const std::logic_error& error) {
        caught = std::string_view{error.what()} == "host failure";
    }
    require(caught, "Host exceptions must propagate without being reclassified");
    Host host;
    require(compute(host).succeeded(), "A failed callback must not leave global layout state");
}

void reentrant() {
    const auto callback = +[](ItemId, const MeasureInput& input, void* context) -> MeasureResult {
        require(context == nullptr, "Default callback context must be null");
        const GridSpec inner{.columns = {Track::weighted()}, .rows = {Track::fixed(30)}};
        const auto result = compute_grid(inner, std::array{GridItem{}}, {input.known_width.value_or(80), 100});
        require(result.succeeded() && result.measurement_count == 0, "A recursive host call must have independent statistics");
        return {{20, 30}, result.size};
    };
    const auto result = compute_grid(content_grid(), std::array{GridItem{}}, {50, 100}, callback);
    expect_box(result, {0, 0, 50, 30}, {0, 0, 50, 30}, {50, 30});
    require(result.measurement_count == 2, "Recursive layout must not inflate its caller's measurement count");
}

void deterministic() {
    Host host;
    host.wrap = true;
    auto spec = content_grid();
    spec.padding = {1, 2, 3, 4};
    const GridItem item{.margin = {5, 6, 7, 8}, .horizontal_alignment = HorizontalAlignment::End, .preferred_size = {1, 2}};
    const auto columns = spec.columns;
    const auto rows = spec.rows;
    const auto expected = compute(host, spec, {166, 200}, {item});
    for (int repeat = 0; repeat < 20; ++repeat) {
        const auto actual = compute(host, spec, {166, 200}, {item});
        require(actual.succeeded() && actual.size == expected.size && actual.items[0].rect == expected.items[0].rect
                && actual.items[0].cell_rect == expected.items[0].cell_rect && actual.measurement_count == 2,
                "Repeated layouts must be deterministic and reset measurement statistics");
    }
    require(spec.columns == columns && spec.rows == rows && spec.padding == Insets{1, 2, 3, 4}
            && item.margin == Insets{5, 6, 7, 8} && item.preferred_size == Size{1, 2}, "Measurement must not mutate input values");
}

void resize() {
    Host host;
    host.wrap = true;
    for (const auto size : {Size{400, 20}, Size{150, 60}, Size{80, 100}, Size{400, 20}}) {
        const auto result = compute(host, content_grid(), {size.width, 200});
        expect_box(result, {0, 0, size.width, size.height}, {0, 0, size.width, size.height}, size);
        require(result.measurement_count == 2, "Each resize must remeasure without stale cached height");
    }
}

void randomized_invariants() {
    std::mt19937 random{20260926};
    std::uniform_int_distribution<int> count_distribution{1, 8}, value_distribution{0, 100};
    for (int sample = 0; sample < 500; ++sample) {
        Host host;
        host.natural.clear();
        GridSpec spec{.rows = {Track::content()}, .column_gap = 3, .padding = {2, 0, 4, 0}};
        std::vector<GridItem> items;
        const auto count = static_cast<std::uint32_t>(count_distribution(random));
        Unit lower = 6 + 3 * static_cast<Unit>(count - 1);
        Unit upper = lower;
        for (std::uint32_t index = 0; index < count; ++index) {
            const Unit minimum = static_cast<Unit>(value_distribution(random));
            const Unit preferred = minimum + static_cast<Unit>(value_distribution(random));
            host.natural[index] = {{minimum, 10}, {preferred, 10}};
            spec.columns.push_back(Track::content());
            items.push_back({.id = index, .column = index, .margin = {2, 0, 3, 0}});
            lower += minimum + 5;
            upper += preferred + 5;
        }
        const Unit available = static_cast<Unit>(value_distribution(random) * 10);
        const auto result = compute(host, spec, {available, 100}, items);
        const auto larger = compute(host, spec, {available + 20, 100}, items);
        require(result.succeeded() && larger.succeeded() && result.measurement_count == 2 * items.size(),
                "Random content layout must succeed with exactly two measurements per item");
        require(close(result.size.width, std::clamp(available, lower, upper)), "Content extent must match the feasible occupancy range");
        for (std::size_t index = 0; index < items.size(); ++index) {
            const auto& cell = result.items[index].cell_rect;
            const auto& rect = result.items[index].rect;
            const auto bounds = host.natural.at(items[index].id);
            require(cell.width + 0.0001f >= bounds.minimum.width + 5 && cell.width <= bounds.preferred.width + 5.0001f,
                    "Content tracks must stay within intrinsic bounds including margins");
            require(larger.items[index].cell_rect.width + 0.0001f >= cell.width,
                    "Giving the grid more space must not shrink a content track");
            require(rect.width == *host.calls[items.size() + index].input.known_width,
                    "Every final width must match its host measurement constraint");
            require(rect.x >= cell.x && rect.x + rect.width <= cell.x + cell.width + 0.0001f,
                    "Measured item must remain inside its cell");
        }
        std::reverse(items.begin(), items.end());
        const auto reordered = compute(host, spec, {available, 100}, items);
        require(reordered.succeeded() && reordered.size == result.size, "Input order must not affect content sizing");
        for (std::size_t index = 0; index < items.size(); ++index) {
            require(reordered.items[index].rect == result.items[items.size() - 1 - index].rect,
                    "Reordering input must only reorder the output");
        }
    }
}

struct TestCase { std::string_view name; void (*run)(); };

} // namespace

int main(int argc, char** argv) {
    const std::array cases{
        TestCase{"empty_content", empty_content}, TestCase{"static_fallback", static_fallback},
        TestCase{"null_callback", null_callback}, TestCase{"intrinsic_size", intrinsic_size},
        TestCase{"largest_contribution", largest_contribution}, TestCase{"content_shrink", content_shrink},
        TestCase{"minimum_overflow", minimum_overflow}, TestCase{"rigid_content", rigid_content},
        TestCase{"mixed_tracks", mixed_tracks}, TestCase{"weighted_minimum", weighted_minimum},
        TestCase{"weighted_cap", weighted_cap}, TestCase{"both_axes", both_axes}, TestCase{"margins", margins},
        TestCase{"width_reflow", width_reflow}, TestCase{"alignment_width", alignment_width},
        TestCase{"clipped_width", clipped_width}, TestCase{"zero_width", zero_width},
        TestCase{"advisory_space", advisory_space}, TestCase{"fixed_weighted_compatibility", fixed_weighted_compatibility},
        TestCase{"row_remeasurement", row_remeasurement}, TestCase{"callback_order", callback_order},
        TestCase{"invalid_intrinsic", invalid_intrinsic}, TestCase{"invalid_constrained", invalid_constrained},
        TestCase{"invalid_order", invalid_order}, TestCase{"input_validation", input_validation},
        TestCase{"numeric_overflow", numeric_overflow}, TestCase{"large_preferred", large_preferred},
        TestCase{"fractional_limits", fractional_limits}, TestCase{"exceptions", exceptions},
        TestCase{"reentrant", reentrant}, TestCase{"deterministic", deterministic}, TestCase{"resize", resize},
        TestCase{"randomized_invariants", randomized_invariants}
    };
    if (argc != 2) {
        std::cerr << "Provide one measurement test name\n";
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
    std::cerr << "Unknown measurement test: " << argv[1] << '\n';
    return 2;
}
