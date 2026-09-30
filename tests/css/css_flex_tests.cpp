#include <axisly/css_tree.hpp>

#include <cmath>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
using namespace axisly;
using namespace axisly::css;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void row_grow() {
    LayoutTree tree;
    Style container;
    container.box.size = {Length::length(300), Length::length(100)};
    container.gap.width = Length::length(10);
    container.box.padding.left = Length::length(10);
    container.box.padding.right = Length::length(10);
    const auto root = tree.create_node(container);
    Style first_style;
    first_style.box.size = {Length::length(50), Length::length(20)};
    const auto first = tree.create_node(first_style);
    Style second_style;
    second_style.box.size.height = Length::length(20);
    second_style.flex_grow = 1;
    const auto second = tree.create_node(second_style,
        [](const MeasureRequest&) { return Measurement{{30, 20}, {}, {}}; });
    tree.append_child(root, first);
    tree.append_child(root, second);
    const auto& result = tree.compute(root,
        {AvailableSpace::definite(300), AvailableSpace::definite(100)});
    require(result.succeeded() && result.size == Size{300, 100}, "Flex root size");
    require(result.find(root)->content_box == Rect{10, 0, 280, 100}, "Flex content box");
    require(result.find(first)->border_box == Rect{10, 0, 50, 20}, "Fixed item position");
    require(result.find(second)->border_box == Rect{70, 0, 220, 20}, "Grow item takes remaining space");
}

void shrink_and_reverse() {
    LayoutTree tree;
    Style container;
    container.box.size = {Length::length(100), Length::length(100)};
    container.flex_direction = FlexDirection::RowReverse;
    const auto root = tree.create_node(container);
    Style child_style;
    child_style.box.size = {Length::length(80), Length::length(20)};
    child_style.box.min_size.width = Length::length(0);
    const auto a = tree.create_node(child_style);
    const auto b = tree.create_node(child_style);
    tree.append_child(root, a);
    tree.append_child(root, b);
    const auto& result = tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::definite(100)});
    require(result.succeeded(), "Reverse shrink must succeed");
    require(result.find(a)->border_box == Rect{50, 0, 50, 20}, "First reverse item is on the right");
    require(result.find(b)->border_box == Rect{0, 0, 50, 20}, "Second reverse item is on the left");
}

void wrap_and_cross_alignment() {
    LayoutTree tree;
    Style container;
    container.box.size = {Length::length(100), Length::length(80)};
    container.flex_wrap = FlexWrap::Wrap;
    container.gap = {Length::length(5), Length::length(5)};
    const auto root = tree.create_node(container);
    Style child;
    child.box.size = {Length::length(40), Length::length(10)};
    const auto a = tree.create_node(child);
    const auto b = tree.create_node(child);
    const auto c = tree.create_node(child);
    tree.append_child(root, a);
    tree.append_child(root, b);
    tree.append_child(root, c);
    const auto& result = tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::definite(80)});
    require(result.succeeded(), "Wrapped Flex must succeed");
    require(result.find(a)->border_box == Rect{0, 0, 40, 10}, "First wrapped item");
    require(result.find(b)->border_box == Rect{45, 0, 40, 10}, "Second wrapped item");
    const auto third = result.find(c)->border_box;
    require(third.x == 0 && std::abs(third.y - 42.5f) < 0.01f
        && third.width == 40 && third.height == 10,
        "Third item starts on stretched second line");
}

void automatic_minimum() {
    LayoutTree tree;
    Style container;
    container.box.size = {Length::length(100), Length::length(20)};
    const auto root = tree.create_node(container);
    Style item_style;
    item_style.flex_shrink = 1;
    const auto measure = [](const MeasureRequest& request) {
        const auto width = request.available_width.kind == AvailableKind::MinContent ? 60.0f : 80.0f;
        return Measurement{{width, 10}, {}, {}};
    };
    const auto first = tree.create_node(item_style, measure);
    const auto second = tree.create_node(item_style, measure);
    tree.append_child(root, first);
    tree.append_child(root, second);
    const auto& result = tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::definite(20)});
    require(result.succeeded(), "Automatic minimum query must succeed");
    require(result.find(first)->border_box.width == 60
        && result.find(second)->border_box.width == 60,
        "Visible Flex items must retain their min-content automatic minimum");
    item_style.overflow_x = Overflow::Hidden;
    tree.set_style(first, item_style);
    tree.set_style(second, item_style);
    const auto& scrollable = tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::definite(20)});
    require(scrollable.succeeded()
        && scrollable.find(first)->border_box.width == 50
        && scrollable.find(second)->border_box.width == 50,
        "Scroll-container Flex items must allow a zero automatic minimum");
}

void balanced_lines() {
    LayoutTree tree;
    Style container;
    container.box.size = {Length::length(100), Length::length(40)};
    container.flex_wrap = FlexWrap::Balance;
    container.flex_line_count = 2;
    const auto root = tree.create_node(container);
    Style child;
    child.box.size = {Length::length(30), Length::length(10)};
    const auto a = tree.create_node(child);
    const auto b = tree.create_node(child);
    const auto c = tree.create_node(child);
    const auto d = tree.create_node(child);
    for (const auto id : {a, b, c, d}) tree.append_child(root, id);
    const auto& result = tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::definite(40)});
    require(result.succeeded(), "Balanced Flex layout must succeed");
    require(result.find(a)->border_box.y == 0 && result.find(b)->border_box.y == 0
        && result.find(c)->border_box.y == 20 && result.find(d)->border_box.y == 20,
        "Balance must choose two equal lines instead of greedy three-and-one");
}

void balanced_zero_item() {
    LayoutTree tree;
    Style container;
    container.box.size = {Length::length(100), Length::length(40)};
    container.flex_wrap = FlexWrap::Balance;
    container.gap.width = Length::length(10);
    const auto root = tree.create_node(container);
    Style child;
    child.box.size = {Length::length(70), Length::length(10)};
    const auto a = tree.create_node(child);
    child.box.size.width = Length::length(0);
    const auto zero = tree.create_node(child);
    child.box.size.width = Length::length(20);
    const auto b = tree.create_node(child);
    for (const auto id : {a, zero, b}) tree.append_child(root, id);
    const auto& result = tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::definite(40)});
    require(result.succeeded(), "Balanced zero item layout must succeed");
    require(result.find(zero)->border_box.x == 80 && result.find(zero)->border_box.y == 0
        && result.find(b)->border_box.x == 0 && result.find(b)->border_box.y == 20,
        "Zero-width item must remain at the end of its preceding line");
}

void nested_flex_basis() {
    LayoutTree tree;
    Style root_style;
    root_style.box.size = {Length::length(300), Length::length(100)};
    const auto root = tree.create_node(root_style);
    const auto nested = tree.create_node();
    Style item;
    item.box.size = {Length::length(40), Length::length(10)};
    const auto first = tree.create_node(item);
    const auto second = tree.create_node(item);
    tree.append_child(nested, first);
    tree.append_child(nested, second);
    item.box.size.width = Length::length(50);
    const auto sibling = tree.create_node(item);
    tree.append_child(root, nested);
    tree.append_child(root, sibling);
    const auto& result = tree.compute(root,
        {AvailableSpace::definite(300), AvailableSpace::definite(100)});
    require(result.succeeded() && result.find(nested)->border_box.width == 80
        && result.find(sibling)->border_box.x == 80,
        "Nested Flex auto basis must use intrinsic content width");
}

// WPT css/css-flexbox/layout-algorithm_algo-cross-line-001.html.
void wpt_scroll_cross_size() {
    LayoutTree tree;
    Style container;
    container.box.size.width = Length::length(200);
    const auto root = tree.create_node(container);
    Style item;
    item.box.size.height = Length::length(200);
    item.flex_grow = 1;
    item.flex_shrink = 1;
    item.flex_basis = Length::length(0);
    item.overflow_x = Overflow::Scroll;
    item.overflow_y = Overflow::Scroll;
    const auto first = tree.create_node(item);
    const auto second = tree.create_node(item);
    tree.append_child(root, first);
    tree.append_child(root, second);
    const auto& result = tree.compute(root,
        {AvailableSpace::definite(200), AvailableSpace::max_content()});
    require(result.succeeded() && result.find(root)->border_box == Rect{0, 0, 200, 200}
        && result.find(first)->border_box == Rect{0, 0, 100, 200}
        && result.find(second)->border_box == Rect{100, 0, 100, 200},
        "Scrolling Flex items must retain the 200px cross size");
}

// WPT css/css-flexbox/flex-002.htm: scaled shrink factors are 600:200.
void wpt_scaled_flex_shrink() {
    LayoutTree tree;
    Style container;
    container.box.size.width = Length::length(300);
    const auto root = tree.create_node(container);
    Style first_style;
    first_style.box.size = {Length::length(300), Length::length(50)};
    first_style.flex_shrink = 2;
    Style second_style;
    second_style.box.size = {Length::length(200), Length::length(50)};
    const auto first = tree.create_node(first_style);
    const auto second = tree.create_node(second_style);
    tree.append_child(root, first);
    tree.append_child(root, second);
    const auto& result = tree.compute(root,
        {AvailableSpace::definite(300), AvailableSpace::max_content()});
    require(result.succeeded() && result.find(first)->border_box == Rect{0, 0, 150, 50}
        && result.find(second)->border_box == Rect{150, 0, 150, 50},
        "Negative free space must be split by flex-shrink times flex base size");
}
// WPT css/css-flexbox/align-content-wrap-001.html, horizontal writing.
void wpt_single_line_wrap() {
    const std::pair<ContentPosition, Unit> cases[]{
        {ContentPosition::FlexStart, 0.0f}, {ContentPosition::FlexEnd, 50.0f},
        {ContentPosition::Center, 25.0f}, {ContentPosition::SpaceBetween, 0.0f},
        {ContentPosition::SpaceAround, 25.0f}, {ContentPosition::SpaceEvenly, 25.0f},
        {ContentPosition::Stretch, 0.0f}};
    for (const auto [alignment, offset] : cases) {
        LayoutTree tree;
        Style style;
        style.flex_wrap = FlexWrap::Wrap;
        style.align_content = {alignment};
        style.box.size = {Length::length(100), Length::length(70)};
        const auto root = tree.create_node(style);
        style = {};
        style.box.size = {Length::length(20), Length::length(20)};
        const auto child = tree.create_node(style);
        tree.append_child(root, child);
        const auto& result = tree.compute(root,
            {AvailableSpace::definite(100), AvailableSpace::definite(70)});
        require(result.succeeded() && result.find(child)->border_box == Rect{0, offset, 20, 20},
            "A wrap container with one occupied line must still apply align-content");
    }
}

void small_factors_gap() {
    for (const bool grow : {false, true}) {
        LayoutTree tree;
        Style style;
        style.box.size = {Length::length(grow ? 200.0f : 100.0f), Length::length(20)};
        style.gap.width = Length::length(10);
        const auto root = tree.create_node(style);
        style = {};
        style.flex_basis = Length::length(grow ? 40.0f : 80.0f);
        style.box.min_size.width = Length::length(0);
        style.flex_grow = grow ? .25f : 0;
        style.flex_shrink = grow ? 0 : .25f;
        const auto first = tree.create_node(style);
        const auto second = tree.create_node(style);
        tree.append_child(root, first);
        tree.append_child(root, second);
        const auto& result = tree.compute(root,
            {AvailableSpace::max_content(), AvailableSpace::max_content()});
        // Taffy 0.14 scales initial free space, then subtracts the 10px gap
        // again: grow widths 62.5, shrink widths 57.5. Browser/spec values
        // are separately asserted in tests/spec/flex_small_factors_gap.html.
        const Unit width = grow ? 62.5f : 57.5f;
        require(result.succeeded() && result.find(first)->border_box.width == width
            && result.find(second)->border_box.width == width
            && result.find(second)->border_box.x == width + 10,
            "Low flex factors and gaps must retain the documented Taffy version behavior");
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected test case name");
        const std::string_view name = argv[1];
        if (name == "row_grow") row_grow();
        else if (name == "shrink_and_reverse") shrink_and_reverse();
        else if (name == "wrap_and_cross_alignment") wrap_and_cross_alignment();
        else if (name == "automatic_minimum") automatic_minimum();
        else if (name == "balanced_lines") balanced_lines();
        else if (name == "balanced_zero_item") balanced_zero_item();
        else if (name == "nested_flex_basis") nested_flex_basis();
        else if (name == "wpt_scroll_cross_size") wpt_scroll_cross_size();
        else if (name == "wpt_scaled_flex_shrink") wpt_scaled_flex_shrink();
        else if (name == "wpt_single_line_wrap") wpt_single_line_wrap();
        else if (name == "small_factors_gap") small_factors_gap();
        else throw std::runtime_error("Unknown test case");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
