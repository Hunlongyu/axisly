#include <axisly/css_tree.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
using namespace axisly;
using namespace axisly::css;
const AvailableSize available{AvailableSpace::max_content(), AvailableSpace::max_content()};
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

void item_size_stages() {
    LayoutTree tree;
    Style parent;
    parent.flex_direction = FlexDirection::Column;
    parent.box.size = {Length::length(200), Length::length(400)};
    const auto root = tree.create_node(parent);
    Style a;
    a.flex_grow = 1;
    a.box.min_size.height = Length::percent(0.6f);
    Style b;
    b.flex_grow = 2;
    b.box.min_size.height = Length::percent(0.1f);
    const auto first = tree.create_node(a);
    const auto second = tree.create_node(b);
    tree.append_child(root, first);
    tree.append_child(root, second);
    const auto& result = tree.compute(root, available);
    require(result.succeeded() && std::abs(result.find(first)->border_box.height - 240) < 0.001f
        && std::abs(result.find(second)->border_box.height - 160) < 0.001f,
        "Percentage minimum sizes must clamp flexible targets after content bases are resolved");

    LayoutTree ratios;
    parent.box.size = {Length::length(100), Length::length(100)};
    parent.align_items = ItemAlignment{ItemPosition::Start};
    const auto ratio_root = ratios.create_node(parent);
    Style item;
    item.box.aspect_ratio = 2.0f;
    item.box.max_size.width = Length::length(40);
    const auto ratio_child = ratios.create_node(item, [](const MeasureRequest& request) {
        return Measurement{{request.known_content_width.value_or(220),
            request.known_content_height.value_or(60)}, {}, {}, Size{220, 60}};
    });
    ratios.append_child(ratio_root, ratio_child);
    const auto& ratio_result = ratios.compute(ratio_root, available);
    require(ratio_result.succeeded() && ratio_result.find(ratio_child)->border_box == Rect{0, 0, 40, 20}
        && ratio_result.find(ratio_child)->scrollable_overflow_rect.height == 60,
        "Ratio-transferred limits must bound the automatic minimum without discarding natural overflow");

    LayoutTree stretch;
    parent.flex_direction = FlexDirection::Row;
    parent.box.size = {Length::length(120), Length::length(60)};
    const auto stretch_root = stretch.create_node(parent);
    item = {};
    item.box.size = {Length::length(40), Length::stretch()};
    item.box.margin.top = Length::length(5);
    item.box.margin.bottom = Length::length(10);
    const auto stretch_child = stretch.create_node(item,
        [](const MeasureRequest&) { return Measurement{{40, 10}}; });
    stretch.append_child(stretch_root, stretch_child);
    const auto& stretch_result = stretch.compute(stretch_root, available);
    require(stretch_result.succeeded() && stretch_result.find(stretch_child)->border_box == Rect{0, 5, 40, 45},
        "A cross stretch sizing keyword fills the line independently of align-items");

    LayoutTree percentages;
    parent = {};
    parent.flex_direction = FlexDirection::Column;
    parent.box.size.width = Length::length(200);
    parent.box.min_size.height = Length::length(200);
    const auto percent_root = percentages.create_node(parent);
    item = {};
    item.flex_direction = FlexDirection::Column;
    item.flex_grow = 1;
    const auto middle = percentages.create_node(item);
    item = {};
    item.display = Display::Block;
    item.box.size.height = Length::percent(1);
    const auto inner = percentages.create_node(item);
    percentages.append_child(percent_root, middle);
    percentages.append_child(middle, inner);
    const auto& percent_result = percentages.compute(percent_root, available);
    require(percent_result.succeeded() && percent_result.find(middle)->border_box.height == 200
        && percent_result.find(inner)->border_box.height == 0,
        "A content-derived Flex main size must remain indefinite for descendant percentage sizes");
}

void baseline_auto_margins() {
    LayoutTree tree;
    Style parent;
    parent.box.size.width = Length::length(100);
    parent.align_items = ItemAlignment{ItemPosition::Baseline};
    const auto root = tree.create_node(parent);
    const auto make = [&](Unit height, Unit baseline, bool automatic) {
        Style item;
        item.box.size = {Length::length(20), Length::length(height)};
        if (automatic) item.box.margin.top = Length::automatic();
        const auto child = tree.create_node(item, [=](const MeasureRequest&) {
            return Measurement{{20, height}, baseline, baseline};
        });
        tree.append_child(root, child);
        return child;
    };
    const auto a = make(20, 5, false);
    const auto b = make(30, 25, false);
    const auto c = make(60, 60, true);
    const auto& result = tree.compute(root, available);
    require(result.succeeded() && result.find(root)->border_box.height == 60
        && result.find(a)->border_box.y == 20 && result.find(b)->border_box.y == 0
        && result.find(c)->border_box.y == 0,
        "Automatic cross margins exclude an item from the baseline sharing group");
}

void balanced_measurement_budget() {
    LayoutTree tree;
    Style parent;
    parent.flex_direction = FlexDirection::Column;
    parent.flex_wrap = FlexWrap::Balance;
    parent.flex_line_count = 2;
    parent.box.size = {Length::length(210), Length::length(100)};
    parent.gap.width = Length::length(10);
    parent.align_items = ItemAlignment{ItemPosition::Start};
    parent.align_content = ContentAlignment{ContentPosition::Start};
    const auto root = tree.create_node(parent);
    const auto first = tree.create_node({}, [](const MeasureRequest& request) {
        const Unit width = request.known_content_width.value_or(
            request.available_width.kind == AvailableKind::MinContent ? 100.0f
            : request.available_width.kind == AvailableKind::Definite
                ? std::clamp(request.available_width.value, 100.0f, 300.0f) : 300.0f);
        return Measurement{{width, request.known_content_height.value_or(
            std::ceil(300.0f / width) * 10)}};
    });
    Style item;
    item.box.size = {Length::length(30), Length::length(60)};
    const auto second = tree.create_node(item);
    tree.append_child(root, first);
    tree.append_child(root, second);
    const auto& result = tree.compute(root, available);
    require(result.succeeded() && result.find(first)->border_box == Rect{0, 0, 100, 30}
        && result.find(second)->border_box == Rect{110, 0, 30, 60},
        "Requested Flex lines share cross-axis measurement space after subtracting their gap");

    LayoutTree negative;
    parent = {};
    parent.flex_wrap = FlexWrap::Wrap;
    const auto negative_root = negative.create_node(parent);
    item = {};
    item.box.size = {Length::length(100), Length::length(10)};
    const auto wide = negative.create_node(item);
    item.box.size.width = Length::length(0);
    item.box.margin.left = Length::length(-10);
    const auto zero = negative.create_node(item);
    negative.append_child(negative_root, wide);
    negative.append_child(negative_root, zero);
    const auto& negative_result = negative.compute(negative_root, available);
    require(negative_result.succeeded() && negative_result.find(negative_root)->border_box.width == 100,
        "A zero scaled shrink weight must retain its base in intrinsic flex fraction sizing");
}
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected test case name");
        const std::string_view name = argv[1];
        if (name == "item_size_stages") item_size_stages();
        else if (name == "baseline_auto_margins") baseline_auto_margins();
        else if (name == "balanced_measurement_budget") balanced_measurement_budget();
        else throw std::runtime_error("Unknown test case");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
