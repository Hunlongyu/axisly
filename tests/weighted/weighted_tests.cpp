#include <gridly/gridly.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <optional>
#include <random>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

using namespace gridly;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool close(Unit actual, Unit expected) {
    const double error = std::abs(static_cast<double>(actual) - expected);
    const double tolerance = std::max(0.00001, std::abs(static_cast<double>(expected)) * 0.000001);
    return std::isfinite(actual) && error <= tolerance;
}

std::vector<GridItem> column_items(std::size_t count) {
    std::vector<GridItem> items;
    for (std::size_t index = 0; index < count; ++index) {
        items.push_back({.id = index, .column = static_cast<std::uint32_t>(index)});
    }
    return items;
}

GridResult layout_columns(const std::vector<Track>& columns, Unit available) {
    const GridSpec spec{.columns = columns, .rows = {Track::fixed(10)}};
    return compute_grid(spec, column_items(columns.size()), {available, 10});
}

void expect_columns(const GridResult& result, Unit extent, std::initializer_list<Unit> widths) {
    require(result.succeeded(), "Expected successful weighted layout");
    require(result.items.size() == widths.size(), "Unexpected item count");
    require(close(result.size.width, extent) && result.size.height == 10, "Unexpected occupied extent");
    Unit offset = 0;
    std::size_t index = 0;
    for (const Unit width : widths) {
        const auto& item = result.items[index];
        require(item.id == index && close(item.rect.x, offset) && close(item.rect.width, width)
                    && item.rect.y == 0 && item.rect.height == 10,
                "Unexpected weighted rectangle");
        offset += width;
        ++index;
    }
}

template<class Function>
void expect_invalid_argument(Function&& function) {
    try {
        function();
    } catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error("Expected invalid weighted limits to be rejected");
}

void equal_weights() {
    expect_columns(layout_columns({Track::weighted(), Track::weighted()}, 200), 200, {100, 100});
    expect_columns(layout_columns({Track::weighted()}, 73.5f), 73.5f, {73.5f});
}

void ratio_one_two() {
    expect_columns(layout_columns({Track::weighted(1), Track::weighted(2)}, 300), 300, {100, 200});
}

void ratio_one_two_three() {
    expect_columns(layout_columns({Track::weighted(1), Track::weighted(2), Track::weighted(3)}, 600), 600, {100, 200, 300});
}

void mixed_tracks() {
    expect_columns(layout_columns({Track::fixed(80), Track::weighted(1), Track::fixed(20), Track::weighted(2)}, 400),
                   400, {80, 100, 20, 200});
    expect_columns(layout_columns({Track::weighted(1), Track::fixed(40), Track::weighted(3)}, 200), 200, {40, 40, 120});
}

void both_axes() {
    const GridSpec spec{.columns = {Track::weighted(1), Track::weighted(2)},
                        .rows = {Track::weighted(3), Track::weighted(1)}};
    const std::array items{GridItem{.id = 4, .column = 1, .row = 1}, GridItem{.id = 0}};
    const auto result = compute_grid(spec, items, {300, 200});
    require(result.succeeded() && result.size == Size{300, 200}, "Both weighted axes must fill their budgets");
    require(result.items.size() == 2 && result.items[0].id == 4 && result.items[1].id == 0,
            "Input item order must be preserved");
    require(result.items[0].rect == Rect{100, 150, 200, 50} && result.items[1].rect == Rect{0, 0, 100, 150},
            "Row and column weights must resolve independently");

    const GridSpec limited{.columns = {Track::fixed(10)},
                           .rows = {Track::weighted(1, 80), Track::weighted(1, 0, 30.0f)}};
    const auto constrained = compute_grid(limited, std::array{GridItem{.id = 1, .row = 1}}, {10, 200});
    require(constrained.succeeded() && constrained.size == Size{10, 200}
                && constrained.items[0].rect == Rect{0, 170, 10, 30},
            "Numeric limits must work on rows too");
}

void padding_and_gaps() {
    const GridSpec spec{.columns = {Track::fixed(50), Track::weighted(1), Track::weighted(2)},
                        .rows = {Track::fixed(10), Track::weighted()},
                        .column_gap = 5, .row_gap = 7, .padding = {11, 13, 19, 23}};
    const std::array items{GridItem{.id = 1}, GridItem{.id = 2, .column = 1, .row = 1}, GridItem{.id = 3, .column = 2, .row = 1}};
    const auto result = compute_grid(spec, items, {390, 200});
    require(result.succeeded() && result.size == Size{390, 200}, "Padding and gaps must be removed before distribution");
    require(result.items[0].rect == Rect{11, 13, 50, 10}, "Fixed track or start padding changed");
    require(result.items[1].rect == Rect{66, 30, 100, 147}, "Unexpected first weighted cell");
    require(result.items[2].rect == Rect{171, 30, 200, 147}, "Unexpected second weighted cell");
}

void no_free_space() {
    expect_columns(layout_columns({Track::fixed(100), Track::weighted(1), Track::weighted(2)}, 100), 100, {100, 0, 0});
    const GridSpec spec{.columns = {Track::weighted(), Track::weighted()}, .rows = {Track::fixed(10)},
                        .column_gap = 10, .padding = {5, 0, 15, 0}};
    const auto result = compute_grid(spec, column_items(2), {30, 10});
    require(result.succeeded() && result.size == Size{30, 10}, "An exactly exhausted budget is valid");
    require(result.items[0].rect == Rect{5, 0, 0, 10} && result.items[1].rect == Rect{15, 0, 0, 10},
            "Zero weighted sizes must retain padding and gaps");
}

void insufficient_space() {
    expect_columns(layout_columns({Track::fixed(100), Track::weighted(1), Track::weighted(2)}, 20), 100, {100, 0, 0});
    expect_columns(layout_columns({Track::weighted(1, 80), Track::weighted(2, 50)}, 100), 130, {80, 50});
    const GridSpec spec{.columns = {Track::weighted(), Track::weighted()}, .rows = {Track::fixed(10)},
                        .column_gap = 50, .padding = {30, 0, 40, 0}};
    const auto result = compute_grid(spec, column_items(2), {10, 10});
    require(result.succeeded() && result.size == Size{120, 10} && result.items[1].rect == Rect{80, 0, 0, 10},
            "Padding and gaps larger than the budget must overflow without negative sizes");
}

void zero_container() {
    expect_columns(layout_columns({Track::weighted(), Track::weighted(2)}, 0), 0, {0, 0});
    expect_columns(layout_columns({Track::weighted(1, 20), Track::fixed(10)}, 0), 30, {20, 10});
    const GridSpec spec{.columns = {Track::weighted()}, .rows = {Track::weighted()}};
    const auto result = compute_grid(spec, std::array{GridItem{}}, {});
    require(result.succeeded() && result.size == Size{} && result.items[0].rect == Rect{}, "Zero budget on both axes must be valid");
}

void minimum_freeze() {
    expect_columns(layout_columns({Track::weighted(1, 80), Track::weighted(1)}, 100), 100, {80, 20});
    expect_columns(layout_columns({Track::weighted(1, 80), Track::weighted(1)}, 200), 200, {100, 100});
    expect_columns(layout_columns({Track::weighted(1, 80), Track::weighted(2), Track::weighted(1)}, 200), 200, {80, 80, 40});
}

void maximum_freeze() {
    expect_columns(layout_columns({Track::weighted(1, 0, 20.0f), Track::weighted(1)}, 100), 100, {20, 80});
    expect_columns(layout_columns({Track::weighted(2, 0, 90.0f), Track::weighted(1), Track::weighted(3)}, 330),
                   330, {90, 60, 180});
}

void competing_bounds() {
    // Freezing both initial violations would incorrectly leave the first at 60.
    expect_columns(layout_columns({Track::weighted(1, 60), Track::weighted(1, 0, 1.0f)}, 100), 100, {99, 1});
    // Here the minimum wins; the second track must leave its initial maximum.
    expect_columns(layout_columns({Track::weighted(1, 150), Track::weighted(1, 0, 80.0f)}, 200), 200, {150, 50});
    // Opposing violations can cancel at a valid clamped solution.
    expect_columns(layout_columns({Track::weighted(1, 60), Track::weighted(1, 0, 40.0f)}, 100), 100, {60, 40});
}

void cascading_bounds() {
    expect_columns(layout_columns({Track::weighted(1, 0, 10.0f), Track::weighted(1, 0, 20.0f), Track::weighted()}, 100),
                   100, {10, 20, 70});
    expect_columns(layout_columns({Track::weighted(1, 90), Track::weighted(1, 60), Track::weighted(2)}, 200),
                   200, {90, 60, 50});
    expect_columns(layout_columns({Track::weighted(1, 0, 10.0f), Track::weighted(1, 0, 40.0f), Track::weighted()}, 100),
                   100, {10, 40, 50});
}

void all_capped() {
    expect_columns(layout_columns({Track::weighted(1, 0, 20.0f), Track::weighted(2, 0, 30.0f)}, 200), 50, {20, 30});
    expect_columns(layout_columns({Track::fixed(10), Track::weighted(1, 0, 0.0f)}, 200), 10, {10, 0});
    const GridSpec spec{.columns = {Track::weighted(1, 0, 20.0f), Track::weighted(1, 0, 30.0f)},
                        .rows = {Track::fixed(10)}, .column_gap = 5, .padding = {10, 0, 15, 0}};
    const auto result = compute_grid(spec, column_items(2), {200, 10});
    require(result.succeeded() && result.size.width == 80 && result.items[1].rect == Rect{35, 0, 30, 10},
            "Capped extent must include only used tracks, gaps and padding");
}

void equal_bounds() {
    expect_columns(layout_columns({Track::weighted(100, 30, 30.0f), Track::weighted()}, 100), 100, {30, 70});
    expect_columns(layout_columns({Track::weighted(100, 30, 30.0f), Track::weighted()}, 20), 30, {30, 0});
}

void empty_items() {
    const GridSpec spec{.columns = {Track::weighted(), Track::weighted(2)}, .rows = {Track::weighted()}};
    const auto result = compute_grid(spec, {}, {300, 200});
    require(result.succeeded() && result.items.empty() && result.size == Size{300, 200},
            "Tracks must size independently of whether items are present");
    const GridSpec one_axis{.columns = {Track::weighted()}, .padding = {5, 7, 11, 13}};
    const auto partial = compute_grid(one_axis, {}, {100, 200});
    require(partial.succeeded() && partial.size == Size{100, 20}, "An empty axis must only occupy its padding");
}

void numeric_limits() {
    const auto unbounded = Track::weighted();
    const auto& original = std::get<WeightedTrack>(unbounded.definition());
    require(original.weight == 1 && original.minimum == 0 && !original.maximum, "Default weighted limits changed");
    const auto bounded = Track::weighted(2, 10, 30.0f);
    const auto& limits = std::get<WeightedTrack>(bounded.definition());
    require(limits.weight == 2 && limits.minimum == 10 && limits.maximum == 30, "Weighted limits must be retained");
    const auto infinity = std::numeric_limits<Unit>::infinity();
    for (const Unit bad : {-1.0f, infinity, -infinity, std::numeric_limits<Unit>::quiet_NaN()}) {
        expect_invalid_argument([bad] { (void)Track::weighted(1, bad); });
        expect_invalid_argument([bad] { (void)Track::weighted(1, 0, bad); });
    }
    expect_invalid_argument([] { (void)Track::weighted(1, 30, 20.0f); });
    expect_invalid_argument([] { (void)Track::range(Track::fixed(10), Track::weighted(1, 20)); });
    expect_invalid_argument([] { (void)Track::range(Track::fixed(10), Track::weighted(1, 0, 100.0f)); });
}

void extreme_weights() {
    const auto maximum = std::numeric_limits<Unit>::max();
    const auto tiny = std::numeric_limits<Unit>::denorm_min();
    expect_columns(layout_columns({Track::weighted(maximum), Track::weighted(maximum)}, 200), 200, {100, 100});
    expect_columns(layout_columns({Track::weighted(tiny), Track::weighted(2 * tiny)}, 300), 300, {100, 200});
    expect_columns(layout_columns({Track::weighted(maximum, 0, 10.0f), Track::weighted(tiny)}, 100), 100, {10, 90});
    expect_columns(layout_columns({Track::weighted(tiny), Track::weighted(maximum, 0, 10.0f)}, 100), 100, {90, 10});
    expect_columns(layout_columns({Track::weighted(tiny), Track::weighted(maximum, 0, 0.0f)}, 100), 100, {100, 0});

    const auto large = layout_columns({Track::weighted(1), Track::weighted(2), Track::weighted(3)}, maximum);
    require(large.succeeded() && large.size.width == maximum, "A finite maximum-sized container must not falsely overflow");
    for (const auto& item : large.items) {
        require(std::isfinite(item.rect.x) && std::isfinite(item.rect.width) && item.rect.width > 0,
                "Extreme finite weights must produce finite geometry");
    }
    const auto small = layout_columns({Track::weighted(), Track::weighted()}, 2 * tiny);
    require(small.succeeded() && small.size.width == 2 * tiny && small.items[0].rect.width == tiny
                && small.items[1].rect.x == tiny && small.items[1].rect.width == tiny,
            "Subnormal container sizes must remain meaningful");
}

void weight_scale() {
    const auto first = layout_columns({Track::weighted(1, 50, 80.0f), Track::weighted(2), Track::weighted(3)}, 600);
    const auto scaled = layout_columns({Track::weighted(7, 50, 80.0f), Track::weighted(14), Track::weighted(21)}, 600);
    require(first.succeeded() && scaled.succeeded() && first.size == scaled.size, "Scaled weights must preserve the extent");
    for (std::size_t index = 0; index < first.items.size(); ++index) {
        require(first.items[index].rect == scaled.items[index].rect, "Uniform weight scaling must preserve rectangles");
    }
}

void resize_and_determinism() {
    const std::vector<Track> columns{Track::fixed(20), Track::weighted(1, 50, 150.0f), Track::weighted(2)};
    const auto saved = columns;
    expect_columns(layout_columns(columns, 320), 320, {20, 100, 200});
    expect_columns(layout_columns(columns, 620), 620, {20, 150, 450});
    expect_columns(layout_columns(columns, 60), 70, {20, 50, 0});
    const auto baseline = layout_columns(columns, 320);
    for (int iteration = 0; iteration < 50; ++iteration) {
        (void)layout_columns(columns, 620);
        const auto repeated = layout_columns(columns, 320);
        require(repeated.succeeded() && repeated.size == baseline.size && repeated.items.size() == baseline.items.size(),
                "Resize must not retain state");
        for (std::size_t index = 0; index < baseline.items.size(); ++index) {
            require(repeated.items[index].rect == baseline.items[index].rect, "Repeated weighted rectangles must be identical");
        }
    }
    require(columns == saved, "Weighted computation must not modify input definitions");
}

void numeric_overflow() {
    const auto maximum = std::numeric_limits<Unit>::max();
    for (const auto& columns : {
             std::vector{Track::weighted(1, maximum), Track::weighted(1, maximum)},
             std::vector{Track::fixed(maximum), Track::weighted(1, maximum)}}) {
        const auto result = layout_columns(columns, maximum);
        require(result.error.code == GridErrorCode::NumericOverflow && result.size == Size{} && result.items.empty(),
                "An unrepresentable minimum extent must fail without partial results");
    }
    const GridSpec spec{.columns = {Track::weighted()}, .rows = {Track::weighted()}, .padding = {maximum, 0, maximum, 0}};
    const auto result = compute_grid(spec, {}, {maximum, maximum});
    require(result.error.code == GridErrorCode::NumericOverflow, "Weighted tracks must not hide padding overflow");
}

void reference_cases() {
    // Independently solve the monotone clamp equation by bisection. This is
    // intentionally different from the production freeze/redistribute method.
    std::mt19937 random{20260924u};
    for (int test = 0; test < 500; ++test) {
        const std::size_t count = 1 + random() % 8;
        std::vector<Track> columns;
        std::vector<double> weights, minima, maxima;
        double minimum_sum = 0;
        double maximum_sum = 0;
        for (std::size_t index = 0; index < count; ++index) {
            const Unit weight = static_cast<Unit>(1 + random() % 10);
            const Unit minimum = static_cast<Unit>(random() % 100);
            const Unit maximum = minimum + static_cast<Unit>(random() % 200);
            columns.push_back(Track::weighted(weight, minimum, maximum));
            weights.push_back(weight);
            minima.push_back(minimum);
            maxima.push_back(maximum);
            minimum_sum += minimum;
            maximum_sum += maximum;
        }
        const Unit available = static_cast<Unit>(random() % 2000);
        const double target = std::clamp(static_cast<double>(available), minimum_sum, maximum_sum);
        double low = 0;
        double high = 1;
        for (std::size_t index = 0; index < count; ++index) {
            high = std::max(high, maxima[index] / weights[index]);
        }
        for (int iteration = 0; iteration < 100; ++iteration) {
            const double factor = (low + high) / 2;
            double total = 0;
            for (std::size_t index = 0; index < count; ++index) {
                total += std::clamp(factor * weights[index], minima[index], maxima[index]);
            }
            if (total < target) {
                low = factor;
            } else {
                high = factor;
            }
        }

        const auto result = layout_columns(columns, available);
        require(result.succeeded() && close(result.size.width, static_cast<Unit>(target)), "Reference extent mismatch");
        double offset = 0;
        for (std::size_t index = 0; index < count; ++index) {
            const double expected = std::clamp(high * weights[index], minima[index], maxima[index]);
            require(close(result.items[index].rect.width, static_cast<Unit>(expected))
                        && close(result.items[index].rect.x, static_cast<Unit>(offset)),
                    "Freeze/redistribute result differs from independent clamp-equation reference");
            require(result.items[index].rect.width >= minima[index] && result.items[index].rect.width <= maxima[index],
                    "Weighted size escaped numeric bounds");
            offset += expected;
        }
    }
}

struct TestCase {
    std::string_view name;
    void (*run)();
};

} // namespace

int main(int argc, char** argv) {
    const std::array cases{
        TestCase{"equal_weights", equal_weights}, TestCase{"ratio_one_two", ratio_one_two},
        TestCase{"ratio_one_two_three", ratio_one_two_three}, TestCase{"mixed_tracks", mixed_tracks},
        TestCase{"both_axes", both_axes}, TestCase{"padding_and_gaps", padding_and_gaps},
        TestCase{"no_free_space", no_free_space}, TestCase{"insufficient_space", insufficient_space},
        TestCase{"zero_container", zero_container}, TestCase{"minimum_freeze", minimum_freeze},
        TestCase{"maximum_freeze", maximum_freeze}, TestCase{"competing_bounds", competing_bounds},
        TestCase{"cascading_bounds", cascading_bounds}, TestCase{"all_capped", all_capped},
        TestCase{"equal_bounds", equal_bounds}, TestCase{"empty_items", empty_items},
        TestCase{"numeric_limits", numeric_limits}, TestCase{"extreme_weights", extreme_weights},
        TestCase{"weight_scale", weight_scale}, TestCase{"resize_and_determinism", resize_and_determinism},
        TestCase{"numeric_overflow", numeric_overflow}, TestCase{"reference_cases", reference_cases}
    };
    if (argc != 2) {
        std::cerr << "Provide one weighted-layout test name\n";
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
    std::cerr << "Unknown weighted-layout test: " << argv[1] << '\n';
    return 2;
}
