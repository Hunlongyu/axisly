#include <axisly/axisly.hpp>

#include <iostream>
#include <vector>

#include "console_session.hpp"

int main() {
    const axisly::examples::ConsoleSession console_session;
    using namespace axisly;

    GridSpec spec;
    spec.columns = {Track::fixed(140), Track::fixed(220), Track::fixed(100)};
    spec.rows = {Track::fixed(80), Track::fixed(120)};
    spec.column_gap = 8;
    spec.row_gap = 8;
    spec.padding = {12, 12, 12, 12};

    const std::vector<GridItem> items = {
        {.id = 1, .column = 0, .row = 0},
        {.id = 2, .column = 1, .row = 0},
        {.id = 3, .column = 2, .row = 0},
        {.id = 4, .column = 0, .row = 1},
        {.id = 5, .column = 1, .row = 1},
        {.id = 6, .column = 2, .row = 1}
    };

    const auto result = compute_grid(spec, items, {640, 360});
    if (!result.succeeded()) {
        std::cerr << result.error.message << '\n';
        return 1;
    }

    std::cout << "axisly M1: occupied size " << result.size.width << " x " << result.size.height << '\n';
    for (const auto& item : result.items) {
        std::cout << "item " << item.id << ": x=" << item.rect.x << " y=" << item.rect.y
                  << " width=" << item.rect.width << " height=" << item.rect.height << '\n';
    }
}
