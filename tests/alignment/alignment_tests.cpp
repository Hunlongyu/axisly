#include <axisly/axisly.hpp>

#include <array>
#include <cmath>
#include <exception>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

using namespace axisly;

constexpr std::array horizontal_modes{HorizontalAlignment::Start, HorizontalAlignment::Center,
                                      HorizontalAlignment::End, HorizontalAlignment::Stretch};
constexpr std::array vertical_modes{VerticalAlignment::Start, VerticalAlignment::Center,
                                    VerticalAlignment::End, VerticalAlignment::Stretch};

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool close(Unit actual, Unit expected) {
    return std::isfinite(actual) && (actual == expected || std::abs(actual - expected) <= 0.0001f);
}

bool same_rect(const Rect& actual, const Rect& expected) {
    return close(actual.x, expected.x) && close(actual.y, expected.y)
        && close(actual.width, expected.width) && close(actual.height, expected.height);
}

GridSpec spec() {
    return {.columns = {Track::fixed(100)}, .rows = {Track::fixed(80)}, .padding = {10, 20, 30, 40}};
}

GridItem item(HorizontalAlignment horizontal, VerticalAlignment vertical) {
    return {.id = 7, .horizontal_alignment = horizontal, .vertical_alignment = vertical, .preferred_size = {30, 10}};
}

void expect(const GridSpec& grid, const GridItem& input, Size available, Rect rectangle, Rect cell, Size extent) {
    const auto result = compute_grid(grid, std::array{input}, available);
    require(result.succeeded() && result.items.size() == 1, "Expected one successful aligned item");
    require(result.error.message.empty() && !result.error.item_id, "Successful alignment must not carry an error");
    require(result.items[0].id == input.id, "Alignment must retain the item ID");
    require(same_rect(result.items[0].rect, rectangle), "Unexpected aligned item rectangle");
    require(same_rect(result.items[0].cell_rect, cell), "Unexpected original cell rectangle");
    require(close(result.size.width, extent.width) && close(result.size.height, extent.height),
            "Item alignment must not change track occupancy");
}

void expect(const GridItem& input, Rect rectangle) {
    expect(spec(), input, {300, 200}, rectangle, {10, 20, 100, 80}, {140, 140});
}

void expect_error(const GridItem& input, GridErrorCode code) {
    const std::array items{GridItem{.id = 1}, input};
    const auto result = compute_grid(spec(), items, {});
    require(!result.succeeded() && result.error.code == code && result.error.item_id == input.id,
            "Unexpected item validation error");
    require(!result.error.message.empty() && result.size == Size{} && result.items.empty(),
            "Item validation failures must not expose partial layout");
}

void defaults() {
    require(GridItem{}.preferred_size == Size{}, "Default static preferred size must be zero");
    expect(GridItem{}, {10, 20, 100, 80});
}

void start() {
    expect(item(HorizontalAlignment::Start, VerticalAlignment::Start), {10, 20, 30, 10});
}

void center() {
    expect(item(HorizontalAlignment::Center, VerticalAlignment::Center), {45, 55, 30, 10});
}

void end() {
    expect(item(HorizontalAlignment::End, VerticalAlignment::End), {80, 90, 30, 10});
}

void stretch() {
    expect(item(HorizontalAlignment::Stretch, VerticalAlignment::Stretch), {10, 20, 100, 80});
    auto input = item(HorizontalAlignment::Stretch, VerticalAlignment::Stretch);
    input.preferred_size = {200, 300};
    expect(input, {10, 20, 100, 80});
}

void all_axis_pairs() {
    constexpr std::array<Unit, 4> x{10, 45, 80, 10}, width{30, 30, 30, 100};
    constexpr std::array<Unit, 4> y{20, 55, 90, 20}, height{10, 10, 10, 80};
    for (std::size_t h = 0; h < horizontal_modes.size(); ++h) {
        for (std::size_t v = 0; v < vertical_modes.size(); ++v) {
            expect(item(horizontal_modes[h], vertical_modes[v]), {x[h], y[v], width[h], height[v]});
        }
    }
}

void margins() {
    GridItem input{.id = 0, .margin = {5, 5, 5, 5}};
    expect(input, {15, 25, 90, 70});
    input.margin = {};
    expect(input, {10, 20, 100, 80});
}

void asymmetric_margins() {
    constexpr std::array<Rect, 4> expected{
        Rect{15, 27, 30, 10}, Rect{40, 52, 30, 10}, Rect{65, 77, 30, 10}, Rect{15, 27, 80, 60}
    };
    for (std::size_t index = 0; index < expected.size(); ++index) {
        auto input = item(horizontal_modes[index], vertical_modes[index]);
        input.margin = {5, 7, 15, 13};
        expect(input, expected[index]);
    }
}

void oversized_preferred() {
    for (std::size_t index = 0; index < horizontal_modes.size(); ++index) {
        auto input = item(horizontal_modes[index], vertical_modes[index]);
        input.preferred_size = {200, 300};
        expect(input, {10, 20, 100, 80});
        input.margin = {5, 7, 15, 13};
        expect(input, {15, 27, 80, 60});
    }
}

void zero_preferred() {
    constexpr std::array<Rect, 3> expected{Rect{10, 20, 0, 0}, Rect{60, 60, 0, 0}, Rect{110, 100, 0, 0}};
    for (std::size_t index = 0; index < expected.size(); ++index) {
        auto input = item(horizontal_modes[index], vertical_modes[index]);
        input.preferred_size = {};
        expect(input, expected[index]);
    }
    auto input = item(HorizontalAlignment::Center, VerticalAlignment::Stretch);
    input.preferred_size.width = 0;
    expect(input, {60, 20, 0, 80});
}

void oversized_margins() {
    for (std::size_t index = 0; index < horizontal_modes.size(); ++index) {
        auto input = item(horizontal_modes[index], vertical_modes[index]);
        input.margin = {150, 100, 20, 30};
        expect(input, {110, 100, 0, 0});
        input.margin = {10, 15, 150, 100};
        expect(input, {20, 35, 0, 0});
        input.margin = {30, 20, 70, 60};
        expect(input, {40, 40, 0, 0});
        input.margin = {std::numeric_limits<Unit>::max(), std::numeric_limits<Unit>::max(),
                        std::numeric_limits<Unit>::max(), std::numeric_limits<Unit>::max()};
        expect(input, {110, 100, 0, 0});
    }
}

void zero_cells() {
    const GridSpec grid{.columns = {Track::fixed(0)}, .rows = {Track::fixed(0)}, .padding = {10, 20, 30, 40}};
    auto input = item(HorizontalAlignment::End, VerticalAlignment::Center);
    input.margin = {10, 10, 10, 10};
    expect(grid, input, {}, {10, 20, 0, 0}, {10, 20, 0, 0}, {40, 60});
}

void fractional() {
    const GridSpec grid{.columns = {Track::fixed(1.5f)}, .rows = {Track::fixed(2.25f)},
                        .padding = {0.125f, 0.25f, 0.375f, 0.5f}};
    auto input = item(HorizontalAlignment::Center, VerticalAlignment::Center);
    input.margin = {0.125f, 0.25f, 0.25f, 0.5f};
    input.preferred_size = {0.375f, 0.5f};
    expect(grid, input, {}, {0.625f, 1, 0.375f, 0.5f}, {0.125f, 0.25f, 1.5f, 2.25f}, {2, 3});
}

void padding_and_gaps() {
    const GridSpec grid{.columns = {Track::fixed(20), Track::fixed(40)}, .rows = {Track::fixed(30), Track::fixed(50)},
                        .column_gap = 5, .row_gap = 7, .padding = {10, 20, 30, 40}};
    auto input = item(HorizontalAlignment::Center, VerticalAlignment::Center);
    input.column = 1;
    input.row = 1;
    input.margin = {3, 4, 5, 6};
    input.preferred_size = {10, 8};
    expect(grid, input, {10, 10}, {49, 77, 10, 8}, {35, 57, 40, 50}, {105, 147});
}

void weighted_cells() {
    const GridSpec grid{.columns = {Track::fixed(20), Track::weighted(1, 0, 50.0f), Track::weighted()},
                        .rows = {Track::weighted()}};
    auto input = item(HorizontalAlignment::Center, VerticalAlignment::End);
    input.column = 2;
    input.margin = {5, 5, 5, 5};
    input.preferred_size = {30, 20};
    expect(grid, input, {200, 100}, {120, 75, 30, 20}, {70, 0, 130, 100}, {200, 100});
    expect(grid, input, {300, 200}, {170, 175, 30, 20}, {70, 0, 230, 200}, {300, 200});
}

void cell_rectangles() {
    const auto grid = spec();
    const std::array items{
        GridItem{.id = 0, .margin = {5, 7, 15, 13}, .horizontal_alignment = HorizontalAlignment::Start,
                 .vertical_alignment = VerticalAlignment::Start, .preferred_size = {20, 20}},
        GridItem{.id = std::numeric_limits<ItemId>::max(), .horizontal_alignment = HorizontalAlignment::End,
                 .vertical_alignment = VerticalAlignment::End, .preferred_size = {60, 50}}
    };
    const auto result = compute_grid(grid, items, {});
    require(result.succeeded() && result.items.size() == 2 && result.items[0].id == 0
                && result.items[1].id == std::numeric_limits<ItemId>::max(), "Overlapping cells must preserve IDs and order");
    require(result.items[0].cell_rect == Rect{10, 20, 100, 80} && result.items[1].cell_rect == result.items[0].cell_rect,
            "Cell rectangles must be independent of per-item geometry");
    require(result.items[0].rect == Rect{15, 27, 20, 20} && result.items[1].rect == Rect{50, 50, 60, 50},
            "Items in one cell must align independently");
}

std::array<Unit, 4> invalid_scalars() {
    const auto infinity = std::numeric_limits<Unit>::infinity();
    return {-1, infinity, -infinity, std::numeric_limits<Unit>::quiet_NaN()};
}

void invalid_preferred() {
    for (const Unit bad : invalid_scalars()) {
        GridItem input{.id = 0, .preferred_size = {bad, 10}};
        expect_error(input, GridErrorCode::InvalidPreferredSize);
        input.preferred_size = {10, bad};
        expect_error(input, GridErrorCode::InvalidPreferredSize);
        input.horizontal_alignment = HorizontalAlignment::Center;
        input.vertical_alignment = VerticalAlignment::End;
        expect_error(input, GridErrorCode::InvalidPreferredSize);
    }
}

void invalid_margins() {
    for (const Unit bad : invalid_scalars()) {
        for (auto member : {&Insets::left, &Insets::top, &Insets::right, &Insets::bottom}) {
            GridItem input{.id = 0};
            input.margin.*member = bad;
            expect_error(input, GridErrorCode::InvalidMargin);
        }
    }
}

void invalid_alignments() {
    GridItem input{.id = 0};
    input.horizontal_alignment = static_cast<HorizontalAlignment>(255);
    expect_error(input, GridErrorCode::InvalidAlignment);
    input.horizontal_alignment = HorizontalAlignment::Stretch;
    input.vertical_alignment = static_cast<VerticalAlignment>(255);
    expect_error(input, GridErrorCode::InvalidAlignment);
}

void extreme_geometry() {
    const auto maximum = std::numeric_limits<Unit>::max();
    const GridSpec large{.columns = {Track::fixed(maximum)}, .rows = {Track::fixed(maximum)}};
    auto input = item(HorizontalAlignment::End, VerticalAlignment::End);
    input.preferred_size = {};
    expect(large, input, {}, {maximum, maximum, 0, 0}, {0, 0, maximum, maximum}, {maximum, maximum});
    input.margin = {maximum, maximum, maximum, maximum};
    expect(large, input, {}, {maximum, maximum, 0, 0}, {0, 0, maximum, maximum}, {maximum, maximum});

    const GridSpec weighted{.columns = {Track::weighted(), Track::weighted(), Track::weighted()}, .rows = {Track::weighted()}};
    input.column = 2;
    input.margin = {};
    const auto edge = compute_grid(weighted, std::array{input}, {maximum, maximum});
    require(edge.succeeded() && edge.items[0].rect == Rect{maximum, maximum, 0, 0},
            "Aligning to the largest finite edge must not create infinity");

    const auto tiny = std::numeric_limits<Unit>::denorm_min();
    const GridSpec small{.columns = {Track::fixed(4 * tiny)}, .rows = {Track::fixed(4 * tiny)}};
    input = item(HorizontalAlignment::Center, VerticalAlignment::Center);
    input.preferred_size = {2 * tiny, 2 * tiny};
    const auto subnormal = compute_grid(small, std::array{input}, {});
    require(subnormal.succeeded() && subnormal.items[0].rect == Rect{tiny, tiny, 2 * tiny, 2 * tiny},
            "Alignment must preserve subnormal geometry");
}

void deterministic() {
    const auto grid = spec();
    auto input = item(HorizontalAlignment::Center, VerticalAlignment::End);
    input.margin = {5, 7, 15, 13};
    const auto original = input;
    for (int iteration = 0; iteration < 50; ++iteration) {
        expect(input, {40, 77, 30, 10});
        expect(GridItem{}, {10, 20, 100, 80});
    }
    require(input.preferred_size == original.preferred_size && input.margin == original.margin
                && input.horizontal_alignment == original.horizontal_alignment && input.vertical_alignment == original.vertical_alignment,
            "Alignment must not mutate item inputs");
    const auto first = compute_grid(grid, std::array{input}, {});
    const auto second = compute_grid(grid, std::array{input}, {});
    require(first.succeeded() && second.succeeded() && first.items[0].rect == second.items[0].rect
                && first.items[0].cell_rect == second.items[0].cell_rect, "Repeated aligned output must be identical");
}

void bounds_invariants() {
    std::mt19937 random{20260925u};
    for (int iteration = 0; iteration < 500; ++iteration) {
        const Unit width = static_cast<Unit>(random() % 1000) / 4;
        const Unit height = static_cast<Unit>(random() % 1000) / 4;
        const GridSpec grid{.columns = {Track::fixed(width)}, .rows = {Track::fixed(height)}, .padding = {10, 20, 30, 40}};
        GridItem input{.id = 0};
        input.horizontal_alignment = horizontal_modes[random() % horizontal_modes.size()];
        input.vertical_alignment = vertical_modes[random() % vertical_modes.size()];
        input.preferred_size = {static_cast<Unit>(random() % 1000) / 4, static_cast<Unit>(random() % 1000) / 4};
        for (auto member : {&Insets::left, &Insets::top, &Insets::right, &Insets::bottom}) {
            input.margin.*member = static_cast<Unit>(random() % 500) / 4;
        }
        const auto result = compute_grid(grid, std::array{input}, {});
        require(result.succeeded() && result.items.size() == 1, "Valid geometry must produce a result");
        const auto& rectangle = result.items[0].rect;
        const auto& cell = result.items[0].cell_rect;
        require(std::isfinite(rectangle.x) && std::isfinite(rectangle.y) && std::isfinite(rectangle.width)
                    && std::isfinite(rectangle.height) && rectangle.width >= 0 && rectangle.height >= 0,
                "Alignment must produce finite non-negative sizes");
        require(rectangle.x >= cell.x && rectangle.y >= cell.y
                    && rectangle.x + rectangle.width <= cell.x + cell.width + 0.0001f
                    && rectangle.y + rectangle.height <= cell.y + cell.height + 0.0001f,
                "Aligned rectangles must remain inside their original cell");
        if (input.horizontal_alignment != HorizontalAlignment::Stretch) {
            require(rectangle.width <= input.preferred_size.width, "Non-Stretch width must not exceed the preferred width");
        }
        if (input.vertical_alignment != VerticalAlignment::Stretch) {
            require(rectangle.height <= input.preferred_size.height, "Non-Stretch height must not exceed the preferred height");
        }
        require(result.size == Size{width + 40, height + 60}, "Margins and preferred sizes must not grow fixed tracks");
    }
}

struct TestCase {
    std::string_view name;
    void (*run)();
};

} // namespace

int main(int argc, char** argv) {
    const std::array cases{
        TestCase{"defaults", defaults}, TestCase{"start", start}, TestCase{"center", center}, TestCase{"end", end},
        TestCase{"stretch", stretch}, TestCase{"all_axis_pairs", all_axis_pairs}, TestCase{"margins", margins},
        TestCase{"asymmetric_margins", asymmetric_margins}, TestCase{"oversized_preferred", oversized_preferred},
        TestCase{"zero_preferred", zero_preferred}, TestCase{"oversized_margins", oversized_margins},
        TestCase{"zero_cells", zero_cells}, TestCase{"fractional", fractional}, TestCase{"padding_and_gaps", padding_and_gaps},
        TestCase{"weighted_cells", weighted_cells}, TestCase{"cell_rectangles", cell_rectangles},
        TestCase{"invalid_preferred", invalid_preferred}, TestCase{"invalid_margins", invalid_margins},
        TestCase{"invalid_alignments", invalid_alignments}, TestCase{"extreme_geometry", extreme_geometry},
        TestCase{"deterministic", deterministic}, TestCase{"bounds_invariants", bounds_invariants}
    };
    if (argc != 2) {
        std::cerr << "Provide one alignment test name\n";
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
    std::cerr << "Unknown alignment test: " << argv[1] << '\n';
    return 2;
}
