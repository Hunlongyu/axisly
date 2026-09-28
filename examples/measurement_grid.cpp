#include <axisly/axisly.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>

#include "console_session.hpp"

namespace {

// Synthetic text metrics for a self-contained example, without a font/UI library.
axisly::MeasureResult measure_text(axisly::ItemId, const axisly::MeasureInput& input, void*) {
    const float lines = input.known_width && *input.known_width > 0
        ? std::max(1.0f, std::ceil(400.0f / *input.known_width)) : 1.0f;
    const float height = 20.0f * lines;
    std::cout << "  measure width=";
    if (input.known_width) {
        std::cout << *input.known_width;
    } else {
        std::cout << "unknown";
    }
    std::cout << " -> preferred height=" << height << '\n';
    return {{40, height}, {400, height}};
}

} // namespace

int main() {
    const axisly::examples::ConsoleSession console_session;
    using namespace axisly;
    const GridSpec spec{
        .columns = {Track::content()}, .rows = {Track::content()}, .padding = {12, 12, 12, 12}
    };
    const std::array items{GridItem{.id = 1, .margin = {8, 4, 8, 4}}};
    std::cout << "axisly M4: Content tracks and width-dependent measurement\n";
    for (const Unit width : {440.0f, 190.0f, 120.0f}) {
        std::cout << "container width=" << width << '\n';
        const auto result = compute_grid(spec, items, {width, 240}, measure_text);
        if (!result.succeeded()) {
            std::cerr << result.error.message << '\n';
            return 1;
        }
        const auto& rect = result.items.front().rect;
        std::cout << "  item={" << rect.x << ", " << rect.y << ", " << rect.width << ", " << rect.height
                  << "}, occupied=" << result.size.width << " x " << result.size.height
                  << ", measurement calls=" << result.measurement_count << '\n';
    }
}
