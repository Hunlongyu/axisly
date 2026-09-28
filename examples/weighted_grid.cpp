#include <gridly/gridly.hpp>

#include <iostream>
#include <vector>

#include "console_session.hpp"

int main() {
    const gridly::examples::ConsoleSession console_session;
    using namespace gridly;
    const GridSpec spec{
        .columns = {Track::fixed(180), Track::weighted(1), Track::weighted(2)},
        .rows = {Track::fixed(80), Track::weighted()},
        .column_gap = 8,
        .row_gap = 8,
        .padding = {12, 12, 12, 12}
    };
    const std::vector<GridItem> items{
        {.id = 1}, {.id = 2, .column = 1}, {.id = 3, .column = 2},
        {.id = 4, .row = 1}, {.id = 5, .column = 1, .row = 1}, {.id = 6, .column = 2, .row = 1}
    };

    for (const Unit width : {640.0f, 940.0f, 190.0f}) {
        const auto result = compute_grid(spec, items, {width, 360});
        if (!result.succeeded()) {
            std::cerr << result.error.message << '\n';
            return 1;
        }
        std::cout << "gridly M2: available " << width << " x 360, occupied "
                  << result.size.width << " x " << result.size.height << '\n';
        for (const auto& item : result.items) {
            std::cout << "item " << item.id << ": x=" << item.rect.x << " y=" << item.rect.y
                      << " width=" << item.rect.width << " height=" << item.rect.height << '\n';
        }
    }
}
