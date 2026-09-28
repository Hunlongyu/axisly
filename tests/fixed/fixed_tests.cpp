#include <axisly/axisly.hpp>

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

using namespace axisly;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool close(Unit actual, Unit expected) {
    return std::isfinite(actual) && (actual == expected || std::abs(actual - expected) <= 0.00001f);
}

void expect_layout(const GridResult& result, Size size, std::initializer_list<ItemLayout> expected) {
    require(result.succeeded(), "Expected successful layout");
    require(result.error.message.empty() && !result.error.item_id, "Successful result must have no error context");
    require(close(result.size.width, size.width) && close(result.size.height, size.height), "Unexpected occupied grid size");
    require(result.items.size() == expected.size(), "Unexpected item count");
    std::size_t index = 0;
    for (const auto& item : expected) {
        const auto& actual = result.items[index++];
        require(actual.id == item.id, "Item order or ID changed");
        require(close(actual.rect.x, item.rect.x) && close(actual.rect.y, item.rect.y)
                    && close(actual.rect.width, item.rect.width) && close(actual.rect.height, item.rect.height),
                "Unexpected item rectangle");
    }
}

GridSpec two_by_two() {
    return {.columns = {Track::fixed(10), Track::fixed(20)},
            .rows = {Track::fixed(30), Track::fixed(40)}};
}

void expect_error(const GridSpec& spec, const std::vector<GridItem>& items, Size available,
                  GridErrorCode code, std::optional<ItemId> id = {}) {
    const auto result = compute_grid(spec, items, available);
    require(!result.succeeded() && result.error.code == code, "Unexpected layout error code");
    require(!result.error.message.empty(), "Errors must include a diagnostic message");
    require(result.error.item_id == id, "Error must identify the correct item or the grid");
    require(result.items.empty() && result.size == Size{}, "Failure must not expose a partial layout");
}

void one_cell() {
    const GridSpec spec{.columns = {Track::fixed(80)}, .rows = {Track::fixed(40)}};
    const std::array items{GridItem{.id = 7}};
    expect_layout(compute_grid(spec, items, {300, 200}), {80, 40}, {{7, {0, 0, 80, 40}}});
}

void multiple_columns() {
    const GridSpec spec{.columns = {Track::fixed(20), Track::fixed(40), Track::fixed(60)},
                        .rows = {Track::fixed(30)}};
    const std::array items{GridItem{.id = 3, .column = 2}, GridItem{.id = 1}, GridItem{.id = 2, .column = 1}};
    expect_layout(compute_grid(spec, items, {200, 100}), {120, 30},
                  {{3, {60, 0, 60, 30}}, {1, {0, 0, 20, 30}}, {2, {20, 0, 40, 30}}});
}

void multiple_rows() {
    const GridSpec spec{.columns = {Track::fixed(50)},
                        .rows = {Track::fixed(10), Track::fixed(20), Track::fixed(30)}};
    const std::array items{GridItem{.id = 3, .row = 2}, GridItem{.id = 1}, GridItem{.id = 2, .row = 1}};
    expect_layout(compute_grid(spec, items, {200, 100}), {50, 60},
                  {{3, {0, 30, 50, 30}}, {1, {0, 0, 50, 10}}, {2, {0, 10, 50, 20}}});
}

void asymmetric_padding() {
    const GridSpec spec{.columns = {Track::fixed(20), Track::fixed(30)},
                        .rows = {Track::fixed(10), Track::fixed(40)}, .padding = {1, 2, 3, 4}};
    const std::array items{GridItem{.id = 1}, GridItem{.id = 2, .column = 1, .row = 1}};
    expect_layout(compute_grid(spec, items, {100, 100}), {54, 56},
                  {{1, {1, 2, 20, 10}}, {2, {21, 12, 30, 40}}});
}

void gaps() {
    const GridSpec spec{.columns = {Track::fixed(10), Track::fixed(20), Track::fixed(30)},
                        .rows = {Track::fixed(4), Track::fixed(8)}, .column_gap = 3, .row_gap = 5};
    const std::array items{GridItem{.id = 1}, GridItem{.id = 2, .column = 1}, GridItem{.id = 3, .column = 2, .row = 1}};
    expect_layout(compute_grid(spec, items, {100, 100}), {66, 17},
                  {{1, {0, 0, 10, 4}}, {2, {13, 0, 20, 4}}, {3, {36, 9, 30, 8}}});

    const GridSpec single{.columns = {Track::fixed(7)}, .rows = {Track::fixed(9)}, .column_gap = 100, .row_gap = 200};
    const std::array single_item{GridItem{.id = 1}};
    expect_layout(compute_grid(single, single_item, {}), {7, 9}, {{1, {0, 0, 7, 9}}});
}

void fractional() {
    const GridSpec spec{.columns = {Track::fixed(0.25f), Track::fixed(1.5f)},
                        .rows = {Track::fixed(2.25f), Track::fixed(3.5f)},
                        .column_gap = 0.125f, .row_gap = 0.75f, .padding = {0.125f, 0.25f, 0.375f, 0.5f}};
    const std::array items{GridItem{.id = 4, .column = 1, .row = 1}};
    expect_layout(compute_grid(spec, items, {}), {2.375f, 7.25f}, {{4, {0.5f, 3.25f, 1.5f, 3.5f}}});
}

void zero_container() {
    const GridSpec spec{.columns = {Track::fixed(100)}, .rows = {Track::fixed(30)}, .padding = {5, 7, 11, 13}};
    const std::array items{GridItem{.id = 0}};
    for (const Size available : {Size{}, Size{0, 300}, Size{300, 0}, Size{800, 600}}) {
        expect_layout(compute_grid(spec, items, available), {116, 50}, {{0, {5, 7, 100, 30}}});
    }
}

void zero_tracks() {
    const GridSpec spec{.columns = {Track::fixed(0), Track::fixed(0)}, .rows = {Track::fixed(0), Track::fixed(0)},
                        .column_gap = 2, .row_gap = 3, .padding = {1, 2, 3, 4}};
    const std::array items{GridItem{.id = 1}, GridItem{.id = 2, .column = 1, .row = 1}};
    expect_layout(compute_grid(spec, items, {}), {6, 9}, {{1, {1, 2, 0, 0}}, {2, {3, 5, 0, 0}}});
}

void empty_grids() {
    expect_layout(compute_grid({}, {}, {500, 500}), {}, {});
    GridSpec spec;
    spec.padding = {1, 2, 3, 4};
    spec.column_gap = std::numeric_limits<Unit>::max();
    spec.row_gap = std::numeric_limits<Unit>::max();
    expect_layout(compute_grid(spec, {}, {}), {4, 6}, {});
    spec.columns = {Track::fixed(10), Track::fixed(20)};
    spec.column_gap = 3;
    expect_layout(compute_grid(spec, {}, {}), {37, 6}, {});
    spec.rows = {Track::fixed(5)};
    expect_layout(compute_grid(spec, {}, {}), {37, 11}, {});
    expect_error({}, {{.id = 9}}, {}, GridErrorCode::InvalidPlacement, 9);
    spec.rows.clear();
    expect_error(spec, {{.id = 9}}, {}, GridErrorCode::InvalidPlacement, 9);
    spec.columns.clear();
    spec.rows = {Track::fixed(5)};
    expect_layout(compute_grid(spec, {}, {}), {4, 11}, {});
    expect_error(spec, {{.id = 9}}, {}, GridErrorCode::InvalidPlacement, 9);
}

void overflow_layout() {
    const GridSpec spec{.columns = {Track::fixed(50)}, .rows = {Track::fixed(60)}, .padding = {100, 200, 300, 400}};
    const std::array items{GridItem{.id = 1}};
    expect_layout(compute_grid(spec, items, {10, 10}), {450, 660}, {{1, {100, 200, 50, 60}}});
}

void identity_and_order() {
    const auto maximum_id = std::numeric_limits<ItemId>::max();
    const std::array items{GridItem{.id = maximum_id, .column = 1, .row = 1}, GridItem{.id = 0}, GridItem{.id = 9}};
    expect_layout(compute_grid(two_by_two(), items, {}), {30, 70},
                  {{maximum_id, {10, 30, 20, 40}}, {0, {0, 0, 10, 30}}, {9, {0, 0, 10, 30}}});
}

std::array<Unit, 4> invalid_scalars() {
    const auto infinity = std::numeric_limits<Unit>::infinity();
    return {-1.0f, infinity, -infinity, std::numeric_limits<Unit>::quiet_NaN()};
}

void invalid_container() {
    for (const Unit bad : invalid_scalars()) {
        expect_error(two_by_two(), {}, {bad, 100}, GridErrorCode::InvalidContainerSize);
        expect_error(two_by_two(), {}, {100, bad}, GridErrorCode::InvalidContainerSize);
    }
}

void invalid_geometry() {
    for (const Unit bad : invalid_scalars()) {
        auto spec = two_by_two();
        spec.column_gap = bad;
        expect_error(spec, {}, {}, GridErrorCode::InvalidGap);
        spec.column_gap = 0;
        spec.row_gap = bad;
        expect_error(spec, {}, {}, GridErrorCode::InvalidGap);
        spec.row_gap = 0;
        for (auto member : {&Insets::left, &Insets::top, &Insets::right, &Insets::bottom}) {
            spec.padding = {};
            spec.padding.*member = bad;
            expect_error(spec, {}, {}, GridErrorCode::InvalidPadding);
        }
    }
}

void invalid_placement() {
    const auto spec = two_by_two();
    for (const auto index : {2u, std::numeric_limits<std::uint32_t>::max()}) {
        expect_error(spec, {{.id = 1}, {.id = 7, .column = index}}, {}, GridErrorCode::InvalidPlacement, 7);
        expect_error(spec, {{.id = 1}, {.id = 7, .row = index}}, {}, GridErrorCode::InvalidPlacement, 7);
    }
}

void invalid_span() {
    const auto spec = two_by_two();
    for (const auto span : {0u, 2u, std::numeric_limits<std::uint32_t>::max()}) {
        expect_error(spec, {{.id = 0, .column = 1, .column_span = span}}, {}, GridErrorCode::InvalidSpan, 0);
        expect_error(spec, {{.id = 0, .row = 1, .row_span = span}}, {}, GridErrorCode::InvalidSpan, 0);
    }
}

void invalid_item_values() {
    const auto spec = two_by_two();
    for (const Unit bad : invalid_scalars()) {
        for (auto member : {&Insets::left, &Insets::top, &Insets::right, &Insets::bottom}) {
            GridItem item{.id = 8};
            item.margin.*member = bad;
            expect_error(spec, {item}, {}, GridErrorCode::InvalidMargin, 8);
        }
    }
    GridItem item{.id = 8};
    item.horizontal_alignment = static_cast<HorizontalAlignment>(255);
    expect_error(spec, {item}, {}, GridErrorCode::InvalidAlignment, 8);
    item.horizontal_alignment = HorizontalAlignment::Stretch;
    item.vertical_alignment = static_cast<VerticalAlignment>(255);
    expect_error(spec, {item}, {}, GridErrorCode::InvalidAlignment, 8);
}

void range_in_fixed_grid() {
    auto spec = two_by_two();
    spec.columns[1] = Track::range(Track::fixed(25), Track::fixed(100));
    spec.rows[1] = Track::range(Track::fixed(15), Track::content());
    const std::array items{GridItem{.id = 8, .column = 1, .row = 1}};
    const auto result = compute_grid(spec, items, {});
    require(result.succeeded() && result.items.size() == 1
            && result.items[0].cell_rect.width == 25 && result.items[0].cell_rect.height == 15,
            "Empty ranged preferences must retain numeric minima inside fixed grids");
}

void duplicate_ids() {
    for (const ItemId id : {ItemId{0}, std::numeric_limits<ItemId>::max()}) {
        expect_error(two_by_two(), {{.id = id}, {.id = id, .column = 1}}, {}, GridErrorCode::DuplicateItemId, id);
    }
}

void duplicate_order() {
    const auto maximum = std::numeric_limits<ItemId>::max();
    // The first repeated input ID wins, even when another duplicated ID sorts first.
    expect_error(two_by_two(), {{.id = maximum}, {.id = 0}, {.id = maximum}, {.id = 0}, {.id = maximum}},
                 {}, GridErrorCode::DuplicateItemId, maximum);
    expect_error(two_by_two(), {{.id = maximum}, {.id = 0}, {.id = 0}, {.id = maximum}},
                 {}, GridErrorCode::DuplicateItemId, 0);
}

void duplicate_validation_order() {
    const auto spec = two_by_two();
    expect_error(spec, {{.id = 9}, {.id = 9}, {.id = 3, .column = 99}}, {}, GridErrorCode::DuplicateItemId, 9);
    expect_error(spec, {{.id = 9, .column = 99}, {.id = 9}}, {}, GridErrorCode::InvalidPlacement, 9);
    expect_error(spec, {{.id = 9}, {.id = 9, .column = 99}}, {}, GridErrorCode::InvalidPlacement, 9);
    expect_error(spec, {{.id = 9}, {.id = 9, .row_span = 0}}, {}, GridErrorCode::InvalidSpan, 9);
    expect_error(spec, {{.id = 9}, {.id = 9, .margin = {-1,0,0,0}}}, {}, GridErrorCode::InvalidMargin, 9);
    expect_error(spec, {{.id = 9}, {.id = 9, .horizontal_alignment = static_cast<HorizontalAlignment>(255)}},
                 {}, GridErrorCode::InvalidAlignment, 9);
    expect_error(spec, {{.id = 9}, {.id = 9, .preferred_size = {-1,0}}}, {}, GridErrorCode::InvalidPreferredSize, 9);
    auto bad_grid = spec;
    bad_grid.padding.left = -1;
    expect_error(bad_grid, {{.id = 9}, {.id = 9}}, {}, GridErrorCode::InvalidPadding);
    std::size_t calls{};
    const std::array items{GridItem{.id = 9}, GridItem{.id = 9}};
    const auto result = compute_grid(spec, items, {}, [&calls](ItemId, const MeasureInput&) {
        ++calls;
        return MeasureResult{};
    });
    require(result.error.code == GridErrorCode::DuplicateItemId && calls == 0 && result.measurement_count == 0,
            "Duplicate validation must finish before invoking user measurements");
}

void duplicate_randomized() {
    std::mt19937_64 engine(20260928);
    for (std::size_t trial = 0; trial < 400; ++trial) {
        std::vector<GridItem> items;
        const auto count = static_cast<std::size_t>(engine()%129);
        for (std::size_t index = 0; index < count; ++index) {
            auto id = engine();
            if (trial%2 && index && engine()%5 == 0) {
                id = items[static_cast<std::size_t>(engine()%index)].id;
            }
            items.push_back({.id = id});
        }
        std::vector<ItemId> seen;
        std::optional<ItemId> first;
        for (const auto& item : items) {
            if (std::find(seen.begin(), seen.end(), item.id) != seen.end()) {
                first = item.id;
                break;
            }
            seen.push_back(item.id);
        }
        const auto result = compute_grid(two_by_two(), items, {});
        if (first) {
            require(result.error.code == GridErrorCode::DuplicateItemId && result.error.item_id == first && result.items.empty(),
                    "Duplicate error must agree with the independent input-order scan");
        } else {
            require(result.succeeded() && result.items.size() == items.size(), "Distinct full-width IDs must be accepted");
            for (std::size_t index = 0; index < items.size(); ++index) {
                require(result.items[index].id == items[index].id, "ID validation must not reorder output");
            }
        }
    }
}

void numeric_overflow() {
    const auto maximum = std::numeric_limits<Unit>::max();
    GridSpec spec{.columns = {Track::fixed(maximum)}, .rows = {Track::fixed(maximum)}};
    const std::array items{GridItem{.id = 0}};
    expect_layout(compute_grid(spec, items, {}), {maximum, maximum}, {{0, {0, 0, maximum, maximum}}});
    spec.columns.push_back(Track::fixed(maximum));
    expect_error(spec, {}, {}, GridErrorCode::NumericOverflow);
    spec.columns.pop_back();
    spec.rows.push_back(Track::fixed(maximum));
    expect_error(spec, {}, {}, GridErrorCode::NumericOverflow);

    spec = {.columns = {Track::fixed(0), Track::fixed(0), Track::fixed(0)}, .column_gap = maximum};
    expect_error(spec, {}, {}, GridErrorCode::NumericOverflow);
    spec = {.rows = {Track::fixed(0), Track::fixed(0), Track::fixed(0)}, .row_gap = maximum};
    expect_error(spec, {}, {}, GridErrorCode::NumericOverflow);
    spec = {.padding = {maximum, 0, maximum, 0}};
    expect_error(spec, {}, {}, GridErrorCode::NumericOverflow);
    spec.padding = {0, maximum, 0, maximum};
    expect_error(spec, {}, {}, GridErrorCode::NumericOverflow);

    spec = {.columns = {Track::fixed(maximum)}, .padding = {maximum, 0, 0, 0}};
    expect_error(spec, {}, {}, GridErrorCode::NumericOverflow);
    spec.padding = {0, 0, maximum, 0};
    expect_error(spec, {}, {}, GridErrorCode::NumericOverflow);
    spec = {.rows = {Track::fixed(maximum)}, .padding = {0, maximum, 0, 0}};
    expect_error(spec, {}, {}, GridErrorCode::NumericOverflow);
    spec.padding = {0, 0, 0, maximum};
    expect_error(spec, {}, {}, GridErrorCode::NumericOverflow);
}

void deterministic() {
    auto spec = two_by_two();
    spec.column_gap = 0.3f;
    spec.row_gap = 0.7f;
    spec.padding = {1, 2, 3, 4};
    const auto original_columns = spec.columns;
    const auto original_rows = spec.rows;
    const std::array items{GridItem{.id = 7, .column = 1, .row = 1}};
    const auto expected = compute_grid(spec, items, {200, 200});
    require(expected.succeeded(), "Initial deterministic layout must succeed");
    for (int iteration = 0; iteration < 100; ++iteration) {
        expect_error(spec, {{.id = 1, .column = 99}}, {}, GridErrorCode::InvalidPlacement, 1);
        const auto actual = compute_grid(spec, items, {200, 200});
        require(actual.succeeded() && actual.size == expected.size && actual.items.size() == expected.items.size(),
                "Repeated computations must preserve result shape");
        require(actual.items.front().id == expected.items.front().id && actual.items.front().rect == expected.items.front().rect,
                "Repeated computations must produce identical rectangles");
    }
    require(spec.columns == original_columns && spec.rows == original_rows && spec.padding == Insets{1, 2, 3, 4}
                && spec.column_gap == 0.3f && spec.row_gap == 0.7f,
            "Computation must not mutate the grid specification");
    require(items[0].id == 7 && items[0].column == 1 && items[0].row == 1 && items[0].margin == Insets{},
            "Computation must not mutate the items");
}

struct TestCase {
    std::string_view name;
    void (*run)();
};

} // namespace

int main(int argc, char** argv) {
    const std::array cases{
        TestCase{"one_cell", one_cell}, TestCase{"multiple_columns", multiple_columns},
        TestCase{"multiple_rows", multiple_rows}, TestCase{"asymmetric_padding", asymmetric_padding},
        TestCase{"gaps", gaps}, TestCase{"fractional", fractional},
        TestCase{"zero_container", zero_container}, TestCase{"zero_tracks", zero_tracks},
        TestCase{"empty_grids", empty_grids}, TestCase{"overflow_layout", overflow_layout},
        TestCase{"identity_and_order", identity_and_order}, TestCase{"invalid_container", invalid_container},
        TestCase{"invalid_geometry", invalid_geometry}, TestCase{"invalid_placement", invalid_placement},
        TestCase{"invalid_span", invalid_span}, TestCase{"invalid_item_values", invalid_item_values},
        TestCase{"range_in_fixed_grid", range_in_fixed_grid}, TestCase{"duplicate_ids", duplicate_ids},
        TestCase{"duplicate_order", duplicate_order}, TestCase{"duplicate_validation_order", duplicate_validation_order},
        TestCase{"duplicate_randomized", duplicate_randomized},
        TestCase{"numeric_overflow", numeric_overflow}, TestCase{"deterministic", deterministic}
    };
    if (argc != 2) {
        std::cerr << "Provide one fixed-layout test name\n";
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
    std::cerr << "Unknown fixed-layout test: " << argv[1] << '\n';
    return 2;
}
