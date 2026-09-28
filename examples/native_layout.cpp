#include <axisly/axisly.hpp>

#include <array>
#include <iostream>

int main() {
    using namespace axisly;
    GridSpec frame;
    frame.columns = {Track::fixed(320)};
    frame.rows = {Track::fixed(48), Track::fixed(192)};
    const std::array panels{
        GridItem{.id = 1, .row = 0},
        GridItem{.id = 2, .row = 1}
    };
    const auto grid = compute_grid(frame, panels, {320, 240});
    if (!grid.succeeded()) return 1;

    FlexSpec toolbar;
    toolbar.align_items.value = FlexAlign::Center;
    toolbar.justify_content.value = FlexAlign::SpaceBetween;
    const std::array actions{
        FlexItem{.id = 11, .preferred_size = {72, 28}},
        FlexItem{.id = 12, .preferred_size = {96, 28}}
    };
    const auto flex = compute_flex(toolbar, actions,
        {grid.items[0].rect.width, grid.items[0].rect.height});
    if (!flex.succeeded()) return 2;

    OverlayItem badge;
    badge.id = 21;
    badge.anchors.right = AnchorLength::fixed(12);
    badge.anchors.top = AnchorLength::fixed(12);
    badge.width = 64.0f;
    badge.height = 24.0f;
    const auto overlay = compute_overlay({}, std::array{badge},
        {grid.items[1].rect.width, grid.items[1].rect.height});
    if (!overlay.succeeded()) return 3;

    // Results are local to each container. A widget host adds the parent's
    // origin when applying the child rectangles.
    std::cout << "toolbar action: " << flex.items[1].rect.x << ","
        << flex.items[1].rect.y << "\n";
    std::cout << "badge: " << overlay.items[0].rect.x << ","
        << grid.items[1].rect.y + overlay.items[0].rect.y << "\n";
}
