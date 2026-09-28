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

bool same_rect(const Rect& actual, const Rect& expected) {
    return close(actual.x, expected.x) && close(actual.y, expected.y)
        && close(actual.width, expected.width) && close(actual.height, expected.height);
}

struct Call { ItemId id{}; MeasureInput input; };

struct Host {
    std::map<ItemId, MeasureResult> sizes;
    std::vector<Call> calls;
    bool wrap{};
    std::optional<MeasureResult> constrained;

    static MeasureResult measure(ItemId id, const MeasureInput& input, void* context) {
        auto& host = *static_cast<Host*>(context);
        host.calls.push_back({id, input});
        require(!input.known_height, "Span measurement must still resolve columns before rows");
        const auto found = host.sizes.find(id);
        auto result = found == host.sizes.end() ? MeasureResult{} : found->second;
        if (input.known_width) {
            require(input.available.width == *input.known_width, "Known width and available width must agree");
            if (id == 1 && host.constrained) return *host.constrained;
            if (host.wrap && *input.known_width > 0) {
                const Unit lines = std::max(1.0f, std::ceil(result.preferred.width / *input.known_width));
                result.minimum.height *= lines;
                result.preferred.height *= lines;
            }
        }
        return result;
    }
};

std::vector<GridItem> column_probes(const GridSpec& spec, std::vector<GridItem> items) {
    for (std::size_t index = 0; index < spec.columns.size(); ++index) {
        items.push_back({.id = 1000 + index, .column = static_cast<std::uint32_t>(index)});
    }
    return items;
}

std::vector<GridItem> row_probes(const GridSpec& spec, std::vector<GridItem> items) {
    for (std::size_t index = 0; index < spec.rows.size(); ++index) {
        items.push_back({.id = 2000 + index, .row = static_cast<std::uint32_t>(index)});
    }
    return items;
}

void expect_columns(const GridResult& result, std::initializer_list<Unit> widths) {
    require(result.succeeded() && result.items.size() >= widths.size(), "Expected a successful layout with column probes");
    std::size_t index = result.items.size() - widths.size();
    for (const auto width : widths) {
        require(close(result.items[index++].cell_rect.width, width), "Unexpected column size after span contributions");
    }
}

void expect_cell(const GridResult& result, Rect cell, Rect rect) {
    require(result.succeeded() && !result.items.empty(), "Expected successful span geometry");
    require(same_rect(result.items[0].cell_rect, cell), "Unexpected merged span cell");
    require(same_rect(result.items[0].rect, rect), "Unexpected aligned span rectangle");
}

GridResult measured(const GridSpec& spec, const std::vector<GridItem>& items, Size available, Host& host) {
    return compute_grid(spec, items, available, Host::measure, &host);
}

void expect_error(const GridResult& result, GridErrorCode code, std::optional<ItemId> id, std::size_t calls) {
    require(!result.succeeded() && result.error.code == code && result.error.item_id == id,
            "Unexpected span error or item ID");
    require(result.items.empty() && result.size == Size{} && !result.error.message.empty(), "Span errors must not expose partial geometry");
    require(result.measurement_count == calls, "Span error must preserve actual measurement count");
}

void fixed_columns() {
    const GridSpec spec{.columns = {Track::fixed(40), Track::fixed(60)}, .rows = {Track::fixed(20)}, .column_gap = 10};
    const auto result = compute_grid(spec, std::array{GridItem{.column_span = 2}}, {});
    expect_cell(result, {0, 0, 110, 20}, {0, 0, 110, 20});
    require(result.size == Size{110, 20}, "A fixed span may overflow the container without growing tracks");
}

void fixed_rows() {
    const GridSpec spec{.columns = {Track::fixed(50)}, .rows = {Track::fixed(20), Track::fixed(30)}, .row_gap = 5};
    expect_cell(compute_grid(spec, std::array{GridItem{.row_span = 2}}, {100, 100}), {0, 0, 50, 55}, {0, 0, 50, 55});
}

void both_axes() {
    const GridSpec spec{.columns = {Track::fixed(40), Track::fixed(60)}, .rows = {Track::fixed(20), Track::fixed(30)},
                        .column_gap = 10, .row_gap = 5, .padding = {2, 3, 8, 7}};
    const auto result = compute_grid(spec, std::array{GridItem{.column_span = 2, .row_span = 2}}, {});
    expect_cell(result, {2, 3, 110, 55}, {2, 3, 110, 55});
    require(result.size == Size{120, 65}, "Padding must remain outside the merged span");
}

void subspan() {
    const GridSpec spec{.columns = {Track::fixed(10), Track::fixed(20), Track::fixed(30), Track::fixed(40)},
                        .rows = {Track::fixed(10), Track::fixed(20), Track::fixed(30)},
                        .column_gap = 3, .row_gap = 5, .padding = {2, 4, 6, 8}};
    const auto result = compute_grid(spec, std::array{GridItem{.column = 1, .row = 1, .column_span = 2, .row_span = 2}}, {});
    expect_cell(result, {15, 19, 53, 55}, {15, 19, 53, 55});
    require(result.size == Size{117, 82}, "Span boundaries must exclude neighboring gaps");
}

void alignments() {
    constexpr std::array horizontal{HorizontalAlignment::Start, HorizontalAlignment::Center, HorizontalAlignment::End, HorizontalAlignment::Stretch};
    constexpr std::array vertical{VerticalAlignment::Start, VerticalAlignment::Center, VerticalAlignment::End, VerticalAlignment::Stretch};
    constexpr std::array<Unit, 4> x{10, 30, 50, 10}, y{5, 15, 25, 5}, width{30, 30, 30, 70}, height{20, 20, 20, 40};
    const GridSpec spec{.columns = {Track::fixed(40), Track::fixed(50)}, .rows = {Track::fixed(20), Track::fixed(30)},
                        .column_gap = 10, .row_gap = 10};
    for (std::size_t h = 0; h < horizontal.size(); ++h) {
        for (std::size_t v = 0; v < vertical.size(); ++v) {
            const GridItem item{.column_span = 2, .row_span = 2, .margin = {10, 5, 20, 15},
                                .horizontal_alignment = horizontal[h], .vertical_alignment = vertical[v], .preferred_size = {30, 20}};
            expect_cell(compute_grid(spec, std::array{item}, {}), {0, 0, 100, 60}, {x[h], y[v], width[h], height[v]});
        }
    }
}

void oversized_margins() {
    const GridSpec spec{.columns = {Track::fixed(10), Track::fixed(20)}, .rows = {Track::fixed(10), Track::fixed(20)},
                        .column_gap = 5, .row_gap = 5};
    const GridItem item{.column_span = 2, .row_span = 2, .margin = {100, 100, 100, 100}, .preferred_size = {1000, 1000}};
    expect_cell(compute_grid(spec, std::array{item}, {}), {0, 0, 35, 35}, {35, 35, 0, 0});
}

void overlapping_fixed() {
    const GridSpec spec{.columns = {Track::fixed(10), Track::fixed(20), Track::fixed(30)}, .rows = {Track::fixed(10)}, .column_gap = 5};
    const std::array items{GridItem{.id = 9, .column_span = 2}, GridItem{.id = 0, .column = 1, .column_span = 2},
                           GridItem{.id = std::numeric_limits<ItemId>::max(), .column_span = 3}};
    const auto result = compute_grid(spec, items, {});
    expect_cell(result, {0, 0, 35, 10}, {0, 0, 35, 10});
    require(result.items.size() == 3 && result.items[0].id == 9 && result.items[1].id == 0
            && result.items[2].id == items[2].id, "Overlapping spans preserve all IDs and input order");
    require(result.items[1].cell_rect == Rect{15, 0, 55, 10} && result.items[2].cell_rect == Rect{0, 0, 70, 10},
            "Overlaps must not relocate or duplicate items");
}

void content_preferred() {
    const GridSpec spec{.columns = {Track::content(), Track::content()}, .rows = {Track::fixed(20)}, .column_gap = 10};
    const auto items = column_probes(spec, {{.column_span = 2, .preferred_size = {200, 10}}});
    const auto result = compute_grid(spec, items, {500, 100});
    expect_columns(result, {95, 95});
    require(result.size.width == 200 && result.measurement_count == 0, "Static preferred size must size a Content span");
    expect_columns(compute_grid(spec, items, {100, 100}), {45, 45});
}

void content_minimum() {
    const GridSpec spec{.columns = {Track::content(), Track::content()}, .rows = {Track::fixed(20)}, .column_gap = 10};
    Host host;
    host.sizes[1] = {{200, 10}, {400, 10}};
    const auto items = column_probes(spec, {{.id = 1, .column_span = 2}});
    const auto result = measured(spec, items, {100, 100}, host);
    expect_columns(result, {95, 95});
    require(result.size.width == 200 && result.items[0].rect.width == 200, "Content spanning minima must survive insufficient space");
}

void existing_content() {
    const GridSpec spec{.columns = {Track::content(), Track::content()}, .rows = {Track::fixed(20)}, .column_gap = 10};
    const auto items = column_probes(spec, {{.id = 1, .preferred_size = {80, 10}}, {.id = 2, .column = 1, .preferred_size = {20, 10}},
                                           {.id = 3, .column_span = 2, .preferred_size = {200, 10}}});
    expect_columns(compute_grid(spec, items, {500, 100}), {125, 65});
}

void fixed_content() {
    const GridSpec spec{.columns = {Track::fixed(80), Track::content()}, .rows = {Track::fixed(20)}, .column_gap = 10};
    const auto items = column_probes(spec, {{.column_span = 2, .margin = {3, 0, 7, 0}, .preferred_size = {200, 10}}});
    const auto result = compute_grid(spec, items, {500, 100});
    expect_columns(result, {80, 120});
    expect_cell(result, {0, 0, 210, 20}, {3, 0, 200, 20});
}

void content_weighted() {
    const GridSpec spec{.columns = {Track::fixed(50), Track::content(), Track::weighted(1, 20)},
                        .rows = {Track::fixed(20)}, .column_gap = 10};
    Host host;
    host.sizes[1] = {{100, 10}, {260, 10}};
    const auto items = column_probes(spec, {{.id = 1, .column_span = 3}});
    expect_columns(measured(spec, items, {300, 100}, host), {50, 170, 60});
    expect_columns(measured(spec, items, {}, host), {50, 10, 20});
}

void weighted_minimum() {
    const GridSpec spec{.columns = {Track::weighted(), Track::weighted(2), Track::weighted(3)}, .rows = {Track::fixed(20)}};
    Host host;
    host.sizes[1] = {{120, 10}, {600, 10}};
    const auto items = column_probes(spec, {{.id = 1, .column_span = 3}});
    expect_columns(measured(spec, items, {}, host), {20, 40, 60});
    expect_columns(measured(spec, items, {300, 100}, host), {50, 100, 150});
}

void weighted_existing_minimum() {
    const GridSpec spec{.columns = {Track::weighted(1, 60), Track::weighted(3)}, .rows = {Track::fixed(20)}};
    Host host;
    host.sizes[1] = {{100, 10}, {200, 10}};
    const auto items = column_probes(spec, {{.id = 1, .column_span = 2}});
    expect_columns(measured(spec, items, {}, host), {70, 30});
}

void weighted_cap() {
    const GridSpec spec{.columns = {Track::weighted(1, 0, 30.0f), Track::weighted(3)}, .rows = {Track::fixed(20)}};
    Host host;
    host.sizes[1] = {{200, 10}, {200, 10}};
    expect_columns(measured(spec, column_probes(spec, {{.id = 1, .column_span = 2}}), {}, host), {30, 170});
}

void all_capped() {
    const GridSpec spec{.columns = {Track::fixed(20), Track::weighted(1, 0, 30.0f), Track::weighted(3, 0, 70.0f)},
                        .rows = {Track::fixed(20)}, .column_gap = 5};
    Host host;
    host.sizes[1] = {{200, 10}, {300, 10}};
    const auto result = measured(spec, column_probes(spec, {{.id = 1, .column_span = 3}}), {500, 100}, host);
    expect_columns(result, {20, 30, 70});
    require(result.items[0].rect.width == 130 && result.size.width == 130, "Hard sizes and caps take priority over an impossible item minimum");
}

void weighted_preferred() {
    const GridSpec spec{.columns = {Track::weighted(), Track::weighted(3)}, .rows = {Track::fixed(20)}};
    const auto items = column_probes(spec, {{.column_span = 2, .preferred_size = {10000, 10}}});
    expect_columns(compute_grid(spec, items, {100, 100}), {25, 75});
    expect_columns(compute_grid(spec, items, {}), {0, 0});
}

void overlapping_contributions() {
    const GridSpec spec{.columns = {Track::content(), Track::content(), Track::content()}, .rows = {Track::fixed(20)}};
    Host host;
    host.sizes = {{1, {{100, 10}, {100, 10}}}, {2, {{100, 10}, {100, 10}}}};
    const auto items = column_probes(spec, {{.id = 1, .column_span = 2}, {.id = 2, .column = 1, .column_span = 2}});
    const auto result = measured(spec, items, {}, host);
    expect_columns(result, {50, 50, 50});
    require(result.items[0].rect.width == 100 && result.items[1].rect.width == 100,
            "Equal-span groups must share growth without processing-order bias");
}

void duplicate_spans() {
    const GridSpec spec{.columns = {Track::content(), Track::content()}, .rows = {Track::fixed(20)}};
    const auto items = column_probes(spec, {{.id = 1, .column_span = 2, .preferred_size = {100, 10}},
                                           {.id = 2, .column_span = 2, .preferred_size = {120, 10}}});
    expect_columns(compute_grid(spec, items, {1000, 100}), {60, 60});
}

void shorter_spans_first() {
    const GridSpec spec{.columns = {Track::content(), Track::content(), Track::content()}, .rows = {Track::fixed(20)}};
    Host host;
    host.sizes = {{1, {{150, 10}, {150, 10}}}, {2, {{100, 10}, {100, 10}}}};
    const auto items = column_probes(spec, {{.id = 1, .column_span = 3}, {.id = 2, .column_span = 2}});
    expect_columns(measured(spec, items, {}, host), {200.0f / 3, 200.0f / 3, 50.0f / 3});
}

void require_same_by_id(const GridResult& actual, const GridResult& expected) {
    require(actual.succeeded() && expected.succeeded() && actual.size == expected.size
            && actual.items.size() == expected.items.size(), "Input permutations must preserve grid occupancy");
    for (const auto& item : actual.items) {
        const auto found = std::find_if(expected.items.begin(), expected.items.end(), [&](const ItemLayout& candidate) { return candidate.id == item.id; });
        require(found != expected.items.end() && item.cell_rect == found->cell_rect && item.rect == found->rect,
                "Input permutations must preserve each item's geometry exactly");
    }
}

void permutation() {
    const GridSpec spec{.columns = {Track::weighted(1, 0, 40.0f), Track::weighted(3), Track::content(), Track::fixed(30)},
                        .rows = {Track::fixed(20)}, .column_gap = 5};
    Host host;
    host.sizes = {{1, {{100, 10}, {200, 10}}}, {2, {{180, 10}, {240, 10}}},
                  {3, {{150, 10}, {220, 10}}}, {4, {{400, 10}, {500, 10}}}};
    std::vector<GridItem> spans{{.id = 1, .column_span = 2}, {.id = 2, .column = 1, .column_span = 2},
                                {.id = 3, .column = 2, .column_span = 2}, {.id = 4, .column_span = 4}};
    const auto expected = measured(spec, column_probes(spec, spans), {700, 100}, host);
    do {
        require_same_by_id(measured(spec, column_probes(spec, spans), {700, 100}, host), expected);
    } while (std::next_permutation(spans.begin(), spans.end(), [](const GridItem& left, const GridItem& right) { return left.id < right.id; }));
}

void width_reflow() {
    const GridSpec spec{.columns = {Track::fixed(70), Track::fixed(70)}, .rows = {Track::content()}, .column_gap = 10};
    Host host;
    host.wrap = true;
    host.sizes[1] = {{40, 20}, {400, 20}};
    const auto result = measured(spec, {{.id = 1, .column_span = 2}}, {500, 200}, host);
    expect_cell(result, {0, 0, 150, 60}, {0, 0, 150, 60});
    require(host.calls.size() == 2 && host.calls[1].input.known_width == 150 && result.measurement_count == 2,
            "A spanned item must be remeasured once at the entire span width");
}

void row_reflow() {
    const GridSpec spec{.columns = {Track::fixed(150)}, .rows = {Track::content(), Track::content()}, .row_gap = 10};
    Host host;
    host.wrap = true;
    host.sizes[1] = {{40, 20}, {400, 20}};
    const auto items = row_probes(spec, {{.id = 1, .row_span = 2}});
    const auto result = measured(spec, items, {500, 200}, host);
    expect_cell(result, {0, 0, 150, 60}, {0, 0, 150, 60});
    require(result.items[1].cell_rect.height == 25 && result.items[2].cell_rect.height == 25,
            "Row spans must distribute the remeasured height, counting internal gaps once");
    require(result.measurement_count == 2 * items.size(), "Span solving must not add host callback passes");
}

void aligned_measurement() {
    const GridSpec spec{.columns = {Track::fixed(100), Track::fixed(100)}, .rows = {Track::content(), Track::content()},
                        .column_gap = 10, .row_gap = 8};
    Host host;
    host.sizes[1] = {{20, 10}, {80, 20}};
    host.constrained = MeasureResult{{10, 40}, {999, 40}};
    const GridItem item{.id = 1, .column_span = 2, .row_span = 2, .margin = {10, 5, 20, 7},
                        .horizontal_alignment = HorizontalAlignment::Center, .vertical_alignment = VerticalAlignment::End};
    const auto result = measured(spec, {item}, {500, 200}, host);
    expect_cell(result, {0, 0, 210, 52}, {60, 5, 80, 40});
    require(host.calls[1].input.known_width == 80, "Remeasure at aligned item width, not at raw span width");
}

void zero_width() {
    const GridSpec spec{.columns = {Track::fixed(0), Track::fixed(0)}, .rows = {Track::content(), Track::content()}};
    Host host;
    host.sizes[1] = {{40, 20}, {400, 20}};
    const auto result = measured(spec, {{.id = 1, .column_span = 2, .row_span = 2}}, {}, host);
    expect_cell(result, {0, 0, 0, 20}, {0, 0, 0, 20});
    require(!host.calls[0].input.known_width && host.calls[1].input.known_width.has_value()
            && *host.calls[1].input.known_width == 0, "A zero-width span must not be treated as unconstrained");
}

void zero_tracks_with_gaps() {
    const GridSpec spec{.columns = {Track::fixed(0), Track::fixed(0), Track::fixed(0)},
                        .rows = {Track::fixed(0), Track::fixed(0)}, .column_gap = 5, .row_gap = 7};
    expect_cell(compute_grid(spec, std::array{GridItem{.column_span = 3, .row_span = 2}}, {}),
                {0, 0, 10, 7}, {0, 0, 10, 7});
}

void resize() {
    const GridSpec spec{.columns = {Track::weighted(), Track::weighted(2)}, .rows = {Track::content()}, .column_gap = 10};
    Host host;
    host.wrap = true;
    host.sizes[1] = {{40, 20}, {400, 20}};
    for (const auto size : {Size{400, 20}, Size{150, 60}, Size{80, 100}, Size{400, 20}}) {
        const auto result = measured(spec, {{.id = 1, .column_span = 2}}, {size.width, 200}, host);
        expect_cell(result, {0, 0, size.width, size.height}, {0, 0, size.width, size.height});
        require(result.measurement_count == 2, "Each resize needs fresh measurements but only two calls per item");
    }
}

void invalid_spans() {
    const GridSpec spec{.columns = {Track::content(), Track::content()}, .rows = {Track::content(), Track::content()}};
    Host host;
    for (const auto span : {0u, 2u, std::numeric_limits<std::uint32_t>::max()}) {
        expect_error(measured(spec, {{.id = 1, .column_span = 2}, {.id = 9, .column = 1, .column_span = span}}, {100, 100}, host),
                     GridErrorCode::InvalidSpan, 9, 0);
        expect_error(measured(spec, {{.id = 1, .row_span = 2}, {.id = 9, .row = 1, .row_span = span}}, {100, 100}, host),
                     GridErrorCode::InvalidSpan, 9, 0);
    }
    require(host.calls.empty(), "Even a later invalid span must be rejected before any measurement");
}

void numeric_overflow() {
    constexpr Unit maximum = std::numeric_limits<Unit>::max();
    const GridSpec columns{.columns = {Track::content(), Track::content()}, .rows = {Track::fixed(10)}};
    Host host;
    host.sizes[1] = {{maximum, 1}, {maximum, 1}};
    expect_error(measured(columns, {{.id = 1, .column_span = 2, .margin = {maximum, 0, 0, 0}}}, {maximum, maximum}, host),
                 GridErrorCode::NumericOverflow, {}, 1);
    const GridSpec rows{.columns = {Track::fixed(10)}, .rows = {Track::content(), Track::content()}};
    host.sizes[1] = {{1, maximum}, {1, maximum}};
    expect_error(measured(rows, {{.id = 1, .row_span = 2, .margin = {0, maximum, 0, 0}}}, {maximum, maximum}, host),
                 GridErrorCode::NumericOverflow, {}, 2);
    const GridSpec weighted{.columns = {Track::weighted(), Track::weighted()}, .rows = {Track::fixed(10)}};
    host.sizes[1] = {{maximum, 1}, {maximum, 1}};
    expect_error(measured(weighted, {{.id = 1, .column_span = 2, .margin = {maximum, 0, 0, 0}}}, {maximum, maximum}, host),
                 GridErrorCode::NumericOverflow, {}, 1);
}

void huge_offset() {
    constexpr Unit offset = std::numeric_limits<Unit>::max() / 2;
    const GridSpec spec{.columns = {Track::fixed(offset), Track::fixed(1), Track::fixed(2)},
                        .rows = {Track::fixed(offset), Track::fixed(1), Track::fixed(2)}};
    const auto result = compute_grid(spec, std::array{GridItem{.column = 1, .row = 1, .column_span = 2, .row_span = 2}}, {});
    expect_cell(result, {offset, offset, 3, 3}, {offset, offset, 3, 3});
    require(result.items[0].rect.width == 3 && result.items[0].rect.height == 3,
            "Subtracting rounded end positions must not erase a small span at a huge offset");
}

void fractional_limits() {
    const GridSpec spec{.columns = {Track::content(), Track::content()}, .rows = {Track::content(), Track::content()},
                        .column_gap = 0.25f, .row_gap = 0.25f};
    Host host;
    host.sizes[1] = {{0.75f, 1.5f}, {1.25f, 2.5f}};
    const auto result = measured(spec, {{.id = 1, .column_span = 2, .row_span = 2}}, {}, host);
    expect_cell(result, {0, 0, 0.75f, 1.5f}, {0, 0, 0.75f, 1.5f});
    constexpr Unit tiny = std::numeric_limits<Unit>::denorm_min();
    auto tiny_spec = spec;
    tiny_spec.column_gap = 0;
    tiny_spec.row_gap = 0;
    host.sizes[1] = {{tiny, tiny}, {tiny, tiny}};
    const auto tiny_result = measured(tiny_spec, {{.id = 1, .column_span = 2, .row_span = 2}}, {}, host);
    require(tiny_result.succeeded() && tiny_result.items[0].rect == Rect{0, 0, tiny, tiny},
            "Subnormal aggregate spans must retain their size through internal allocation");
}

void large_preferred() {
    constexpr Unit maximum = std::numeric_limits<Unit>::max();
    const GridSpec spec{.columns = {Track::content(), Track::content()}, .rows = {Track::content(), Track::content()}};
    Host host;
    host.sizes[1] = {{0, 0}, {maximum, maximum}};
    const GridItem item{.id = 1, .column_span = 2, .row_span = 2, .margin = {maximum / 4, maximum / 4, maximum / 4, maximum / 4}};
    const auto result = measured(spec, {item}, {maximum, maximum}, host);
    require(result.succeeded() && result.size == Size{maximum, maximum}, "Large span preferences must be allowed to shrink");
    require(result.items[0].rect == Rect{maximum / 4, maximum / 4, maximum / 2, maximum / 2},
            "Span size plus margins must use wide intermediates");
}

void extreme_weights() {
    constexpr Unit huge = std::numeric_limits<Unit>::max();
    constexpr Unit tiny = std::numeric_limits<Unit>::denorm_min();
    const GridSpec spec{.columns = {Track::weighted(huge, 0, 1.0f), Track::weighted(tiny)}, .rows = {Track::fixed(10)}};
    Host host;
    host.sizes[1] = {{100, 10}, {100, 10}};
    expect_columns(measured(spec, column_probes(spec, {{.id = 1, .column_span = 2}}), {}, host), {1, 99});
}

void independent_axes() {
    const GridSpec spec{.columns = {Track::content(), Track::content()}, .rows = {Track::content()}};
    const auto result = compute_grid(spec, std::array{GridItem{.column_span = 2, .preferred_size = {150, 40}}}, {500, 500});
    expect_cell(result, {0, 0, 150, 40}, {0, 0, 150, 40});
}

void randomized_invariants() {
    std::mt19937 random{20260927};
    std::uniform_int_distribution<int> count_distribution{2, 7}, value_distribution{0, 100};
    for (int sample = 0; sample < 500; ++sample) {
        const auto count = static_cast<std::uint32_t>(count_distribution(random));
        GridSpec spec{.rows = {Track::fixed(30)}, .column_gap = 3, .padding = {2, 0, 4, 0}};
        for (std::uint32_t index = 0; index < count; ++index) {
            spec.columns.push_back(index % 3 == 0 ? Track::fixed(10)
                : index % 3 == 1 ? Track::weighted(2, 5, 80.0f) : Track::content());
        }
        Host host;
        std::vector<GridItem> items;
        for (std::uint32_t index = 0; index < 6; ++index) {
            const auto start = static_cast<std::uint32_t>(value_distribution(random)) % (count - 1);
            const auto span = 2 + static_cast<std::uint32_t>(value_distribution(random)) % (count - start - 1);
            const Unit minimum = static_cast<Unit>(value_distribution(random));
            const Unit preferred = minimum + static_cast<Unit>(value_distribution(random));
            host.sizes[index] = {{minimum, 10}, {preferred, 10}};
            items.push_back({.id = index, .column = start, .column_span = span, .margin = {2, 1, 3, 2}});
        }
        items = column_probes(spec, items);
        const auto original_columns = spec.columns;
        const auto small = measured(spec, items, {}, host);
        const auto large = measured(spec, items, {10000, 100}, host);
        require(small.succeeded() && large.succeeded() && small.measurement_count == 2 * items.size()
                && large.measurement_count == 2 * items.size(), "Random spans must finish with bounded measurements");
        for (std::size_t index = 0; index < 6; ++index) {
            const auto& item = items[index];
            bool has_content = false;
            Unit maximum = static_cast<Unit>(item.column_span - 1) * spec.column_gap;
            for (std::size_t track = item.column; track < item.column + item.column_span; ++track) {
                if (spec.columns[track].kind() == TrackKind::Content) has_content = true;
                maximum += spec.columns[track].kind() == TrackKind::Fixed ? 10.0f : 80.0f;
            }
            const auto intrinsic = host.sizes.at(item.id);
            if (has_content || maximum >= intrinsic.minimum.width + 5) {
                require(small.items[index].cell_rect.width + 0.0001f >= intrinsic.minimum.width + 5,
                        "A feasible minimum must be satisfied even when the container is too small");
            }
            if (has_content) {
                require(large.items[index].cell_rect.width + 0.0001f >= intrinsic.preferred.width + 5,
                        "A span containing Content must reach preference with enough space");
            }
            const auto& rect = large.items[index].rect;
            const auto& cell = large.items[index].cell_rect;
            require(rect.x >= cell.x && rect.x + rect.width <= cell.x + cell.width + 0.001f,
                    "A spanning item must stay inside its merged cell");
            require(rect.width == *host.calls[3 * items.size() + index].input.known_width,
                    "Final span width must exactly match constrained measurement");
        }
        for (std::size_t index = 0; index < spec.columns.size(); ++index) {
            const auto small_width = small.items[6 + index].cell_rect.width;
            const auto large_width = large.items[6 + index].cell_rect.width;
            if (spec.columns[index].kind() == TrackKind::Fixed) {
                require(small_width == 10 && large_width == 10, "Spans must never grow fixed tracks");
            } else if (spec.columns[index].kind() == TrackKind::Weighted) {
                require(small_width <= 80 && large_width <= 80, "Span growth must never exceed weighted maxima");
            }
        }
        std::shuffle(items.begin(), items.end(), random);
        require_same_by_id(measured(spec, items, {10000, 100}, host), large);
        require(spec.columns == original_columns, "Span resolution must not mutate public track definitions");
    }
}

struct TestCase { std::string_view name; void (*run)(); };

} // namespace

int main(int argc, char** argv) {
    const std::array cases{
        TestCase{"fixed_columns", fixed_columns}, TestCase{"fixed_rows", fixed_rows}, TestCase{"both_axes", both_axes},
        TestCase{"subspan", subspan}, TestCase{"alignments", alignments}, TestCase{"oversized_margins", oversized_margins},
        TestCase{"overlapping_fixed", overlapping_fixed}, TestCase{"content_preferred", content_preferred},
        TestCase{"content_minimum", content_minimum}, TestCase{"existing_content", existing_content},
        TestCase{"fixed_content", fixed_content}, TestCase{"content_weighted", content_weighted},
        TestCase{"weighted_minimum", weighted_minimum}, TestCase{"weighted_existing_minimum", weighted_existing_minimum},
        TestCase{"weighted_cap", weighted_cap}, TestCase{"all_capped", all_capped}, TestCase{"weighted_preferred", weighted_preferred},
        TestCase{"overlapping_contributions", overlapping_contributions}, TestCase{"duplicate_spans", duplicate_spans},
        TestCase{"shorter_spans_first", shorter_spans_first}, TestCase{"permutation", permutation},
        TestCase{"width_reflow", width_reflow}, TestCase{"row_reflow", row_reflow}, TestCase{"aligned_measurement", aligned_measurement},
        TestCase{"zero_width", zero_width}, TestCase{"zero_tracks_with_gaps", zero_tracks_with_gaps}, TestCase{"resize", resize},
        TestCase{"invalid_spans", invalid_spans}, TestCase{"numeric_overflow", numeric_overflow}, TestCase{"huge_offset", huge_offset},
        TestCase{"fractional_limits", fractional_limits}, TestCase{"large_preferred", large_preferred},
        TestCase{"extreme_weights", extreme_weights}, TestCase{"independent_axes", independent_axes},
        TestCase{"randomized_invariants", randomized_invariants}
    };
    if (argc != 2) {
        std::cerr << "Provide one spanning test name\n";
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
    std::cerr << "Unknown spanning test: " << argv[1] << '\n';
    return 2;
}
