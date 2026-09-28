#include <axisly/axisly.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>

#include "console_session.hpp"

namespace {

// Synthetic host metrics, independent of fonts or a UI framework.
axisly::MeasureResult measure(axisly::ItemId id, const axisly::MeasureInput& input, void*) {
    if (id == 1) return {{60, 24}, {240, 24}};
    const float lines = input.known_width && *input.known_width > 0
        ? std::max(1.0f, std::ceil(400.0f / *input.known_width)) : 1.0f;
    return {{100, 20 * lines}, {400, 20 * lines}};
}

} // namespace

int main() {
    const axisly::examples::ConsoleSession console_session;
    using namespace axisly;
    const GridSpec spec{
        .columns = {Track::range(Track::fixed(80), Track::fixed(160)),
                    Track::range(Track::content(), Track::weighted())},
        .rows = {Track::range(Track::fixed(30), Track::content())},
        .column_gap = 12, .padding = {12, 12, 12, 12}
    };
    const std::array items{
        GridItem{.id = 1, .vertical_alignment = VerticalAlignment::Center},
        GridItem{.id = 2, .column = 1, .vertical_alignment = VerticalAlignment::Start}
    };
    std::cout << "axisly M6: bounded Content and content-minimum Weighted ranges\n";
    for (const Unit width : {600.0f, 300.0f, 180.0f}) {
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
            std::cout << "  item " << item.id << " cell={" << cell.x << ", " << cell.y << ", " << cell.width << ", " << cell.height
                      << "}, rect={" << rect.x << ", " << rect.y << ", " << rect.width << ", " << rect.height << "}\n";
        }
    }
}
