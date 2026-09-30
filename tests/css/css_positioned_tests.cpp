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

void ratio_and_content() {
    for (const auto display : {Display::Block, Display::Flex, Display::Grid}) {
        LayoutTree tree;
        Style parent;
        parent.display = display;
        parent.box.size = {Length::length(400), Length::length(300)};
        const auto root = tree.create_node(parent);
        Style item;
        item.position = Position::Absolute;
        item.box.aspect_ratio = 3.0f;
        item.inset = {Length::length(20), Length::length(15),
            Length::length(20), Length::length(15)};
        std::optional<MeasureRequest> latest;
        const auto child = tree.create_node(item, [&](const MeasureRequest& request) {
            latest = request;
            return Measurement{{80, 90}, {}, {}, Size{80, 90}};
        });
        tree.append_child(root, child);
        const AvailableSize available{AvailableSpace::max_content(), AvailableSpace::max_content()};
        const auto& filled = tree.compute(root, available);
        require(filled.succeeded() && filled.find(child)->border_box == Rect{20, 15, 360, 120},
            "Inset width must transfer through the ratio before choosing inset height");
        require(latest && latest->known_content_width == 360 && latest->known_content_height == 120,
            "Final positioned dimensions must reach the host measurement");

        item.inset = {};
        item.box.max_size.width = Length::length(50);
        tree.set_style(child, item);
        const auto& bounded = tree.compute(root, available);
        require(bounded.succeeded() && bounded.find(child)->border_box.width == 50
            && std::abs(bounded.find(child)->border_box.height - 50.0f / 3) < 0.001f,
            "A positioned max-width must transfer to max-height before the final layout");
        require(latest && latest->known_content_width == 50
            && latest->known_content_height && std::abs(*latest->known_content_height - 50.0f / 3) < 0.001f,
            "Content layout must receive both constrained axes");
        require(bounded.find(child)->scrollable_overflow_rect.height == 90,
            "Constraining the outer height must retain the independently measured content extent");
    }
}

void scrollbars_and_negative_margin() {
    for (const auto display : {Display::Block, Display::Flex, Display::Grid}) {
        LayoutTree tree;
        Style parent;
        parent.display = display;
        parent.box.size = {Length::length(50), Length::length(50)};
        parent.overflow_x = parent.overflow_y = Overflow::Scroll;
        parent.scrollbar_width = 15;
        const auto root = tree.create_node(parent);
        Style item;
        item.position = Position::Absolute;
        item.inset = {Length::length(0), Length::length(0), Length::length(0), Length::length(0)};
        const auto child = tree.create_node(item);
        tree.append_child(root, child);
        const auto& result = tree.compute(root, {AvailableSpace::max_content(), AvailableSpace::max_content()});
        require(result.succeeded() && result.find(child)->border_box == Rect{0, 0, 35, 35},
            "A positioned containing block must exclude both scrollbar gutters");
    }
    LayoutTree tree;
    Style parent;
    parent.display = Display::Block;
    parent.box.size = {Length::length(52), Length::length(52)};
    const auto root = tree.create_node(parent);
    Style item;
    item.position = Position::Absolute;
    item.box.size = {Length::length(72), Length::length(72)};
    item.box.margin.left = Length::automatic();
    item.inset.left = Length::length(10);
    item.inset.right = Length::length(20);
    const auto child = tree.create_node(item);
    tree.append_child(root, child);
    const auto& result = tree.compute(root, {AvailableSpace::max_content(), AvailableSpace::max_content()});
    require(result.succeeded() && result.find(child)->border_box.x == -40
        && result.find(child)->margin.left == -50,
        "A single automatic Block absolute margin may absorb negative space");
}

void grid_area_edges() {
    for (const auto direction : {Direction::Ltr, Direction::Rtl}) {
        LayoutTree tree;
        Style parent;
        parent.display = Display::Grid;
        parent.box.direction = direction;
        parent.box.size = {Length::length(240), Length::length(100)};
        parent.box.padding = {Length::length(15), Length::length(15),
            Length::length(15), Length::length(15)};
        parent.box.border = {Length::length(5), Length::length(5),
            Length::length(5), Length::length(5)};
        parent.gap = {Length::length(10), Length::length(10)};
        parent.grid.template_columns = {TrackSizing::fixed(40), TrackSizing::fixed(40)};
        parent.grid.template_rows = {TrackSizing::fixed(20), TrackSizing::fixed(20)};
        const auto root = tree.create_node(parent);
        Style item;
        item.position = Position::Absolute;
        item.inset = {Length::length(0), Length::length(0), Length::length(0), Length::length(0)};
        item.grid.column = {GridLine::index(2), GridLine::automatic()};
        item.grid.row = item.grid.column;
        const auto child = tree.create_node(item);
        tree.append_child(root, child);
        const AvailableSize available{AvailableSpace::max_content(), AvailableSpace::max_content()};
        const auto& start = tree.compute(root, available);
        require(start.succeeded() && start.find(child)->border_box
            == Rect{direction == Direction::Ltr ? 70.0f : 5.0f, 50, 165, 45},
            "A definite Grid start edge must retain the opposite padding-box boundary");

        item.grid.column = {GridLine::automatic(), GridLine::index(2)};
        item.grid.row = item.grid.column;
        tree.set_style(child, item);
        const auto& end = tree.compute(root, available);
        require(end.succeeded() && end.find(child)->border_box
            == Rect{direction == Direction::Ltr ? 5.0f : 180.0f, 5, 55, 35},
            "A definite Grid end edge must use the preceding track's end");

        item.grid.column = {GridLine::index(1), GridLine::index(9999)};
        item.grid.row = item.grid.column;
        tree.set_style(child, item);
        const auto& outside = tree.compute(root, available);
        require(outside.succeeded() && outside.find(child)->border_box
            == Rect{direction == Direction::Ltr ? 20.0f : 5.0f, 20, 215, 75}
            && outside.grid_tracks.front().columns.size() == 2
            && outside.grid_tracks.front().rows.size() == 2,
            "An absent absolute endpoint must fall back without creating implicit tracks");
    }
    LayoutTree tree;
    Style parent;
    parent.display = Display::Grid;
    parent.box.direction = Direction::Rtl;
    parent.box.size = {Length::length(530), Length::length(330)};
    parent.box.padding = {Length::length(15), Length::length(15),
        Length::length(15), Length::length(15)};
    parent.gap = {Length::length(10), Length::length(10)};
    parent.grid.template_columns = {GridRepeat{RepeatKind::AutoFit, 1, {TrackSizing::fixed(100)}, {}}};
    parent.grid.template_rows = parent.grid.template_columns;
    const auto root = tree.create_node(parent);
    Style item;
    item.position = Position::Absolute;
    item.grid.column = {GridLine::index(-1), GridLine::automatic()};
    item.grid.row = item.grid.column;
    const auto child = tree.create_node(item);
    tree.append_child(root, child);
    const auto& result = tree.compute(root, {AvailableSpace::max_content(), AvailableSpace::max_content()});
    require(result.succeeded() && result.find(child)->border_box == Rect{515, 15, 0, 0},
        "Collapsed auto-fit endpoints must keep the physical RTL grid line");
}
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected test case name");
        const std::string_view name = argv[1];
        if (name == "ratio_and_content") ratio_and_content();
        else if (name == "scrollbars_and_negative_margin") scrollbars_and_negative_margin();
        else if (name == "grid_area_edges") grid_area_edges();
        else throw std::runtime_error("Unknown test case");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
