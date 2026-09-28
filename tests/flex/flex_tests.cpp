#include <axisly/flex.hpp>

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <vector>

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
void check(const FlexResult& result, std::size_t index, Rect expected) {
    require(result.succeeded(), "Flex result failed");
    require(index < result.items.size(), "Missing flex item");
    if (!is(result.items[index].rect, expected)) {
        std::cerr << "Item " << index << " actual {" << result.items[index].rect.x << ", "
            << result.items[index].rect.y << ", " << result.items[index].rect.width
            << ", " << result.items[index].rect.height << "} expected {"
            << expected.x << ", " << expected.y << ", " << expected.width << ", "
            << expected.height << "}\n";
        throw std::runtime_error("Incorrect flex rectangle");
    }
}
FlexItem sized(ItemId id, Unit width, Unit height) {
    FlexItem item;
    item.id = id;
    item.preferred_size = {width, height};
    return item;
}
void empty() {
    const auto result = compute_flex({}, {}, {100, 50});
    require(result.succeeded() && result.items.empty() && result.lines.empty()
        && is(result.occupied_bounds, {0, 0, 100, 50}), "Empty flex container");
}
void row_column() {
    const std::array items{sized(1, 30, 10), sized(2, 20, 15)};
    auto result = compute_flex({}, items, {100, 60});
    check(result, 0, {0, 0, 30, 60});
    check(result, 1, {30, 0, 20, 60});
    FlexSpec spec;
    spec.direction = FlexDirection::Column;
    result = compute_flex(spec, items, {100, 60});
    check(result, 0, {0, 0, 100, 10});
    check(result, 1, {0, 10, 100, 15});
}
void box_model() {
    FlexSpec spec;
    spec.padding = {10, 20, 30, 40};
    spec.border = {1, 2, 3, 4};
    const auto result = compute_flex(spec, std::array{sized(1, 30, 10)}, {200, 160});
    check(result, 0, {11, 22, 30, 94});
    require(is(result.lines[0].rect, {11, 22, 156, 94}), "Flex content box line");
}
void grow_shrink() {
    std::array items{FlexItem{.id = 1, .grow = 1, .basis = FlexLength::fixed(0)},
                     FlexItem{.id = 2, .grow = 2, .basis = FlexLength::fixed(0)}};
    auto result = compute_flex({}, items, {120, 20});
    check(result, 0, {0, 0, 40, 20});
    check(result, 1, {40, 0, 80, 20});
    items = {sized(1, 80, 10), sized(2, 80, 10)};
    result = compute_flex({}, items, {100, 20});
    check(result, 0, {0, 0, 50, 20});
    check(result, 1, {50, 0, 50, 20});
}
void flex_freeze_and_partial_factors() {
    auto first = sized(1, 0, 10), second = sized(2, 0, 10);
    first.grow = second.grow = 1;
    first.basis = second.basis = FlexLength::fixed(0);
    first.maximum_width = FlexLength::fixed(20);
    auto result = compute_flex({}, std::array{first, second}, {100, 20});
    check(result, 0, {0, 0, 20, 20});
    check(result, 1, {20, 0, 80, 20});
    first.maximum_width = {};
    first.grow = second.grow = 0.25f;
    first.basis = second.basis = FlexLength::fixed(20);
    result = compute_flex({}, std::array{first, second}, {100, 20});
    check(result, 0, {0, 0, 35, 20});
    check(result, 1, {35, 0, 35, 20});
}
void automatic_minimum() {
    auto first = sized(1, 80, 10), second = sized(2, 80, 10);
    first.min_content.width = second.min_content.width = 70;
    std::array items{first, second};
    auto result = compute_flex({}, items, {100, 20});
    check(result, 0, {0, 0, 70, 20});
    check(result, 1, {70, 0, 70, 20});
    require(near(result.occupied_bounds.width, 140), "Automatic minimum preserves overflow");
    items[0].overflow = items[1].overflow = FlexOverflow::Hidden;
    result = compute_flex({}, items, {100, 20});
    check(result, 0, {0, 0, 50, 20});
    check(result, 1, {50, 0, 50, 20});
}
void replaced_automatic_minimum() {
    auto item = sized(1, 80, 10);
    item.min_content.width = 80;
    item.aspect_ratio = 2.0f;
    item.height = FlexLength::fixed(10);
    item.basis = FlexLength::fixed(100);
    auto result = compute_flex({}, std::array{item}, {50, 30});
    check(result, 0, {0, 0, 80, 10});
    item.replaced = true;
    result = compute_flex({}, std::array{item}, {50, 30});
    check(result, 0, {0, 0, 50, 10});
    item.overflow = FlexOverflow::Clip;
    result = compute_flex({}, std::array{item}, {50, 30});
    check(result, 0, {0, 0, 50, 10});
    item.overflow = FlexOverflow::Hidden;
    result = compute_flex({}, std::array{item}, {50, 30});
    check(result, 0, {0, 0, 50, 10});
}
void wrap() {
    FlexSpec spec;
    spec.wrap = FlexWrap::Wrap;
    std::array items{sized(1, 40, 10), sized(2, 40, 10), sized(3, 40, 10)};
    auto result = compute_flex(spec, items, {100, 50});
    check(result, 0, {0, 0, 40, 25});
    check(result, 1, {40, 0, 40, 25});
    check(result, 2, {0, 25, 40, 25});
    require(result.lines.size() == 2, "Wrap creates two lines");
    spec.wrap = FlexWrap::WrapReverse;
    result = compute_flex(spec, items, {100, 50});
    check(result, 0, {0, 25, 40, 25});
    check(result, 2, {0, 0, 40, 25});
}
void reverse_and_writing() {
    const std::array items{sized(1, 30, 10), sized(2, 20, 10)};
    FlexSpec spec;
    spec.direction = FlexDirection::RowReverse;
    auto result = compute_flex(spec, items, {100, 30});
    check(result, 0, {70, 0, 30, 30});
    check(result, 1, {50, 0, 20, 30});
    spec.direction = FlexDirection::Row;
    spec.text_direction = TextDirection::Rtl;
    result = compute_flex(spec, items, {100, 30});
    check(result, 0, {70, 0, 30, 30});
    spec.direction = FlexDirection::RowReverse;
    result = compute_flex(spec, items, {100, 30});
    check(result, 0, {0, 0, 30, 30});
    spec = {};
    spec.writing_mode = WritingMode::VerticalRl;
    result = compute_flex(spec, items, {100, 80});
    check(result, 0, {0, 0, 100, 10});
    check(result, 1, {0, 10, 100, 10});
}
void alignment() {
    FlexSpec spec;
    spec.justify_content.value = FlexAlign::Center;
    spec.align_items.value = FlexAlign::Center;
    auto result = compute_flex(spec, std::array{sized(1, 20, 10)}, {100, 50});
    check(result, 0, {40, 20, 20, 10});
    spec.justify_content.value = FlexAlign::SpaceBetween;
    const std::array items{sized(1, 20, 10), sized(2, 20, 10), sized(3, 20, 10)};
    result = compute_flex(spec, items, {100, 50});
    check(result, 0, {0, 20, 20, 10});
    check(result, 1, {40, 20, 20, 10});
    check(result, 2, {80, 20, 20, 10});
}
void baseline_alignment() {
    FlexSpec spec;
    spec.align_items.value = FlexAlign::FirstBaseline;
    auto first = sized(1, 20, 20), second = sized(2, 20, 30);
    first.baseline = 10.0f;
    second.baseline = 5.0f;
    const auto result = compute_flex(spec, std::array{first, second}, {100, 50});
    check(result, 0, {0, 0, 20, 20});
    check(result, 1, {20, 5, 20, 30});
}
void logical_and_safe_alignment() {
    FlexSpec spec;
    spec.direction = FlexDirection::RowReverse;
    spec.justify_content.value = FlexAlign::Start;
    auto result = compute_flex(spec, std::array{sized(1, 30, 10)}, {100, 20});
    check(result, 0, {0, 0, 30, 20});
    spec.justify_content.value = FlexAlign::End;
    result = compute_flex(spec, std::array{sized(1, 30, 10)}, {100, 20});
    check(result, 0, {70, 0, 30, 20});
    spec.direction = FlexDirection::Row;
    spec.justify_content = {FlexAlign::Center, true};
    auto oversized = sized(1, 120, 10);
    oversized.shrink = 0;
    result = compute_flex(spec, std::array{oversized}, {100, 20});
    check(result, 0, {0, 0, 120, 20});
    spec.justify_content = {FlexAlign::SpaceEvenly, false};
    result = compute_flex(spec, std::array{oversized}, {100, 20});
    check(result, 0, {-10, 0, 120, 20});
}
void margins_and_gap() {
    auto first = sized(1, 20, 10);
    first.margin.left = FlexLength{};
    FlexSpec spec;
    spec.column_gap = 5;
    auto result = compute_flex(spec, std::array{first}, {100, 30});
    check(result, 0, {80, 0, 20, 30});
    first.margin.left = FlexLength::fixed(5);
    result = compute_flex(spec, std::array{first, sized(2, 20, 10)}, {100, 30});
    check(result, 0, {5, 0, 20, 30});
    check(result, 1, {30, 0, 20, 30});
}
void basis_and_order() {
    auto first = sized(1, 10, 10), second = sized(2, 30, 10), third = sized(3, 20, 10);
    first.basis = FlexLength::percent(0.5f);
    first.order = 2;
    second.order = -1;
    third.order = 2;
    const auto result = compute_flex({}, std::array{first, second, third}, {120, 20});
    check(result, 1, {0, 0, 30, 20});
    check(result, 0, {30, 0, 60, 20});
    check(result, 2, {90, 0, 20, 20});
    require(result.paint_order == std::vector<ItemId>{2, 1, 3}, "Order sort is stable");
}
void absolute_and_collapse() {
    auto absolute = sized(1, 20, 10);
    absolute.position = FlexPosition::Absolute;
    absolute.width = FlexLength::fixed(20);
    absolute.height = FlexLength::fixed(10);
    absolute.inset.left = FlexLength::fixed(10);
    absolute.inset.top = FlexLength::fixed(5);
    auto collapsed = sized(3, 40, 30);
    collapsed.visibility = FlexVisibility::Collapse;
    auto result = compute_flex({}, std::array{absolute, sized(2, 30, 10), collapsed}, {100, 50});
    check(result, 0, {10, 5, 20, 10});
    require(is(result.items[0].static_position_rect, {0, 0, 20, 50}),
        "Static position rectangle uses content cross edges");
    check(result, 1, {0, 0, 30, 50});
    require(result.items[0].absolute && result.items[2].collapsed
        && result.items[2].rect.width == 0, "Absolute and collapsed item metadata");
}
void absolute_stretch() {
    auto item = sized(1, 20, 10);
    item.position = FlexPosition::Absolute;
    item.inset.left = FlexLength::fixed(10);
    item.inset.right = FlexLength::fixed(20);
    item.inset.top = FlexLength::fixed(5);
    item.inset.bottom = FlexLength::fixed(5);
    const auto result = compute_flex({}, std::array{item}, {100, 50});
    check(result, 0, {10, 5, 70, 40});
}
void collapsed_strut_rewrap() {
    FlexSpec spec;
    spec.wrap = FlexWrap::Wrap;
    spec.align_content.value = FlexAlign::FlexStart;
    auto first = sized(1, 80, 30);
    first.visibility = FlexVisibility::Collapse;
    const auto result = compute_flex(spec,
        std::array{first, sized(2, 40, 10), sized(3, 40, 10), sized(4, 40, 10)}, {100, 80});
    require(result.succeeded() && result.lines.size() == 2, "Collapsed item triggers line recollection");
    check(result, 1, {0, 0, 40, 30});
    check(result, 2, {40, 0, 40, 30});
    check(result, 3, {0, 30, 40, 10});
    require(result.items[0].collapsed && result.items[0].rect.width == 0,
        "Collapsed item is represented by a strut");
}
void aspect_and_measurement() {
    auto item = sized(1, 30, 10);
    item.basis = FlexLength::fixed(40);
    item.aspect_ratio = 2.0f;
    auto result = compute_flex({}, std::array{item}, {100, 50});
    check(result, 0, {0, 0, 40, 50}); // Stretch takes precedence over aspect-ratio cross size.
    item.align_self.value = FlexAlign::Start;
    result = compute_flex({}, std::array{item}, {100, 50});
    check(result, 0, {0, 0, 40, 20});
    item.aspect_ratio.reset();
    std::size_t calls = 0;
    result = compute_flex({}, std::array{item}, {100, 50},
        [&calls](ItemId, const FlexMeasureInput& input) {
            ++calls;
            if (!input.known_content_width) return FlexMeasureResult{{10, 5}, {40, 20}, {40, 20}, {}, {}};
            require(*input.known_content_width == 40, "Known final flex width");
            return FlexMeasureResult{{10, 5}, {40, 30}, {40, 30}, {}, {}};
        });
    check(result, 0, {0, 0, 40, 30});
    require(calls == 2 && result.measurement_count == 2, "Intrinsic and constrained probes");
}
void invalid_and_overflow() {
    auto item = sized(1, 20, 10);
    auto result = compute_flex({}, std::array{item, item}, {100, 30});
    require(result.error.code == FlexErrorCode::DuplicateItemId && result.items.empty()
        && result.size == Size{}, "Duplicate IDs fail atomically");
    item.grow = std::numeric_limits<Unit>::infinity();
    result = compute_flex({}, std::array{item}, {100, 30});
    require(result.error.code == FlexErrorCode::InvalidItemStyle, "Infinite growth rejected");
    item.grow = 0;
    result = compute_flex({}, std::array{item}, {100, 30},
        [](ItemId, const FlexMeasureInput&) {
            return FlexMeasureResult{{20, 10}, {10, 10}, {20, 10}, {}, {}};
        });
    require(result.error.code == FlexErrorCode::InvalidMeasurement
        && result.items.empty(), "Invalid measurement fails atomically");
}
void run(std::string_view name) {
    if (name == "empty") empty();
    else if (name == "row_column") row_column();
    else if (name == "box_model") box_model();
    else if (name == "grow_shrink") grow_shrink();
    else if (name == "flex_freeze_and_partial_factors") flex_freeze_and_partial_factors();
    else if (name == "automatic_minimum") automatic_minimum();
    else if (name == "replaced_automatic_minimum") replaced_automatic_minimum();
    else if (name == "wrap") wrap();
    else if (name == "reverse_and_writing") reverse_and_writing();
    else if (name == "alignment") alignment();
    else if (name == "baseline_alignment") baseline_alignment();
    else if (name == "logical_and_safe_alignment") logical_and_safe_alignment();
    else if (name == "margins_and_gap") margins_and_gap();
    else if (name == "basis_and_order") basis_and_order();
    else if (name == "absolute_and_collapse") absolute_and_collapse();
    else if (name == "absolute_stretch") absolute_stretch();
    else if (name == "collapsed_strut_rewrap") collapsed_strut_rewrap();
    else if (name == "aspect_and_measurement") aspect_and_measurement();
    else if (name == "invalid_and_overflow") invalid_and_overflow();
    else throw std::runtime_error("Unknown test case");
}
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    try {
        run(argv[1]);
    } catch (const std::exception& error) {
        std::cerr << argv[1] << ": " << error.what() << '\n';
        return 1;
    }
}
