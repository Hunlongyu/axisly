#include <algorithm>
#include <cmath>
#include <iostream>

#include "console_session.hpp"
#include "nested_grid_adapter.hpp"

int main() {
    const axisly::examples::ConsoleSession console_session;
    using namespace axisly;
    using namespace axisly::examples;

    const NestedGrid article{
        .spec = {.columns = {Track::weighted()}, .rows = {Track::content()}, .padding = {8, 8, 8, 8}},
        .items = {{.id = 1}},
        .measure = +[](ItemId, const MeasureInput& input, void*) -> MeasureResult {
            const Unit lines = input.known_width
                ? std::ceil(600.0f / std::max(1.0f, *input.known_width)) : 1.0f;
            return {{0, 20 * lines}, {600, 20 * lines}};
        }
    };
    const NestedGrid panel{
        .spec = {.columns = {Track::weighted()}, .rows = {Track::fixed(28), Track::content()},
                 .row_gap = 6, .padding = {6, 6, 6, 6}},
        .items = {{.id = 1}, {.id = 2, .row = 1}},
        .children = {{2, &article}}
    };
    const NestedGrid root{
        .spec = {.columns = {Track::fixed(100), Track::weighted()}, .rows = {Track::content()},
                 .column_gap = 12, .padding = {10, 10, 10, 10}},
        .items = {{.id = 1, .preferred_size = {100, 24}}, {.id = 2, .column = 1}},
        .children = {{2, &panel}}
    };
    // All pointers refer to host-owned, stable local objects that outlive every
    // synchronous calculation below.
    std::cout << "axisly M7: nested host adapter, scoped IDs and recursive measurement\n";
    for (const Unit width : {640.0f, 360.0f, 220.0f}) {
        const auto result = compute_nested_grid(root, {width, 400});
        if (!result.succeeded()) {
            std::cerr << result.error.message << '\n';
            return 1;
        }
        std::cout << "container width=" << width << ", occupied=" << result.size.width << " x " << result.size.height
                  << ", layouts=" << result.layout_count << ", measurements=" << result.measurement_count << '\n';
        // A real host resolves each path to a widget here, after the ENTIRE run succeeds.
        for (const auto& item : result.items) {
            std::cout << "  item";
            for (const auto id : item.path) std::cout << '/' << id;
            std::cout << " rect={" << item.rect.x << ", " << item.rect.y << ", "
                      << item.rect.width << ", " << item.rect.height << "}\n";
        }
    }
}
