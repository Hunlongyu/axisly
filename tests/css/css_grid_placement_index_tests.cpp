#include "../../src/css/grid_internal.hpp"

#include <array>
#include <iostream>
#include <random>
#include <stdexcept>
#include <tuple>

namespace {
using namespace axisly::css;
using namespace axisly::css::detail;
auto rectangle(const Placement& p) {
    return std::tuple{p.row_start, p.row_end, p.column_start, p.column_end};
}
auto extent(const GridExtent& value) {
    return std::tuple{value.row_start, value.row_end, value.column_start, value.column_end};
}
void differential(std::uint32_t seed) {
    std::mt19937 random(seed);
    ExpandedTemplate axis;
    axis.tracks.resize(16);
    constexpr std::array<std::int16_t, 10> lines{-10'000, -100, -16, -2, 1, 2, 9, 20, 9999, 10'000};
    constexpr std::array<std::uint16_t, 8> spans{1, 2, 3, 8, 32, 64, 65, 10'000};
    const auto placement = [&] {
        GridPlacement value;
        if (random() % 3) value.start = GridLine::index(lines[random() % lines.size()]);
        value.end = GridLine::span(spans[random() % spans.size()]);
        return value;
    };
    for (std::size_t trial = 0; trial < 100; ++trial) {
        std::vector<Placement> input(1 + random() % 60);
        for (auto& item : input) {
            const auto row = placement(), column = placement();
            item.row = resolve_grid_placement(row, axis);
            item.column = resolve_grid_placement(column, axis);
            item.row_estimate = estimate_grid_axis(row, 16);
            item.column_estimate = estimate_grid_axis(column, 16);
            item.hidden = random() % 11 == 0;
        }
        for (const auto flow : {GridAutoFlow::Row, GridAutoFlow::Column,
                GridAutoFlow::RowDense, GridAutoFlow::ColumnDense}) {
            auto linear = input;
            auto indexed = input;
            // Hidden padding changes only the storage strategy. The compact
            // rectangle scan is an independent oracle for the interval index.
            indexed.resize(64);
            for (std::size_t i = input.size(); i < indexed.size(); ++i) indexed[i].hidden = true;
            const auto expected = place_grid_items(linear, 16, 16, flow, {1});
            const auto actual = place_grid_items(indexed, 16, 16, flow, {1});
            if (extent(expected) != extent(actual)) throw std::runtime_error("Indexed Grid extent differs");
            for (std::size_t i = 0; i < input.size(); ++i) {
                if (rectangle(linear[i]) != rectangle(indexed[i])) throw std::runtime_error("Indexed placement differs");
            }
        }
    }
}
} // namespace
int main() {
    try { for (auto seed : {1U, 831109U, 1234567U, 4294967291U}) differential(seed); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
