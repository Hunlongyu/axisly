#include <gridly/gridly.hpp>

#include <iostream>
#include <vector>

#include "console_session.hpp"

int main() {
    const gridly::examples::ConsoleSession console_session;
    using namespace gridly;
    const GridSpec spec{
        .columns = {Track::fixed(160), Track::fixed(160)},
        .rows = {Track::fixed(100), Track::fixed(100)},
        .column_gap = 12, .row_gap = 12, .padding = {12, 12, 12, 12}
    };
    const Insets margin{10, 8, 14, 12};
    const Size preferred{60, 24};
    const std::vector<GridItem> items{
        {.id = 1, .margin = margin, .horizontal_alignment = HorizontalAlignment::Start,
         .vertical_alignment = VerticalAlignment::Start, .preferred_size = preferred},
        {.id = 2, .column = 1, .margin = margin, .horizontal_alignment = HorizontalAlignment::Center,
         .vertical_alignment = VerticalAlignment::Center, .preferred_size = preferred},
        {.id = 3, .row = 1, .margin = margin, .horizontal_alignment = HorizontalAlignment::End,
         .vertical_alignment = VerticalAlignment::End, .preferred_size = preferred},
        {.id = 4, .column = 1, .row = 1, .margin = margin, .preferred_size = preferred}
    };
    const auto result = compute_grid(spec, items, {400, 300});
    if (!result.succeeded()) {
        std::cerr << result.error.message << '\n';
        return 1;
    }
    std::cout << "gridly M3: Start / Center / End / Stretch\n"
              << "occupied size " << result.size.width << " x " << result.size.height << '\n';
    for (const auto& item : result.items) {
        std::cout << "item " << item.id << " cell={" << item.cell_rect.x << ", " << item.cell_rect.y
                  << ", " << item.cell_rect.width << ", " << item.cell_rect.height << "}"
                  << " rect={" << item.rect.x << ", " << item.rect.y
                  << ", " << item.rect.width << ", " << item.rect.height << "}\n";
    }
}
