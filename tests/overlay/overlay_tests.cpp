#include <axisly/overlay.hpp>

#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace axisly;

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
bool near(Unit a, Unit b) { return std::isfinite(a) && std::abs(a - b) <= 0.0001f; }
bool is(Rect actual, Rect expected) {
    return near(actual.x, expected.x) && near(actual.y, expected.y)
        && near(actual.width, expected.width) && near(actual.height, expected.height);
}

void anchors_and_order() {
    const OverlaySpec spec{.padding = {10, 20, 30, 40}, .border = {1, 2, 3, 4}};
    const std::array items{
        OverlayItem{.id = 8, .anchors = {.left = AnchorLength::fixed(5), .top = AnchorLength::percent(0.5f)},
            .preferred_size = {30, 20}},
        OverlayItem{.id = 3, .anchors = {.left = AnchorLength::fixed(5), .top = AnchorLength::fixed(6),
            .right = AnchorLength::fixed(7), .bottom = AnchorLength::fixed(8)}, .margin = {2, 3, 4, 5}},
    };
    const auto result = compute_overlay(spec, items, {200, 160});
    require(result.succeeded() && result.items.size() == 2, "Overlay should compute two ordered items");
    require(result.items[0].id == 8 && result.items[1].id == 3, "Input order must be retained");
    require(is(result.items[0].rect, {16, 69, 30, 20}), "Mixed fixed/percentage anchor placement");
    require(is(result.items[1].rect, {18, 31, 138, 72}), "Opposing anchors stretch within margins");
    require(is(result.items[1].anchor_rect, {16, 28, 144, 80}), "Anchor rectangle is reported separately");
    require(is(result.occupied_bounds, {0, 0, 200, 160}), "Container bounds are included");
}

void explicit_size_and_alignment() {
    OverlayItem item{.id = 4, .anchors = {.right = AnchorLength::fixed(10), .bottom = AnchorLength::fixed(5)},
        .margin = {1, 2, 3, 4}, .preferred_size = {30, 20}};
    auto result = compute_overlay({}, std::array{item}, {100, 80});
    require(result.succeeded() && is(result.items[0].rect, {57, 51, 30, 20}), "Trailing anchor uses end alignment");
    item.anchors.left = AnchorLength::fixed(10);
    item.anchors.top = AnchorLength::fixed(5);
    item.width = 20.0f;
    item.height = 10.0f;
    result = compute_overlay({}, std::array{item}, {100, 80});
    require(is(result.items[0].rect, {11, 7, 20, 10}), "Fixed size wins over opposing anchors");
    item.horizontal_alignment = OverlayAlign::Center;
    item.vertical_alignment = OverlayAlign::End;
    result = compute_overlay({}, std::array{item}, {100, 80});
    require(is(result.items[0].rect, {39, 61, 20, 10}), "Explicit cross-anchor alignment");
}

void measured_reflow() {
    OverlayItem item{.id = 10, .anchors = {.left = AnchorLength::fixed(5)},
        .preferred_size = {40, 10}};
    std::size_t calls = 0;
    const auto result = compute_overlay({}, std::array{item}, {100, 80},
        [&calls](ItemId id, const MeasureInput& input) {
            require(id == 10, "Measurement item ID");
            ++calls;
            if (!input.known_width) return MeasureResult{{10, 5}, {50, 10}, {}};
            require(*input.known_width == 50, "Second measurement receives resolved width");
            return MeasureResult{{10, 5}, {50, 25}, {}};
        });
    require(result.succeeded() && calls == 2 && result.measurement_count == 2, "Two measurement probes");
    require(is(result.items[0].rect, {5, 0, 50, 25}), "Height follows constrained width");
}

void validation_and_overflow() {
    OverlayItem item{.id = 1};
    const std::array duplicates{item, item};
    auto result = compute_overlay({}, duplicates, {100, 100});
    require(result.error.code == OverlayErrorCode::DuplicateItemId && result.items.empty()
        && result.size == Size{}, "Validation is atomic");
    item.anchors.left = AnchorLength::percent(1e30f);
    result = compute_overlay({}, std::array{item}, {1e30f, 100});
    require(result.error.code == OverlayErrorCode::NumericOverflow && result.items.empty(),
        "Anchor multiplication overflow is rejected");
    item.anchors = {};
    item.maximum_width = 4.0f;
    result = compute_overlay({}, std::array{item}, {100, 100},
        [](ItemId, const MeasureInput&) { return MeasureResult{{5, 0}, {5, 0}, {}}; });
    require(result.error.code == OverlayErrorCode::InvalidMeasurement && result.items.empty(),
        "Measured minimum must fit the declared maximum");
}
}

int main() {
    try {
        anchors_and_order();
        explicit_size_and_alignment();
        measured_reflow();
        validation_and_overflow();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
