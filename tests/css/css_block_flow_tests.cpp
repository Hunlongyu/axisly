#include <axisly/css_tree.hpp>

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
using namespace axisly;
using namespace axisly::css;
const AvailableSize available{AvailableSpace::max_content(), AvailableSpace::max_content()};

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

Style block(Unit width = 0, Unit height = 0) {
    Style style;
    style.display = Display::Block;
    if (width) style.box.size.width = Length::length(width);
    if (height) style.box.size.height = Length::length(height);
    return style;
}

void minimum_and_collapsed_margins() {
    LayoutTree tree;
    const auto root = tree.create_node(block(50));
    auto style = block();
    style.box.min_size.height = Length::length(30);
    const auto parent = tree.create_node(style);
    style = block();
    style.box.margin.bottom = Length::length(20);
    const auto empty = tree.create_node(style);
    const auto next = tree.create_node(block(0, 10));
    tree.append_child(root, parent);
    tree.append_child(parent, empty);
    tree.append_child(root, next);
    const auto& constrained = tree.compute(root, available);
    require(constrained.succeeded()
        && constrained.find(parent)->border_box == Rect{0, 20, 50, 30}
        && constrained.find(empty)->border_box.y == 20
        && constrained.find(next)->border_box.y == 50,
        "A minimum height commits the parent without moving its empty child out of the collapsed top strut");

    style = block();
    style.box.min_size.height = Length::length(10);
    style.box.margin.bottom = Length::length(10);
    tree.set_style(parent, style);
    style = block(0, 20);
    style.box.margin.bottom = Length::length(30);
    tree.set_style(empty, style);
    const auto& natural = tree.compute(root, available);
    require(natural.succeeded() && natural.find(parent)->border_box.height == 20
        && natural.find(parent)->margin.bottom == 30
        && natural.find(next)->border_box.y == 50
        && natural.find(root)->border_box.height == 60,
        "A minimum smaller than the natural height must not commit the last child's bottom margin");
}

void floating_margin_struts() {
    LayoutTree tree;
    const auto root = tree.create_node(block(300));
    const auto parent = tree.create_node(block());
    auto style = block(100, 100);
    style.float_side = FloatSide::Left;
    const auto floating = tree.create_node(style);
    style = block();
    style.clear = ClearSide::Left;
    style.box.margin.top = Length::length(40);
    style.box.margin.bottom = Length::length(140);
    const auto empty = tree.create_node(style);
    const auto next = tree.create_node(block(0, 100));
    tree.append_child(root, parent);
    tree.append_child(parent, floating);
    tree.append_child(parent, empty);
    tree.append_child(root, next);
    const auto& result = tree.compute(root, available);
    require(result.succeeded() && result.find(empty)->border_box.y == 100
        && result.find(parent)->border_box.height == 200
        && result.find(parent)->margin.bottom == 0
        && result.find(next)->border_box.y == 200,
        "Clearance through an empty block preserves its collapsed margins inside the parent's height");

    LayoutTree adjoining;
    const auto outer = adjoining.create_node(block(100));
    const auto flow = adjoining.create_node(block());
    const auto holder = adjoining.create_node(block());
    style = block(100, 50);
    style.float_side = FloatSide::Left;
    const auto held_float = adjoining.create_node(style);
    style = block(0, 50);
    style.clear = ClearSide::Left;
    style.box.margin.top = Length::length(400);
    const auto cleared = adjoining.create_node(style);
    adjoining.append_child(outer, flow);
    adjoining.append_child(flow, holder);
    adjoining.append_child(holder, held_float);
    adjoining.append_child(flow, cleared);
    const auto& forced = adjoining.compute(outer, available);
    require(forced.succeeded() && forced.find(outer)->border_box.height == 100
        && forced.find(cleared)->border_box.y == 50,
        "A float adjoining the leading margin strut must be cleared through same-context descendants");
}

void formatting_context_slots() {
    for (const auto direction : {Direction::Ltr, Direction::Rtl}) {
        LayoutTree tree;
        auto style = block(100);
        style.box.direction = direction;
        const auto root = tree.create_node(style);
        style = block(50, 100);
        style.float_side = direction == Direction::Ltr ? FloatSide::Left : FloatSide::Right;
        const auto floating = tree.create_node(style);
        style = block(0, 50);
        style.overflow_x = style.overflow_y = Overflow::Hidden;
        if (direction == Direction::Ltr) style.box.margin.left = Length::length(-60);
        else style.box.margin.right = Length::length(-60);
        const auto context = tree.create_node(style);
        tree.append_child(root, floating);
        tree.append_child(root, context);
        const auto& negative = tree.compute(root, available);
        require(negative.succeeded() && negative.find(context)->border_box.width == 160
            && negative.find(context)->border_box.y == 100
            && negative.find(root)->border_box.height == 150,
            "The minimum automatic width keeps a nonnegative margin box and moves an oversized context below floats");
        style.box.margin = {};
        style.box.size.width = Length::length(50);
        if (direction == Direction::Ltr) style.box.margin.right = Length::length(10);
        else style.box.margin.left = Length::length(10);
        tree.set_style(context, style);
        const auto& fixed = tree.compute(root, available);
        require(fixed.succeeded() && fixed.find(context)->border_box.y == 0
            && fixed.find(context)->border_box.x == (direction == Direction::Ltr ? 50 : 0)
            && fixed.find(root)->border_box.height == 100,
            "A positive trailing margin may overflow without disqualifying a fixed border box from a float slot");
    }
}

void percentage_item_stages() {
    LayoutTree tree;
    const auto root = tree.create_node(block(40));
    auto style = block(100);
    style.box.padding = {Length::percent(0.1f), Length::percent(0.1f),
        Length::percent(0.1f), Length::percent(0.1f)};
    const auto parent = tree.create_node(style);
    const auto child = tree.create_node(block(0, 10));
    tree.append_child(root, parent);
    tree.append_child(parent, child);
    const auto& result = tree.compute(root, available);
    require(result.succeeded() && result.find(parent)->border_box.height == 18
        && result.find(child)->border_box == Rect{4, 4, 92, 10},
        "Child flow resolves percentage padding against its containing inline width");
    require(result.find(parent)->padding == Insets{4, 0, 4, 0},
        "Taffy 0.14 compatibility reports the early Block item padding with an indefinite vertical basis");

    LayoutTree intrinsic;
    const auto content_root = intrinsic.create_node(block());
    style = block(100, 10);
    style.box.margin.left = Length::percent(0.1f);
    const auto item = intrinsic.create_node(style);
    intrinsic.append_child(content_root, item);
    const auto& content = intrinsic.compute(content_root, available);
    require(content.succeeded() && content.find(content_root)->border_box.width == 100
        && content.find(item)->border_box.x == 10,
        "Cyclic margins contribute zero to intrinsic width and resolve after the used inline width is chosen");
}
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected test case name");
        const std::string_view name = argv[1];
        if (name == "minimum_and_collapsed_margins") minimum_and_collapsed_margins();
        else if (name == "floating_margin_struts") floating_margin_struts();
        else if (name == "formatting_context_slots") formatting_context_slots();
        else if (name == "percentage_item_stages") percentage_item_stages();
        else throw std::runtime_error("Unknown test case");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
