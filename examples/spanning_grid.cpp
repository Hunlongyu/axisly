#include <gridly/gridly.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>

#include "console_session.hpp"

namespace {

// Synthetic text and sidebar dimensions; the host would supply real metrics.
gridly::MeasureResult measure(gridly::ItemId id, const gridly::MeasureInput& input, void*) {
    if (id == 2) {
        return {{40, 80}, {60, 100}};
    }
    const float natural_width = id == 1 ? 400.0f : 320.0f;
    const float line_height = id == 1 ? 20.0f : 24.0f;
    const float minimum_width = id == 1 ? 160.0f : 80.0f;
    const float lines = input.known_width && *input.known_width > 0
        ? std::max(1.0f, std::ceil(natural_width / *input.known_width)) : 1.0f;
    return {{minimum_width, line_height * lines}, {natural_width, line_height * lines}};
}

} // namespace

int main() {
    const gridly::examples::ConsoleSession console_session;
    using namespace gridly;
    const GridSpec spec{
        .columns = {Track::fixed(80), Track::weighted(), Track::weighted(2)},
        .rows = {Track::content(), Track::content(), Track::fixed(30)},
        .column_gap = 8, .row_gap = 6, .padding = {12, 12, 12, 12}
    };
    const std::array items{
        GridItem{.id = 1, .column_span = 3, .margin = {8, 4, 8, 4}},
        GridItem{.id = 2, .row = 1, .row_span = 2, .horizontal_alignment = HorizontalAlignment::Center},
        GridItem{.id = 3, .column = 1, .row = 1, .column_span = 2, .margin = {6, 4, 6, 4}}
    };
    std::cout << "gridly M5: column spans, row spans and text remeasurement\n";
    for (const Unit width : {500.0f, 260.0f}) {
        const auto result = compute_grid(spec, items, {width, 300}, measure);
        if (!result.succeeded()) {
            std::cerr << result.error.message << '\n';
            return 1;
        }
        std::cout << "container width=" << width << ", occupied=" << result.size.width << " x " << result.size.height
                  << ", measurement calls=" << result.measurement_count << '\n';
        for (const auto& item : result.items) {
            const auto& cell = item.cell_rect;
            const auto& rect = item.rect;
            std::cout << "  item " << item.id << " span={" << cell.x << ", " << cell.y << ", " << cell.width << ", " << cell.height
                      << "}, rect={" << rect.x << ", " << rect.y << ", " << rect.width << ", " << rect.height << "}\n";
        }
    }
}
