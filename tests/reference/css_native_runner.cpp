#include <axisly/css_tree.hpp>

#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using namespace axisly;
using namespace axisly::css;

using IdentifiedNode = std::pair<int, NodeId>;

void print(const char* name, const LayoutResult& result,
           const std::vector<IdentifiedNode>& nodes) {
    if (!result.succeeded()) throw std::runtime_error(result.error.message);
    std::cout << std::setprecision(9) << "{\"name\":\"" << name << "\",\"nodes\":[";
    bool first = true;
    for (const auto& [id, handle] : nodes) {
        const auto* layout = result.find(handle);
        if (!layout) throw std::runtime_error("CSS node missing from result");
        if (!first) std::cout << ',';
        first = false;
        const auto& rect = layout->border_box;
        std::cout << "{\"id\":" << id << ",\"rect\":[" << rect.x << ',' << rect.y
            << ',' << rect.width << ',' << rect.height << "]}";
    }
    std::cout << "]}\n";
}

void row_grow() {
    LayoutTree tree;
    Style root_style;
    root_style.box.size = {Length::length(300), Length::length(100)};
    root_style.gap.width = Length::length(10);
    root_style.box.padding.left = Length::length(10);
    root_style.box.padding.right = Length::length(10);
    const auto root = tree.create_node(root_style);
    Style a_style;
    a_style.box.size = {Length::length(50), Length::length(20)};
    const auto a = tree.create_node(a_style,
        [](const MeasureRequest&) { return Measurement{{50, 20}, {}, {}}; });
    Style b_style;
    b_style.box.size.height = Length::length(20);
    b_style.flex_grow = 1;
    const auto b = tree.create_node(b_style,
        [](const MeasureRequest&) { return Measurement{{30, 20}, {}, {}}; });
    tree.append_child(root, a);
    tree.append_child(root, b);
    print("flex-row-grow", tree.compute(root,
        {AvailableSpace::definite(300), AvailableSpace::definite(100)}),
        {{1, root}, {2, a}, {3, b}});
}

void negative_margin() {
    LayoutTree tree;
    Style root_style;
    root_style.box.size = {Length::length(180), Length::length(80)};
    const auto root = tree.create_node(root_style);
    Style a_style;
    a_style.box.box_sizing = BoxSizing::ContentBox;
    a_style.box.size = {Length::length(40), Length::length(20)};
    a_style.box.padding.left = Length::length(5);
    a_style.box.padding.right = Length::length(5);
    a_style.box.padding.top = Length::length(2);
    a_style.box.padding.bottom = Length::length(2);
    a_style.box.border.left = Length::length(1);
    a_style.box.border.right = Length::length(1);
    a_style.box.margin.left = Length::length(-3);
    const auto a = tree.create_node(a_style);
    Style b_style;
    b_style.box.size = {Length::length(20), Length::length(10)};
    const auto b = tree.create_node(b_style);
    tree.append_child(root, a);
    tree.append_child(root, b);
    print("content-box-and-negative-margin", tree.compute(root,
        {AvailableSpace::definite(180), AvailableSpace::definite(80)}),
        {{40, root}, {41, a}, {42, b}});
}

void balance() {
    LayoutTree tree;
    Style root_style;
    root_style.box.size = {Length::length(100), Length::length(80)};
    root_style.flex_wrap = FlexWrap::Balance;
    root_style.flex_line_count = 2;
    root_style.gap = {Length::length(5), Length::length(5)};
    const auto root = tree.create_node(root_style);
    Style item_style;
    item_style.box.size = {Length::length(40), Length::length(10)};
    const auto a = tree.create_node(item_style);
    const auto b = tree.create_node(item_style);
    const auto c = tree.create_node(item_style);
    tree.append_child(root, a);
    tree.append_child(root, b);
    tree.append_child(root, c);
    print("flex-balance-lines", tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::definite(80)}),
        {{50, root}, {51, a}, {52, b}, {53, c}});
}

void grid_tracks() {
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Grid;
    root_style.box.size = {Length::length(200), Length::length(100)};
    root_style.gap.width = Length::length(5);
    root_style.grid.template_columns = {
        TrackSizing::fixed(50),
        TrackSizing::minmax(TrackBreadth::length(20), TrackBreadth::fraction(1)),
    };
    root_style.grid.column_line_names = {{"left"}, {"mid"}, {"right"}};
    root_style.grid.template_rows = {TrackSizing::fixed(30), TrackSizing::fixed(40)};
    const auto root = tree.create_node(root_style);
    Style a_style;
    a_style.box.size = {Length::length(20), Length::length(10)};
    a_style.grid.column = {GridLine::index(2), GridLine::span(1)};
    a_style.grid.row = {GridLine::index(1), GridLine::span(1)};
    const auto a = tree.create_node(a_style);
    Style b_style;
    b_style.box.size = {Length::length(10), Length::length(15)};
    b_style.grid.column = {GridLine::index(1), GridLine::span(1)};
    b_style.grid.row = {GridLine::index(2), GridLine::span(1)};
    const auto b = tree.create_node(b_style);
    tree.append_child(root, a);
    tree.append_child(root, b);
    print("css-grid-tracks-placement", tree.compute(root,
        {AvailableSpace::definite(200), AvailableSpace::definite(100)}),
        {{60, root}, {61, a}, {62, b}});
}

void named_area() {
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Grid;
    root_style.box.size = {Length::length(200), Length::length(100)};
    root_style.grid.template_columns = {TrackSizing::fixed(50), TrackSizing::fixed(150)};
    root_style.grid.template_rows = {TrackSizing::fixed(30), TrackSizing::fixed(70)};
    root_style.grid.column_line_names = {{"left"}, {"mid"}, {"right"}};
    root_style.grid.template_areas = std::vector<std::vector<std::string>>{
        {"header", "header"}, {"sidebar", "main"},
    };
    const auto root = tree.create_node(root_style);
    Style first_style;
    first_style.box.size = {Length::length(20), Length::length(10)};
    first_style.grid.column = {GridLine::named("mid"), GridLine::span(1)};
    first_style.grid.row = {GridLine::index(1), GridLine::span(1)};
    const auto first = tree.create_node(first_style);
    Style second_style;
    second_style.box.size = {Length::length(20), Length::length(10)};
    second_style.grid.column = {GridLine::named("main-start"), GridLine::named("main-end")};
    second_style.grid.row = {GridLine::named("main-start"), GridLine::named("main-end")};
    const auto second = tree.create_node(second_style);
    tree.append_child(root, first);
    tree.append_child(root, second);
    print("named-area-placement", tree.compute(root,
        {AvailableSpace::definite(200), AvailableSpace::definite(100)}),
        {{70, root}, {71, first}, {72, second}});
}

void grid_auto_repeat(bool fit) {
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Grid;
    root_style.box.size = {Length::length(100), Length::length(30)};
    root_style.gap.width = Length::length(5);
    root_style.grid.template_columns = {GridRepeat{
        fit ? RepeatKind::AutoFit : RepeatKind::AutoFill,
        1, {TrackSizing::fixed(30)}, {}}};
    root_style.grid.template_rows = {TrackSizing::fixed(20)};
    const auto root = tree.create_node(root_style);
    Style child_style;
    child_style.box.size = {Length::length(10), Length::length(10)};
    const auto child = tree.create_node(child_style);
    tree.append_child(root, child);
    print(fit ? "grid-auto-fit" : "grid-auto-fill", tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::definite(30)}),
        fit ? std::vector<IdentifiedNode>{{90, root}, {91, child}}
            : std::vector<IdentifiedNode>{{80, root}, {81, child}});
}

void hidden_and_positioned() {
    LayoutTree tree;
    Style root_style;
    root_style.box.size = {Length::length(200), Length::length(50)};
    const auto root = tree.create_node(root_style);
    Style hidden_style;
    hidden_style.display = Display::None;
    hidden_style.box.size = {Length::length(100), Length::length(20)};
    const auto hidden = tree.create_node(hidden_style);
    Style absolute_style;
    absolute_style.position = Position::Absolute;
    absolute_style.inset.left = Length::length(10);
    absolute_style.inset.top = Length::length(5);
    absolute_style.box.size = {Length::length(20), Length::length(10)};
    const auto absolute = tree.create_node(absolute_style);
    Style flow_style;
    flow_style.box.size = {Length::length(30), Length::length(10)};
    const auto flow = tree.create_node(flow_style);
    tree.append_child(root, hidden);
    tree.append_child(root, absolute);
    tree.append_child(root, flow);
    print("hidden-and-positioned", tree.compute(root,
        {AvailableSpace::definite(200), AvailableSpace::definite(50)}),
        {{30, root}, {31, hidden}, {32, absolute}, {33, flow}});
}

void block_float_clear() {
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Block;
    root_style.box.size = {Length::length(240), Length::length(120)};
    const auto root = tree.create_node(root_style);
    Style float_style;
    float_style.display = Display::Block;
    float_style.float_side = FloatSide::Left;
    float_style.box.size = {Length::length(40), Length::length(30)};
    const auto floating = tree.create_node(float_style);
    Style clear_style;
    clear_style.display = Display::Block;
    clear_style.clear = ClearSide::Both;
    clear_style.box.size = {Length::length(60), Length::length(20)};
    const auto cleared = tree.create_node(clear_style);
    tree.append_child(root, floating);
    tree.append_child(root, cleared);
    print("block-float-clear", tree.compute(root,
        {AvailableSpace::definite(240), AvailableSpace::definite(120)}),
        {{20, root}, {21, floating}, {22, cleared}});
}

void block_adjacent_margins() {
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Block;
    root_style.box.size = {Length::length(200), Length::length(100)};
    const auto root = tree.create_node(root_style);
    Style first_style;
    first_style.display = Display::Block;
    first_style.box.size.height = Length::length(10);
    first_style.box.margin.bottom = Length::length(15);
    const auto first = tree.create_node(first_style);
    Style second_style;
    second_style.display = Display::Block;
    second_style.box.size.height = Length::length(10);
    second_style.box.margin.top = Length::length(20);
    const auto second = tree.create_node(second_style);
    tree.append_child(root, first);
    tree.append_child(root, second);
    print("block-adjacent-margins", tree.compute(root,
        {AvailableSpace::definite(200), AvailableSpace::definite(100)}),
        {{100, root}, {101, first}, {102, second}});
}

void mixed_tree() {
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Block;
    root_style.box.size.width = Length::length(400);
    const auto root = tree.create_node(root_style);
    Style flex_style;
    flex_style.box.size = {Length::length(200), Length::length(100)};
    const auto flex = tree.create_node(flex_style);
    Style grid_style;
    grid_style.display = Display::Grid;
    grid_style.box.size = {Length::length(100), Length::length(50)};
    const auto grid = tree.create_node(grid_style);
    const auto leaf = tree.create_node({}, [](const MeasureRequest& request) {
        const auto width = request.known_content_width.value_or(30.0f);
        const auto height = request.known_content_height.value_or(15.0f);
        return Measurement{{width, height}, {}, {}};
    });
    Style other_style;
    other_style.display = Display::FlowRoot;
    other_style.box.size = {Length::length(80), Length::length(30)};
    const auto other = tree.create_node(other_style);
    tree.append_child(root, flex);
    tree.append_child(flex, grid);
    tree.append_child(grid, leaf);
    tree.append_child(root, other);
    print("mixed-block-flex-grid", tree.compute(root,
        {AvailableSpace::definite(400), AvailableSpace::max_content()}),
        {{10, root}, {11, flex}, {12, grid}, {13, leaf}, {14, other}});
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::invalid_argument("Expected fixture name");
        const std::string_view name = argv[1];
        if (name == "flex-row-grow") row_grow();
        else if (name == "content-box-and-negative-margin") negative_margin();
        else if (name == "flex-balance-lines") balance();
        else if (name == "css-grid-tracks-placement") grid_tracks();
        else if (name == "named-area-placement") named_area();
        else if (name == "grid-auto-fill") grid_auto_repeat(false);
        else if (name == "grid-auto-fit") grid_auto_repeat(true);
        else if (name == "hidden-and-positioned") hidden_and_positioned();
        else if (name == "block-float-clear") block_float_clear();
        else if (name == "block-adjacent-margins") block_adjacent_margins();
        else if (name == "mixed-block-flex-grid") mixed_tree();
        else throw std::invalid_argument("Unknown CSS fixture");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
