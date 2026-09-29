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
        const auto& margin = layout->margin;
        const auto& border = layout->border;
        const auto& padding = layout->padding;
        const auto& overflow = layout->scrollable_overflow_rect;
        std::cout << "{\"id\":" << id << ",\"rect\":[" << rect.x << ',' << rect.y
            << ',' << rect.width << ',' << rect.height << "],\"margin\":["
            << margin.left << ',' << margin.top << ',' << margin.right << ','
            << margin.bottom << "],\"padding\":[" << padding.left << ','
            << padding.top << ',' << padding.right << ',' << padding.bottom
            << "],\"border\":[" << border.left << ',' << border.top << ','
            << border.right << ',' << border.bottom
            << "],\"scrollbar\":[" << layout->scrollbar.width
            << ',' << layout->scrollbar.height << "],\"overflow\":["
            << overflow.x << ',' << overflow.y << ','
            << overflow.x + overflow.width << ','
            << overflow.y + overflow.height << "]}";
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

void grid_content_alignment() {
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Grid;
    root_style.box.size = {Length::length(100), Length::length(80)};
    root_style.grid.template_columns = {TrackSizing::fixed(20), TrackSizing::fixed(20)};
    root_style.grid.template_rows = {TrackSizing::fixed(10), TrackSizing::fixed(10)};
    root_style.gap = {Length::length(10), Length::length(5)};
    root_style.justify_content = ContentAlignment{ContentPosition::SpaceBetween};
    root_style.align_content = ContentAlignment{ContentPosition::Center};
    const auto root = tree.create_node(root_style);
    Style child_style;
    child_style.box.size = {Length::length(10), Length::length(10)};
    child_style.grid.column = {GridLine::index(2), GridLine::span(1)};
    child_style.grid.row = {GridLine::index(2), GridLine::span(1)};
    const auto child = tree.create_node(child_style);
    tree.append_child(root, child);
    print("grid-content-alignment", tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::definite(80)}),
        {{110, root}, {111, child}});
}

void flex_intrinsic_basis() {
    LayoutTree tree;
    Style root_style;
    root_style.box.size = {Length::length(200), Length::length(40)};
    const auto root = tree.create_node(root_style);
    Style child_style;
    child_style.box.size = {Length::length(100), Length::length(10)};
    child_style.flex_basis = Length::content();
    const auto measure = [](const MeasureRequest& request) {
        const Unit width = request.available_width.kind == AvailableKind::MinContent
            ? 20.0f : 30.0f;
        return Measurement{{width, 10}, {}, {}};
    };
    const auto first = tree.create_node(child_style, measure);
    child_style.flex_basis = Length::min_content();
    const auto second = tree.create_node(child_style, measure);
    tree.append_child(root, first);
    tree.append_child(root, second);
    print("flex-intrinsic-basis", tree.compute(root,
        {AvailableSpace::definite(200), AvailableSpace::definite(40)}),
        {{120, root}, {121, first}, {122, second}});
}

void flex_baseline() {
    LayoutTree tree;
    Style root_style;
    root_style.box.size = {Length::length(200), Length::length(50)};
    root_style.align_items = ItemAlignment{ItemPosition::Baseline};
    const auto root = tree.create_node(root_style);
    Style first_style;
    first_style.box.size = {Length::length(30), Length::length(20)};
    const auto first = tree.create_node(first_style,
        [](const MeasureRequest&) {
            return Measurement{{30, 20}, 5.0f, {}};
        });
    Style second_style;
    second_style.box.size = {Length::length(40), Length::length(20)};
    const auto second = tree.create_node(second_style,
        [](const MeasureRequest&) {
            return Measurement{{40, 20}, 15.0f, {}};
        });
    tree.append_child(root, first);
    tree.append_child(root, second);
    print("flex-baseline", tree.compute(root,
        {AvailableSpace::definite(200), AvailableSpace::definite(50)}),
        {{130, root}, {131, first}, {132, second}});
}

void grid_baseline() {
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Grid;
    root_style.box.size = {Length::length(100), Length::length(40)};
    root_style.grid.template_columns = {TrackSizing::fixed(50), TrackSizing::fixed(50)};
    root_style.grid.template_rows = {TrackSizing::fixed(30)};
    root_style.align_items = ItemAlignment{ItemPosition::Baseline};
    const auto root = tree.create_node(root_style);
    Style child_style;
    child_style.box.size = {Length::length(20), Length::length(20)};
    const auto first = tree.create_node(child_style,
        [](const MeasureRequest&) {
            return Measurement{{20, 20}, 5.0f, {}};
        });
    const auto second = tree.create_node(child_style,
        [](const MeasureRequest&) {
            return Measurement{{20, 20}, 15.0f, {}};
        });
    tree.append_child(root, first);
    tree.append_child(root, second);
    print("grid-baseline", tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::definite(40)}),
        {{140, root}, {141, first}, {142, second}});
}

void nested_container_baseline(std::string_view name) {
    LayoutTree tree;
    const bool block = name.starts_with("nested-block-");
    const bool grid = name.starts_with("nested-grid-");
    const int kind = block ? 0 : grid ? 2 : 1;
    const bool layout_contain = name.find("-layout-contain-") != std::string_view::npos;
    const bool paint_contain = name.find("-paint-contain-") != std::string_view::npos;
    const int variant = layout_contain ? 1 : paint_contain ? 2 : 0;
    const int base = 5000 + kind * 30 + variant * 10;

    Style root_style;
    root_style.box.size = {Length::length(100), Length::length(50)};
    root_style.align_items = ItemAlignment{ItemPosition::Baseline};
    const auto root = tree.create_node(root_style);
    Style inner_style;
    inner_style.display = block ? Display::Block : grid ? Display::Grid : Display::Flex;
    inner_style.box.size = {Length::length(30), Length::length(20)};
    inner_style.contain = layout_contain ? Contain::Layout
        : paint_contain ? Contain::Paint : Contain::None;
    const auto inner = tree.create_node(inner_style);
    Style inner_leaf_style;
    inner_leaf_style.box.size = {Length::length(10), Length::length(10)};
    const auto inner_leaf = tree.create_node(inner_leaf_style,
        [](const MeasureRequest&) { return Measurement{{10, 10}, 3.0f, {}}; });
    Style outer_leaf_style;
    outer_leaf_style.box.size = {Length::length(20), Length::length(20)};
    const auto outer_leaf = tree.create_node(outer_leaf_style,
        [](const MeasureRequest&) { return Measurement{{20, 20}, 15.0f, {}}; });
    tree.append_child(root, inner);
    tree.append_child(inner, inner_leaf);
    tree.append_child(root, outer_leaf);
    print(name.data(), tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::definite(50)}),
        {{base, root}, {base + 1, inner}, {base + 2, inner_leaf},
         {base + 3, outer_leaf}});
}

void container_baseline_edge(std::string_view name) {
    LayoutTree tree;
    Style outer_style;
    outer_style.box.size = {Length::length(100), Length::length(50)};
    outer_style.align_items = ItemAlignment{ItemPosition::Baseline};
    const auto outer = tree.create_node(outer_style);
    Style inner_style;
    inner_style.display = name.starts_with("block-") ? Display::Block
        : name.starts_with("grid-") ? Display::Grid : Display::Flex;
    const bool second = name == "block-second-child-baseline";
    const bool scroll = name == "block-scroll-child-baseline";
    const bool wrap = name == "flex-wrap-reverse-baseline";
    const bool column = name == "flex-column-reverse-baseline";
    const bool grid_rows = name == "grid-first-row-baseline";
    const bool grid_aligned = name == "grid-aligned-baseline";
    inner_style.box.size = {Length::length(wrap || column || grid_rows ? 20.0f : 30.0f),
                            Length::length(wrap || column || grid_rows ? 30.0f : 20.0f)};
    if (wrap) inner_style.flex_wrap = FlexWrap::WrapReverse;
    if (column) inner_style.flex_direction = FlexDirection::ColumnReverse;
    if (grid_rows) inner_style.grid.template_rows =
        {TrackSizing::fixed(10), TrackSizing::fixed(10)};
    if (grid_aligned) {
        inner_style.grid.template_columns =
            {TrackSizing::fixed(15), TrackSizing::fixed(15)};
        inner_style.align_items = ItemAlignment{ItemPosition::Baseline};
    }
    const auto inner = tree.create_node(inner_style);
    tree.append_child(outer, inner);
    const int base = second ? 5100 : scroll ? 5110 : wrap ? 5120
        : column ? 5130 : grid_rows ? 5140 : 5150;
    std::vector<IdentifiedNode> nodes{{base, outer}, {base + 1, inner}};
    if (second) {
        Style empty_style;
        empty_style.display = Display::Block;
        empty_style.box.size = {Length::length(10), Length::length(5)};
        const auto empty = tree.create_node(empty_style);
        tree.append_child(inner, empty);
        nodes.emplace_back(base + 2, empty);
    }
    Style first_style;
    first_style.box.size = {Length::length(wrap || column ? 20.0f : 10.0f),
                            Length::length(10)};
    if (scroll) first_style.overflow_y = Overflow::Scroll;
    if (grid_rows) {
        first_style.grid.row.start = GridLine::index(2);
        first_style.grid.row.end = GridLine::index(3);
    }
    const auto first = tree.create_node(first_style,
        [scroll, wrap, column](const MeasureRequest&) {
            return Measurement{{wrap || column ? 20.0f : 10.0f,
                    scroll ? 40.0f : 10.0f},
                scroll ? 40.0f : 3.0f, {}};
        });
    tree.append_child(inner, first);
    nodes.emplace_back(base + (second ? 3 : 2), first);
    if (wrap || column || grid_rows || grid_aligned) {
        Style second_style;
        second_style.box.size = {Length::length(wrap || column ? 20.0f : 10.0f),
                                 Length::length(10)};
        if (grid_rows) {
            second_style.grid.row.start = GridLine::index(1);
            second_style.grid.row.end = GridLine::index(2);
        }
        const auto next = tree.create_node(second_style,
            [wrap, column](const MeasureRequest&) {
                return Measurement{{wrap || column ? 20.0f : 10.0f, 10}, 7.0f, {}};
            });
        tree.append_child(inner, next);
        nodes.emplace_back(base + 3, next);
    }
    Style sibling_style;
    sibling_style.box.size = {Length::length(20), Length::length(20)};
    const auto sibling = tree.create_node(sibling_style,
        [](const MeasureRequest&) { return Measurement{{20, 20}, 15.0f, {}}; });
    tree.append_child(outer, sibling);
    nodes.emplace_back(base + (second ? 4 : (scroll ? 3 : 4)), sibling);
    print(name.data(), tree.compute(outer,
        {AvailableSpace::definite(100), AvailableSpace::definite(50)}), nodes);
}

void scroll_baseline_case(std::string_view name) {
    const bool grid = name.starts_with("grid-");
    const int base = grid ? 5170 : 5160;
    LayoutTree tree;
    Style root_style;
    root_style.display = grid ? Display::Grid : Display::Flex;
    root_style.box.size = {Length::length(100), Length::length(40)};
    root_style.align_items = ItemAlignment{ItemPosition::Baseline};
    if (grid) root_style.grid.template_columns =
        {TrackSizing::fixed(50), TrackSizing::fixed(50)};
    const auto root = tree.create_node(root_style);
    Style child_style;
    child_style.box.size = {Length::length(20), Length::length(10)};
    child_style.overflow_y = Overflow::Scroll;
    const auto scroll = tree.create_node(child_style,
        [](const MeasureRequest&) { return Measurement{{20, 40}, 40.0f, {}}; });
    child_style.overflow_y = Overflow::Visible;
    const auto normal = tree.create_node(child_style,
        [](const MeasureRequest&) { return Measurement{{20, 10}, 7.0f, {}}; });
    tree.append_child(root, scroll);
    tree.append_child(root, normal);
    print(name.data(), tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::definite(40)}),
        {{base, root}, {base + 1, scroll}, {base + 2, normal}});
}

void root_percentage_edges_case(std::string_view name) {
    LayoutTree tree;
    const bool block = name.starts_with("root-block-");
    const bool flex = name.starts_with("root-flex-");
    const int kind = block ? 0 : flex ? 1 : 2;
    const bool padding = name.find("-padding-") != std::string_view::npos;
    const bool margin = name.find("-margin-") != std::string_view::npos;
    const int variant = padding ? 0 : margin ? 1 : 2;
    const int base = 6000 + kind * 30 + variant * 10;
    Style root_style;
    root_style.display = block ? Display::Block : flex ? Display::Flex : Display::Grid;
    if (padding) {
        root_style.box.padding.left = Length::percent(0.1f);
        root_style.box.padding.right = Length::percent(0.2f);
        root_style.box.padding.top = Length::percent(0.1f);
        root_style.box.padding.bottom = Length::percent(0.2f);
    } else if (margin) {
        root_style.box.margin.left = Length::percent(0.1f);
        root_style.box.margin.top = Length::percent(0.2f);
    } else {
        root_style.box.border.left = Length::percent(0.1f);
        root_style.box.border.top = Length::percent(0.2f);
    }
    const auto root = tree.create_node(root_style);
    Style child_style;
    child_style.box.size = {Length::length(20), Length::length(10)};
    const auto child = tree.create_node(child_style);
    tree.append_child(root, child);
    print(name.data(), tree.compute(root,
        {AvailableSpace::max_content(), AvailableSpace::max_content()}),
        {{base, root}, {base + 1, child}});
}

void grid_repeat_percentage_gap_case(std::string_view name) {
    const bool row_axis = name.starts_with("repeat-gap-row-");
    const bool fit = name == "repeat-gap-fixed-fit";
    const bool maximum = name == "repeat-gap-max-fill"
        || name == "repeat-gap-row-max";
    const bool minimum = name == "repeat-gap-min-fill"
        || name == "repeat-gap-row-min";
    const bool automatic = name == "repeat-gap-auto-fill";
    const bool percent_track = name == "repeat-gap-fixed-percent-track";
    const int variant = row_axis ? (maximum ? 6 : 7) : fit ? 1
        : maximum ? 2 : minimum ? 3 : automatic ? 4 : percent_track ? 5 : 0;
    const int base = 6200 + variant * 10;
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Grid;
    const GridRepeat repeated{
        fit ? RepeatKind::AutoFit : RepeatKind::AutoFill,
        1, {percent_track ? TrackSizing::percent(0.2f)
            : TrackSizing::fixed(20)}, {}};
    root_style.grid.template_columns = row_axis
        ? std::vector<TemplateTrack>{TrackSizing::fixed(10)}
        : std::vector<TemplateTrack>{repeated};
    root_style.grid.template_rows = row_axis
        ? std::vector<TemplateTrack>{repeated}
        : std::vector<TemplateTrack>{TrackSizing::fixed(10)};
    if (row_axis) root_style.gap.height = Length::percent(0.1f);
    else root_style.gap.width = Length::percent(0.1f);
    if (!automatic) {
        auto& dimension = row_axis ? root_style.box.size.height
            : root_style.box.size.width;
        auto& min_dimension = row_axis ? root_style.box.min_size.height
            : root_style.box.min_size.width;
        auto& max_dimension = row_axis ? root_style.box.max_size.height
            : root_style.box.max_size.width;
        if (maximum) max_dimension = Length::length(100);
        else if (minimum) min_dimension = Length::length(100);
        else dimension = Length::length(100);
    }
    const auto root = tree.create_node(root_style);
    std::vector<IdentifiedNode> nodes{{base, root}};
    const int count = fit ? 2 : 4;
    for (int i = 0; i < count; ++i) {
        Style child_style;
        child_style.box.size = {Length::length(10), Length::length(10)};
        const auto child = tree.create_node(child_style);
        tree.append_child(root, child);
        nodes.emplace_back(base + i + 1, child);
    }
    print(name.data(), tree.compute(root,
        {AvailableSpace::max_content(), AvailableSpace::max_content()}), nodes);
}

void grid_auto_placement_case(std::string_view name) {
    const bool column = name.starts_with("placement-column-")
        || name == "placement-negative-row-start";
    const bool dense = name.find("dense-secondary") != std::string_view::npos;
    const bool secondary_first = name.find("secondary-first") != std::string_view::npos;
    const bool dense_secondary = name.find("dense-secondary") != std::string_view::npos;
    const bool negative_start = name.find("negative-") != std::string_view::npos;
    const bool backtrack = name.find("primary-backtrack") != std::string_view::npos;
    const int variant = name == "placement-row-secondary-first" ? 0
        : name == "placement-column-secondary-first" ? 1
        : name == "placement-row-dense-secondary" ? 2
        : name == "placement-column-dense-secondary" ? 3
        : name == "placement-negative-column-start" ? 4
        : name == "placement-negative-row-start" ? 5
        : name == "placement-row-primary-backtrack" ? 6 : 7;
    const int base = 6300 + variant * 10;
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Grid;
    root_style.box.size = {Length::length(120), Length::length(120)};
    const int column_count = !column && (negative_start || backtrack) ? 2 : 3;
    root_style.grid.template_columns.assign(column_count, TrackSizing::fixed(20));
    root_style.grid.template_rows.assign(2, TrackSizing::fixed(20));
    root_style.grid.auto_columns = {TrackSizing::fixed(20)};
    root_style.grid.auto_rows = {TrackSizing::fixed(20)};
    root_style.grid.auto_flow = column
        ? (dense ? GridAutoFlow::ColumnDense : GridAutoFlow::Column)
        : (dense ? GridAutoFlow::RowDense : GridAutoFlow::Row);
    const auto root = tree.create_node(root_style);
    std::vector<IdentifiedNode> nodes{{base, root}};
    const int child_count = backtrack ? 5 : negative_start ? 3 : 4;
    for (int i = 0; i < child_count; ++i) {
        Style child_style;
        child_style.box.size = {Length::length(10), Length::length(10)};
        if (secondary_first && (i == 1 || i == 2)) {
            auto& placement = column ? child_style.grid.column : child_style.grid.row;
            placement.start = GridLine::index(1);
        }
        if (dense_secondary) {
            if (i == 0) {
                auto& placement = column ? child_style.grid.row : child_style.grid.column;
                placement.start = GridLine::index(2);
                placement.end = GridLine::span(2);
            } else if (i == 1) {
                auto& placement = column ? child_style.grid.column : child_style.grid.row;
                placement.start = GridLine::index(1);
            }
        }
        if (negative_start && i == 0) {
            auto& negative = column ? child_style.grid.row : child_style.grid.column;
            auto& other = column ? child_style.grid.column : child_style.grid.row;
            negative.start = GridLine::index(-4);
            other.start = GridLine::index(2);
        }
        if (backtrack && (i == 2 || i == 3)) {
            auto& placement = column ? child_style.grid.row : child_style.grid.column;
            placement.start = GridLine::index(1);
        }
        const auto child = tree.create_node(child_style);
        tree.append_child(root, child);
        nodes.emplace_back(base + i + 1, child);
    }
    print(name.data(), tree.compute(root,
        {AvailableSpace::definite(120), AvailableSpace::definite(120)}), nodes);
}

void grid_auto_margins() {
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Grid;
    root_style.box.size = {Length::length(100), Length::length(50)};
    root_style.grid.template_columns = {TrackSizing::fixed(100)};
    root_style.grid.template_rows = {TrackSizing::fixed(50)};
    const auto root = tree.create_node(root_style);
    Style child_style;
    child_style.box.size = {Length::length(20), Length::length(10)};
    child_style.box.margin.left = Length::automatic();
    child_style.box.margin.right = Length::automatic();
    child_style.box.margin.top = Length::automatic();
    const auto child = tree.create_node(child_style);
    tree.append_child(root, child);
    print("grid-auto-margins", tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::definite(50)}),
        {{150, root}, {151, child}});
}

void rtl_block_child() {
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Block;
    root_style.box.direction = Direction::Rtl;
    root_style.box.size = {Length::length(100), Length::length(40)};
    const auto root = tree.create_node(root_style);
    Style child_style;
    child_style.display = Display::Block;
    child_style.box.size = {Length::length(20), Length::length(10)};
    const auto child = tree.create_node(child_style);
    tree.append_child(root, child);
    print("rtl-block-child", tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::definite(40)}),
        {{160, root}, {161, child}});
}

void flex_scrollbar_reservation() {
    LayoutTree tree;
    Style root_style;
    root_style.box.size = {Length::length(100), Length::length(50)};
    root_style.overflow_y = Overflow::Scroll;
    root_style.scrollbar_width = 10;
    const auto root = tree.create_node(root_style);
    Style child_style;
    child_style.flex_grow = 1;
    child_style.box.size.height = Length::length(20);
    const auto child = tree.create_node(child_style,
        [](const MeasureRequest& request) {
            const Unit width = request.available_width.kind == AvailableKind::MinContent
                ? 20.0f : 30.0f;
            return Measurement{{width, 20}, {}, {}};
        });
    tree.append_child(root, child);
    print("flex-scrollbar-reservation", tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::definite(50)}),
        {{170, root}, {171, child}});
}

void block_legacy_text_align() {
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Block;
    root_style.box.size = {Length::length(100), Length::length(40)};
    root_style.text_align = TextAlign::LegacyCenter;
    const auto root = tree.create_node(root_style);
    Style child_style;
    child_style.display = Display::Block;
    child_style.box.size = {Length::length(20), Length::length(10)};
    const auto child = tree.create_node(child_style);
    tree.append_child(root, child);
    print("block-legacy-text-align", tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::definite(40)}),
        {{180, root}, {181, child}});
}

void block_contain_float() {
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Block;
    root_style.box.size.width = Length::length(100);
    root_style.contain = Contain::Layout;
    const auto root = tree.create_node(root_style);
    Style child_style;
    child_style.display = Display::Block;
    child_style.float_side = FloatSide::Left;
    child_style.box.size = {Length::length(20), Length::length(30)};
    const auto child = tree.create_node(child_style);
    tree.append_child(root, child);
    print("block-contain-float", tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::max_content()}),
        {{190, root}, {191, child}});
}

void grid_absolute_area() {
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Grid;
    root_style.box.size = {Length::length(200), Length::length(100)};
    root_style.grid.template_columns = {TrackSizing::fixed(50), TrackSizing::fixed(150)};
    root_style.grid.template_rows = {TrackSizing::fixed(30), TrackSizing::fixed(70)};
    const auto root = tree.create_node(root_style);
    Style child_style;
    child_style.position = Position::Absolute;
    child_style.grid.column = {GridLine::index(2), GridLine::index(3)};
    child_style.grid.row = {GridLine::index(2), GridLine::index(3)};
    child_style.inset.left = Length::length(10);
    child_style.inset.top = Length::length(5);
    child_style.box.size = {Length::length(20), Length::length(10)};
    const auto child = tree.create_node(child_style);
    tree.append_child(root, child);
    print("grid-absolute-area", tree.compute(root,
        {AvailableSpace::definite(200), AvailableSpace::definite(100)}),
        {{200, root}, {201, child}});
}

void grid_implicit_negative_lines() {
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Grid;
    root_style.box.size = {Length::length(100), Length::length(30)};
    root_style.grid.template_columns = {TrackSizing::fixed(30), TrackSizing::fixed(30)};
    root_style.grid.template_rows = {TrackSizing::fixed(20)};
    root_style.grid.auto_columns = {TrackSizing::fixed(15), TrackSizing::fixed(20)};
    const auto root = tree.create_node(root_style);
    Style child_style;
    child_style.box.size = {Length::length(10), Length::length(10)};
    child_style.grid.column = {GridLine::index(-2), GridLine::span(1)};
    child_style.grid.row = {GridLine::index(1), GridLine::span(1)};
    const auto first = tree.create_node(child_style);
    child_style.grid.column = {GridLine::index(4), GridLine::span(1)};
    const auto second = tree.create_node(child_style);
    tree.append_child(root, first);
    tree.append_child(root, second);
    print("grid-implicit-negative-lines", tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::definite(30)}),
        {{210, root}, {211, first}, {212, second}});
}

void grid_implicit_rows(std::string_view name) {
    const bool positive = name == "grid-implicit-positive-rows";
    const bool negative = name == "grid-implicit-negative-rows";
    const int id = positive ? 1890 : negative ? 1900 : 1910;
    const std::vector<std::pair<int, int>> lines = positive
        ? std::vector<std::pair<int, int>>{{2, 3}, {3, 4}, {4, 5}}
        : negative ? std::vector<std::pair<int, int>>{
            {-4, -3}, {-3, -2}, {-2, -1}}
        : std::vector<std::pair<int, int>>{
            {-3, -2}, {-2, -1}, {2, 3}, {3, 4}};
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Grid;
    root_style.box.size = {Length::length(100), Length::length(100)};
    root_style.grid.template_columns = {TrackSizing::fixed(20)};
    root_style.grid.template_rows = {TrackSizing::fixed(10)};
    root_style.grid.auto_rows = {TrackSizing::fixed(15), TrackSizing::fixed(25)};
    const auto root = tree.create_node(root_style);
    std::vector<IdentifiedNode> nodes{{id, root}};
    for (const auto [start, end] : lines) {
        Style style;
        style.grid.row = {GridLine::index(static_cast<std::int16_t>(start)),
                          GridLine::index(static_cast<std::int16_t>(end))};
        const auto child = tree.create_node(style);
        tree.append_child(root, child);
        nodes.emplace_back(id + static_cast<int>(nodes.size()), child);
    }
    print(name.data(), tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::definite(100)}), nodes);
}

void self_alignment_direction_case(std::string_view name) {
    const bool grid = name.starts_with("grid-");
    const bool ltr = name.ends_with("ltr-rtl");
    const int id = grid ? (ltr ? 1920 : 1930) : (ltr ? 1940 : 1950);
    LayoutTree tree;
    Style root_style;
    root_style.display = grid ? Display::Grid : Display::Flex;
    root_style.box.direction = ltr ? Direction::Ltr : Direction::Rtl;
    root_style.box.size = {Length::length(100), Length::length(100)};
    if (grid) {
        root_style.grid.template_columns = {TrackSizing::fixed(100)};
        root_style.grid.template_rows = {TrackSizing::fixed(100)};
    } else {
        root_style.flex_direction = FlexDirection::Column;
    }
    const auto root = tree.create_node(root_style);
    std::vector<IdentifiedNode> nodes{{id, root}};
    for (const auto alignment : {ItemPosition::SelfStart, ItemPosition::SelfEnd}) {
        Style style;
        style.box.direction = ltr ? Direction::Rtl : Direction::Ltr;
        style.box.size = {Length::length(20), Length::length(10)};
        if (grid) {
            style.justify_self = ItemAlignment{alignment};
            style.grid.row = {GridLine::index(1), GridLine::index(2)};
            style.grid.column = {GridLine::index(1), GridLine::index(2)};
        } else {
            style.align_self = ItemAlignment{alignment};
        }
        const auto child = tree.create_node(style);
        tree.append_child(root, child);
        nodes.emplace_back(id + static_cast<int>(nodes.size()), child);
    }
    print(name.data(), tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::definite(100)}), nodes);
}

void leaf_intrinsic_ratio_case(std::string_view name) {
    const bool replaced = name.find("-replaced-ratio") != std::string_view::npos;
    const bool block_replaced = name == "block-replaced-ratio";
    const bool flex_replaced = name.starts_with("flex-replaced-");
    const bool grid_replaced = name.starts_with("grid-replaced-");
    const bool grid_fixed = name.ends_with("-fixed");
    const bool grid_start = name.ends_with("-start");
    const bool max_height = name == "leaf-intrinsic-ratio-max-height";
    const bool max_width = name.ends_with("max-width");
    const int id = block_replaced ? 2010 : grid_replaced
        ? (grid_fixed ? 2050 : grid_start ? 2060 : max_width ? 2070 : 2030)
        : flex_replaced ? (max_width ? 2040 : 2020)
        : max_height ? 1990 : max_width ? 2000 : 1980;
    LayoutTree tree;
    Style style;
    style.box.aspect_ratio = 2.0f;
    if (max_height) style.box.max_size.height = Length::length(15);
    if (max_width) style.box.max_size.width = Length::length(30);
    if (grid_start) style.justify_self = ItemAlignment{ItemPosition::Start};
    if (replaced) {
        if (block_replaced) style.display = Display::Block;
        style.box.replaced = true;
    }
    const auto measured = tree.create_node(style, [](const MeasureRequest& request) {
        const Unit width = request.known_content_width.value_or(
            request.available_width.kind == AvailableKind::MinContent
                ? 20.0f : 40.0f);
        const Unit height = request.known_content_height.value_or(10.0f);
        return Measurement{{width, height}, {}, {}, Size{40, 10}};
    });
    if (!replaced) {
        print(name.data(), tree.compute(measured,
            {AvailableSpace::max_content(), AvailableSpace::max_content()}),
            {{id, measured}});
        return;
    }
    Style root_style;
    root_style.display = block_replaced ? Display::Block
        : grid_replaced ? Display::Grid : Display::Flex;
    root_style.box.size = {Length::length(100), Length::length(100)};
    if (grid_fixed) {
        root_style.grid.template_columns = {TrackSizing::fixed(100)};
        root_style.grid.template_rows = {TrackSizing::fixed(100)};
    }
    const auto root = tree.create_node(root_style);
    tree.append_child(root, measured);
    print(name.data(), tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::definite(100)}),
        {{id, root}, {id + 1, measured}});
}

void percent_gap_case(std::string_view name) {
    const bool grid = name.starts_with("grid-");
    const bool fixed = name.find("-fixed-") != std::string_view::npos;
    const int id = grid ? (fixed ? 2110 : 2100) : (fixed ? 2090 : 2080);
    LayoutTree tree;
    Style root_style;
    root_style.display = grid ? Display::Grid : Display::Flex;
    root_style.gap.width = Length::percent(0.1f);
    if (fixed) root_style.box.size.width = Length::length(100);
    if (grid) {
        root_style.grid.template_columns = {
            TrackSizing::fixed(20), TrackSizing::fixed(20)};
        root_style.grid.template_rows = {TrackSizing::fixed(10)};
    }
    const auto root = tree.create_node(root_style);
    std::vector<IdentifiedNode> nodes{{id, root}};
    for (int index = 0; index < 2; ++index) {
        Style style;
        style.box.size = {Length::length(20), Length::length(10)};
        if (grid) {
            style.grid.column = {
                GridLine::index(static_cast<std::int16_t>(index + 1)),
                GridLine::index(static_cast<std::int16_t>(index + 2))};
        }
        const auto child = tree.create_node(style);
        tree.append_child(root, child);
        nodes.emplace_back(id + static_cast<int>(nodes.size()), child);
    }
    print(name.data(), tree.compute(root,
        {AvailableSpace::max_content(), AvailableSpace::max_content()}), nodes);
}

void percent_cross_gap_case(std::string_view name) {
    const bool grid = name.starts_with("grid-");
    const int id = grid ? 2130 : 2120;
    LayoutTree tree;
    Style root_style;
    root_style.display = grid ? Display::Grid : Display::Flex;
    root_style.box.size.width = Length::length(grid ? 20.0f : 40.0f);
    root_style.gap.height = Length::percent(0.1f);
    if (grid) {
        root_style.grid.template_columns = {TrackSizing::fixed(20)};
        root_style.grid.template_rows = {
            TrackSizing::fixed(10), TrackSizing::fixed(10)};
    } else {
        root_style.flex_wrap = FlexWrap::Wrap;
    }
    const auto root = tree.create_node(root_style);
    std::vector<IdentifiedNode> nodes{{id, root}};
    for (int index = 0; index < (grid ? 2 : 3); ++index) {
        Style style;
        style.box.size = {Length::length(20), Length::length(10)};
        if (grid) {
            style.grid.row = {
                GridLine::index(static_cast<std::int16_t>(index + 1)),
                GridLine::index(static_cast<std::int16_t>(index + 2))};
        }
        const auto child = tree.create_node(style);
        tree.append_child(root, child);
        nodes.emplace_back(id + static_cast<int>(nodes.size()), child);
    }
    print(name.data(), tree.compute(root,
        {AvailableSpace::definite(40), AvailableSpace::max_content()}), nodes);
}

void flex_wrap_reverse_physical_case(std::string_view name) {
    const bool row = name.ends_with("row");
    const int id = row ? 1960 : 1970;
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Flex;
    root_style.flex_direction = row ? FlexDirection::Row : FlexDirection::Column;
    root_style.flex_wrap = FlexWrap::WrapReverse;
    root_style.box.size = {Length::length(100), Length::length(100)};
    const auto root = tree.create_node(root_style);
    std::vector<IdentifiedNode> nodes{{id, root}};
    for (const auto alignment : {ItemPosition::Start, ItemPosition::End,
         ItemPosition::FlexStart, ItemPosition::FlexEnd}) {
        Style style;
        style.box.size = {Length::length(20), Length::length(20)};
        style.align_self = ItemAlignment{alignment};
        const auto child = tree.create_node(style);
        tree.append_child(root, child);
        nodes.emplace_back(id + static_cast<int>(nodes.size()), child);
    }
    print(name.data(), tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::definite(100)}), nodes);
}

void grid_row_dense_holes() {
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Grid;
    root_style.box.size = {Length::length(90), Length::length(40)};
    root_style.grid.template_columns = {
        TrackSizing::fixed(30), TrackSizing::fixed(30), TrackSizing::fixed(30)};
    root_style.grid.template_rows = {TrackSizing::fixed(20), TrackSizing::fixed(20)};
    root_style.grid.auto_flow = GridAutoFlow::RowDense;
    const auto root = tree.create_node(root_style);
    Style wide_style;
    wide_style.box.size = {Length::length(10), Length::length(10)};
    wide_style.grid.column.end = GridLine::span(2);
    const auto first = tree.create_node(wide_style);
    const auto second = tree.create_node(wide_style);
    Style narrow_style;
    narrow_style.box.size = {Length::length(10), Length::length(10)};
    const auto third = tree.create_node(narrow_style);
    for (const auto child : {first, second, third}) tree.append_child(root, child);
    print("grid-row-dense-holes", tree.compute(root,
        {AvailableSpace::definite(90), AvailableSpace::definite(40)}),
        {{220, root}, {221, first}, {222, second}, {223, third}});
}

void grid_named_line_occurrences() {
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Grid;
    root_style.box.size = {Length::length(90), Length::length(30)};
    root_style.grid.template_columns = {
        TrackSizing::fixed(20), TrackSizing::fixed(30), TrackSizing::fixed(40)};
    root_style.grid.column_line_names = {{"x"}, {"x"}, {"x"}, {"x"}};
    root_style.grid.template_rows = {TrackSizing::fixed(20)};
    const auto root = tree.create_node(root_style);
    Style first_style;
    first_style.box.size = {Length::length(10), Length::length(10)};
    first_style.grid.column = {GridLine::named("x", 2), GridLine::span(1)};
    const auto first = tree.create_node(first_style);
    Style second_style;
    second_style.box.size.height = Length::length(10);
    second_style.grid.column = {GridLine::index(1), GridLine::named_span("x", 2)};
    const auto second = tree.create_node(second_style);
    tree.append_child(root, first);
    tree.append_child(root, second);
    print("grid-named-line-occurrences", tree.compute(root,
        {AvailableSpace::definite(90), AvailableSpace::definite(30)}),
        {{230, root}, {231, first}, {232, second}});
}

void block_parent_child_margins() {
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Block;
    root_style.box.size.width = Length::length(100);
    const auto root = tree.create_node(root_style);
    Style parent_style = root_style;
    parent_style.box.margin.top = Length::length(5);
    parent_style.box.margin.bottom = Length::length(7);
    const auto parent = tree.create_node(parent_style);
    Style child_style;
    child_style.display = Display::Block;
    child_style.box.size.height = Length::length(10);
    child_style.box.margin.top = Length::length(20);
    child_style.box.margin.bottom = Length::length(15);
    const auto child = tree.create_node(child_style);
    tree.append_child(root, parent);
    tree.append_child(parent, child);
    print("block-parent-child-margins", tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::max_content()}),
        {{240, root}, {241, parent}, {242, child}});
}

void block_empty_margin_chain() {
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Block;
    root_style.box.size.width = Length::length(100);
    const auto root = tree.create_node(root_style);
    Style empty_style;
    empty_style.display = Display::Block;
    empty_style.box.margin.top = Length::length(5);
    empty_style.box.margin.bottom = Length::length(7);
    const auto parent = tree.create_node(empty_style);
    empty_style.box.margin.top = Length::length(20);
    empty_style.box.margin.bottom = Length::length(15);
    const auto empty = tree.create_node(empty_style);
    Style next_style;
    next_style.display = Display::Block;
    next_style.box.size.height = Length::length(10);
    next_style.box.margin.top = Length::length(8);
    const auto next = tree.create_node(next_style);
    tree.append_child(root, parent);
    tree.append_child(parent, empty);
    tree.append_child(root, next);
    print("block-empty-margin-chain", tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::max_content()}),
        {{250, root}, {251, parent}, {252, empty}, {253, next}});
}

void block_negative_margin_chain() {
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Block;
    root_style.box.size.width = Length::length(100);
    const auto root = tree.create_node(root_style);
    Style empty_style;
    empty_style.display = Display::Block;
    empty_style.box.margin.top = Length::length(20);
    empty_style.box.margin.bottom = Length::length(-30);
    const auto empty = tree.create_node(empty_style);
    Style next_style;
    next_style.display = Display::Block;
    next_style.box.size.height = Length::length(10);
    next_style.box.margin.top = Length::length(15);
    const auto next = tree.create_node(next_style);
    tree.append_child(root, empty);
    tree.append_child(root, next);
    print("block-negative-margin-chain", tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::max_content()}),
        {{260, root}, {261, empty}, {262, next}});
}

void block_margin_guard(bool fixed_height) {
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Block;
    root_style.box.size.width = Length::length(100);
    const auto root = tree.create_node(root_style);
    Style parent_style = root_style;
    if (fixed_height) parent_style.box.size.height = Length::length(50);
    else {
        parent_style.box.padding.top = Length::length(3);
        parent_style.box.padding.bottom = Length::length(4);
    }
    const auto parent = tree.create_node(parent_style);
    Style child_style;
    child_style.display = Display::Block;
    child_style.box.size.height = Length::length(10);
    child_style.box.margin.top = Length::length(20);
    child_style.box.margin.bottom = Length::length(15);
    const auto child = tree.create_node(child_style);
    tree.append_child(root, parent);
    tree.append_child(parent, child);
    const char* name = fixed_height
        ? "block-fixed-height-collapse" : "block-padding-stops-collapse";
    const int first_id = fixed_height ? 280 : 270;
    print(name, tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::max_content()}),
        {{first_id, root}, {first_id + 1, parent}, {first_id + 2, child}});
}

void flex_factor_case(std::string_view name) {
    const bool small_grow = name == "flex-small-grow-factors";
    const bool small_shrink = name == "flex-small-shrink-factor";
    const bool grow_freeze = name == "flex-grow-max-freeze";
    const Unit width = small_shrink ? 40.0f : grow_freeze ? 150.0f : 100.0f;
    const Unit child_width = small_grow ? 20.0f
        : grow_freeze ? 40.0f : 80.0f;
    const int first_id = small_grow ? 300 : small_shrink ? 310
        : grow_freeze ? 320 : 330;
    LayoutTree tree;
    Style root_style;
    root_style.box.size = {Length::length(width), Length::length(40)};
    const auto root = tree.create_node(root_style);
    Style first_style;
    first_style.box.size = {Length::length(child_width), Length::length(10)};
    if (small_grow) first_style.flex_grow = 0.2f;
    if (small_shrink) first_style.flex_shrink = 0.2f;
    if (grow_freeze) {
        first_style.flex_grow = 1;
        first_style.box.max_size.width = Length::length(50);
    }
    if (!small_grow && !small_shrink && !grow_freeze) {
        first_style.box.min_size.width = Length::length(60);
    }
    const auto first = tree.create_node(first_style);
    tree.append_child(root, first);
    std::vector<IdentifiedNode> nodes{{first_id, root}, {first_id + 1, first}};
    if (!small_shrink) {
        Style second_style = first_style;
        if (grow_freeze) second_style.box.max_size.width = Length::automatic();
        else if (!small_grow) second_style.box.min_size.width = Length::length(0);
        const auto second = tree.create_node(second_style);
        tree.append_child(root, second);
        nodes.emplace_back(first_id + 2, second);
    }
    print(name.data(), tree.compute(root,
        {AvailableSpace::definite(width), AvailableSpace::definite(40)}), nodes);
}

void absolute_margin_case(std::string_view name) {
    const bool both_auto = name == "absolute-auto-margins";
    const bool one_auto = name == "absolute-one-auto-margin";
    const bool negative = name == "absolute-negative-auto-margins";
    const int first_id = both_auto ? 400 : one_auto ? 410 : negative ? 420 : 430;
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Block;
    root_style.box.size = {Length::length(100), Length::length(100)};
    if (first_id == 430) root_style.box.direction = Direction::Rtl;
    const auto root = tree.create_node(root_style);
    Style child_style;
    child_style.display = Display::Block;
    child_style.position = Position::Absolute;
    child_style.box.size = negative
        ? Dimensions{Length::length(100), Length::length(100)}
        : Dimensions{Length::length(20), Length::length(20)};
    child_style.inset.left = Length::length(10);
    child_style.inset.right = Length::length(10);
    child_style.inset.top = Length::length(5);
    child_style.inset.bottom = Length::length(5);
    if (first_id != 430) {
        child_style.box.margin.left = Length::automatic();
        child_style.box.margin.top = Length::automatic();
        if (!one_auto) {
            child_style.box.margin.right = Length::automatic();
            child_style.box.margin.bottom = Length::automatic();
        }
    }
    const auto child = tree.create_node(child_style);
    tree.append_child(root, child);
    print(name.data(), tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::definite(100)}),
        {{first_id, root}, {first_id + 1, child}});
}

void absolute_static_alignment(std::string_view name) {
    const bool grid = name == "absolute-grid-static-center"
        || name == "absolute-grid-static-end";
    const bool end = name == "absolute-flex-static-end"
        || name == "absolute-grid-static-end";
    const int first_id = grid ? (end ? 470 : 460) : (end ? 450 : 440);
    LayoutTree tree;
    Style root_style;
    root_style.display = grid ? Display::Grid : Display::Flex;
    root_style.box.size = {Length::length(100), Length::length(100)};
    root_style.align_items = ItemAlignment{end ? ItemPosition::End : ItemPosition::Center};
    if (grid) {
        root_style.justify_items = ItemAlignment{end ? ItemPosition::End : ItemPosition::Center};
    } else {
        root_style.justify_content = ContentAlignment{
            end ? ContentPosition::FlexEnd : ContentPosition::Center};
    }
    const auto root = tree.create_node(root_style);
    Style child_style;
    child_style.position = Position::Absolute;
    child_style.box.size = {Length::length(20), Length::length(10)};
    const auto child = tree.create_node(child_style);
    tree.append_child(root, child);
    print(name.data(), tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::definite(100)}),
        {{first_id, root}, {first_id + 1, child}});
}

void absolute_static_padding(std::string_view name) {
    const bool block = name == "absolute-block-static-padding";
    const bool flex = name == "absolute-flex-static-padding-rtl";
    const int first_id = block ? 480 : flex ? 490 : 500;
    LayoutTree tree;
    Style root_style;
    root_style.display = block ? Display::Block : flex ? Display::Flex : Display::Grid;
    root_style.box.size = {Length::length(100), Length::length(100)};
    root_style.box.padding.left = Length::length(10);
    root_style.box.padding.right = Length::length(20);
    root_style.box.padding.top = Length::length(5);
    root_style.box.padding.bottom = Length::length(15);
    if (flex) root_style.box.direction = Direction::Rtl;
    const auto root = tree.create_node(root_style);
    Style child_style;
    child_style.position = Position::Absolute;
    child_style.box.size = {Length::length(20), Length::length(10)};
    const auto child = tree.create_node(child_style);
    tree.append_child(root, child);
    print(name.data(), tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::definite(100)}),
        {{first_id, root}, {first_id + 1, child}});
}

void absolute_block_static_flow() {
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Block;
    root_style.box.size = {Length::length(100), Length::length(100)};
    const auto root = tree.create_node(root_style);
    Style first_style;
    first_style.display = Display::Block;
    first_style.box.size.height = Length::length(20);
    first_style.box.margin.bottom = Length::length(10);
    const auto first = tree.create_node(first_style);
    Style absolute_style;
    absolute_style.display = Display::Block;
    absolute_style.position = Position::Absolute;
    absolute_style.box.size = {Length::length(20), Length::length(10)};
    absolute_style.box.margin.top = Length::length(5);
    const auto absolute = tree.create_node(absolute_style);
    Style last_style = first_style;
    last_style.box.margin.bottom = Length::length(0);
    last_style.box.margin.top = Length::length(15);
    const auto last = tree.create_node(last_style);
    tree.append_child(root, first);
    tree.append_child(root, absolute);
    tree.append_child(root, last);
    print("absolute-block-static-flow", tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::definite(100)}),
        {{510, root}, {511, first}, {512, absolute}, {513, last}});
}

void constrained_container(std::string_view name) {
    const bool block = name == "block-min-width-before-children";
    const bool flex = name == "flex-min-width-before-children";
    const int first_id = block ? 600 : flex ? 610 : 620;
    LayoutTree tree;
    Style root_style;
    root_style.display = block ? Display::Block : flex ? Display::Flex : Display::Grid;
    root_style.box.size = {Length::length(100), Length::length(40)};
    root_style.box.min_size.width = Length::length(200);
    if (!block && !flex) root_style.grid.template_columns = {TrackSizing::fraction(1)};
    const auto root = tree.create_node(root_style);
    Style child_style;
    child_style.display = Display::Block;
    child_style.box.size.height = Length::length(10);
    if (flex) child_style.flex_grow = 1;
    const auto child = tree.create_node(child_style);
    tree.append_child(root, child);
    print(name.data(), tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::definite(40)}),
        {{first_id, root}, {first_id + 1, child}});
}

void auto_min_height(std::string_view name) {
    const bool grid = name == "grid-auto-min-height";
    const int first_id = grid ? 710 : 700;
    LayoutTree tree;
    Style root_style;
    root_style.display = grid ? Display::Grid : Display::Flex;
    root_style.box.size.width = Length::length(100);
    root_style.box.min_size.height = Length::length(100);
    if (grid) root_style.grid.template_rows = {TrackSizing::fraction(1)};
    const auto root = tree.create_node(root_style);
    Style child_style;
    child_style.display = Display::Block;
    child_style.box.size.width = Length::length(20);
    const auto child = tree.create_node(child_style,
        [](const MeasureRequest&) { return Measurement{{20, 10}, {}, {}}; });
    tree.append_child(root, child);
    print(name.data(), tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::max_content()}),
        {{first_id, root}, {first_id + 1, child}});
}

void grid_negative_implicit_sequence() {
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Grid;
    root_style.box.size = {Length::length(100), Length::length(30)};
    root_style.grid.template_columns = {
        TrackSizing::fixed(30), TrackSizing::fixed(30)};
    root_style.grid.template_rows = {TrackSizing::fixed(20)};
    root_style.grid.auto_columns = {
        TrackSizing::fixed(15), TrackSizing::fixed(20)};
    const auto root = tree.create_node(root_style);
    Style child_style;
    child_style.box.size.height = Length::length(10);
    child_style.grid.row = {GridLine::index(1), GridLine::span(1)};
    child_style.grid.column = {GridLine::index(-5), GridLine::span(1)};
    const auto first = tree.create_node(child_style);
    child_style.grid.column = {GridLine::index(-4), GridLine::span(1)};
    const auto second = tree.create_node(child_style);
    tree.append_child(root, first);
    tree.append_child(root, second);
    print("grid-negative-implicit-sequence", tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::definite(30)}),
        {{800, root}, {801, first}, {802, second}});
}

void block_intrinsic_flag(bool table) {
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Block;
    root_style.box.size.width = Length::length(100);
    const auto root = tree.create_node(root_style);
    Style child_style;
    child_style.display = Display::Block;
    child_style.item_is_table = table;
    child_style.box.replaced = !table;
    const auto child = tree.create_node(child_style,
        [](const MeasureRequest&) { return Measurement{{30, 10}, {}, {}}; });
    tree.append_child(root, child);
    print(table ? "block-table-intrinsic" : "block-replaced-intrinsic",
        tree.compute(root, {AvailableSpace::definite(100),
            AvailableSpace::max_content()}),
        table ? std::vector<IdentifiedNode>{{900, root}, {901, child}}
            : std::vector<IdentifiedNode>{{910, root}, {911, child}});
}

void block_table_margin_boundary() {
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Block;
    root_style.box.size.width = Length::length(100);
    const auto root = tree.create_node(root_style);
    Style table_style;
    table_style.display = Display::Block;
    table_style.item_is_table = true;
    const auto table = tree.create_node(table_style);
    Style child_style;
    child_style.display = Display::Block;
    child_style.box.size.height = Length::length(10);
    child_style.box.margin.top = Length::length(20);
    child_style.box.margin.bottom = Length::length(15);
    const auto child = tree.create_node(child_style);
    tree.append_child(root, table);
    tree.append_child(table, child);
    print("block-table-margin-boundary", tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::max_content()}),
        {{920, root}, {921, table}, {922, child}});
}

void overflow_case(std::string_view name) {
    const bool padding = name == "overflow-scroll-padding";
    const bool rtl = name == "overflow-rtl-origin";
    const bool hidden = name == "overflow-hidden-boundary";
    const int first_id = padding ? 1000 : rtl ? 1010 : hidden ? 1020 : 1030;
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Block;
    const Unit height = hidden ? 100.0f : rtl ? 40.0f : 50.0f;
    root_style.box.size = {Length::length(100), Length::length(height)};
    if (padding) {
        root_style.box.padding.left = Length::length(10);
        root_style.box.padding.right = Length::length(5);
        root_style.box.padding.top = Length::length(3);
        root_style.box.padding.bottom = Length::length(4);
    }
    if (padding || first_id == 1030) root_style.overflow_y = Overflow::Scroll;
    if (rtl) root_style.box.direction = Direction::Rtl;
    const auto root = tree.create_node(root_style);
    Style child_style;
    child_style.display = Display::Block;
    child_style.box.size = {Length::length(20),
        Length::length(hidden ? 20.0f : 10.0f)};
    if (hidden) child_style.overflow_x = Overflow::Hidden;
    if (first_id == 1030) child_style.inset.left = Length::length(-30);
    const auto child = tree.create_node(child_style);
    tree.append_child(root, child);
    std::vector<IdentifiedNode> nodes{{first_id, root}, {first_id + 1, child}};
    if (hidden) {
        Style grandchild_style;
        grandchild_style.display = Display::Block;
        grandchild_style.position = Position::Absolute;
        grandchild_style.box.size = {Length::length(100), Length::length(100)};
        const auto grandchild = tree.create_node(grandchild_style);
        tree.append_child(child, grandchild);
        nodes.emplace_back(first_id + 2, grandchild);
    }
    print(name.data(), tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::definite(height)}), nodes);
}

void overflow_absolute_padding() {
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Block;
    root_style.position = Position::Relative;
    root_style.box.size = {Length::length(100), Length::length(50)};
    root_style.box.padding.right = Length::length(5);
    root_style.box.padding.bottom = Length::length(4);
    root_style.overflow_y = Overflow::Scroll;
    const auto root = tree.create_node(root_style);
    Style inflow_style;
    inflow_style.display = Display::Block;
    inflow_style.box.size = {Length::length(10), Length::length(10)};
    const auto inflow = tree.create_node(inflow_style);
    Style absolute_style;
    absolute_style.display = Display::Block;
    absolute_style.position = Position::Absolute;
    absolute_style.box.size = {Length::length(20), Length::length(20)};
    absolute_style.inset.left = Length::length(40);
    absolute_style.inset.top = Length::length(10);
    const auto absolute = tree.create_node(absolute_style);
    tree.append_child(root, inflow);
    tree.append_child(root, absolute);
    print("overflow-absolute-padding", tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::definite(50)}),
        {{1040, root}, {1041, inflow}, {1042, absolute}});
}

void overflow_border_origin() {
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Block;
    root_style.box.size = {Length::length(100), Length::length(50)};
    root_style.box.border.left = Length::length(3);
    root_style.box.border.top = Length::length(2);
    const auto root = tree.create_node(root_style);
    Style child_style;
    child_style.display = Display::Block;
    child_style.box.size = {Length::length(20), Length::length(10)};
    const auto child = tree.create_node(child_style);
    tree.append_child(root, child);
    print("overflow-border-origin", tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::definite(50)}),
        {{1050, root}, {1051, child}});
}

void grid_span_order(bool long_first) {
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Grid;
    root_style.box.size.height = Length::length(20);
    root_style.grid.template_columns = {TrackSizing::automatic(),
        TrackSizing::automatic(), TrackSizing::automatic()};
    root_style.grid.template_rows = {TrackSizing::fixed(20)};
    const auto root = tree.create_node(root_style);
    const auto add_child = [&](int span, Unit intrinsic_width) {
        Style style;
        style.grid.column = {GridLine::index(1),
            GridLine::span(static_cast<std::uint16_t>(span))};
        style.grid.row = {GridLine::index(1), GridLine::span(1)};
        return tree.create_node(style, [intrinsic_width](const MeasureRequest& request) {
            return Measurement{{request.known_content_width.value_or(intrinsic_width),
                request.known_content_height.value_or(10.0f)}, {}, {},
                Size{intrinsic_width, 10}};
        });
    };
    const auto first = long_first ? add_child(3, 150) : add_child(2, 70);
    const auto second = long_first ? add_child(2, 70) : add_child(3, 150);
    tree.append_child(root, first);
    tree.append_child(root, second);
    const int id = long_first ? 1070 : 1060;
    print(long_first ? "grid-span-order-long-first" : "grid-span-order-short-first",
        tree.compute(root, {AvailableSpace::max_content(), AvailableSpace::definite(20)}),
        {{id, root}, {id + 1, first}, {id + 2, second}});
}

void grid_intrinsic_growth(bool capped) {
    LayoutTree tree;
    const Unit width = capped ? 80.0f : 100.0f;
    Style root_style;
    root_style.display = Display::Grid;
    root_style.box.size = {Length::length(width), Length::length(20)};
    root_style.grid.template_columns = {TrackSizing::automatic(),
        TrackSizing::automatic()};
    root_style.grid.template_rows = {TrackSizing::fixed(20)};
    const auto root = tree.create_node(root_style);
    const auto add_child = [&](int column, Unit preferred) {
        Style style;
        style.grid.column = {GridLine::index(static_cast<std::int16_t>(column)),
            GridLine::span(1)};
        style.grid.row = {GridLine::index(1), GridLine::span(1)};
        return tree.create_node(style, [preferred](const MeasureRequest& request) {
            const Unit intrinsic = request.available_width.kind == AvailableKind::MinContent
                ? 20.0f : preferred;
            return Measurement{{request.known_content_width.value_or(intrinsic),
                request.known_content_height.value_or(10.0f)}, {}, {},
                Size{preferred, 10}};
        });
    };
    const auto first = add_child(1, 70);
    const auto second = add_child(2, capped ? 30.0f : 70.0f);
    tree.append_child(root, first);
    tree.append_child(root, second);
    const int id = capped ? 1090 : 1080;
    print(capped ? "grid-capped-intrinsic-growth" : "grid-equal-intrinsic-growth",
        tree.compute(root, {AvailableSpace::definite(width), AvailableSpace::definite(20)}),
        {{id, root}, {id + 1, first}, {id + 2, second}});
}

enum class GridMinimumCase { Visible, Scrolling, ExplicitMinimum };

void grid_automatic_minimum(GridMinimumCase scenario) {
    LayoutTree tree;
    const bool scrolling = scenario != GridMinimumCase::Visible;
    const bool explicit_minimum = scenario == GridMinimumCase::ExplicitMinimum;
    const Unit width = explicit_minimum ? 10.0f : 40.0f;
    Style root_style;
    root_style.display = Display::Grid;
    root_style.box.size = {Length::length(width), Length::length(20)};
    root_style.grid.template_columns = {TrackSizing::fraction(1)};
    root_style.grid.template_rows = {TrackSizing::fixed(20)};
    const auto root = tree.create_node(root_style);
    Style child_style;
    if (scrolling) {
        child_style.overflow_x = Overflow::Hidden;
        child_style.overflow_y = Overflow::Hidden;
    }
    if (explicit_minimum) child_style.box.min_size.width = Length::length(20);
    const auto child = tree.create_node(child_style, [](const MeasureRequest& request) {
        return Measurement{{request.known_content_width.value_or(80.0f),
            request.known_content_height.value_or(10.0f)}, {}, {}, Size{80, 10}};
    });
    tree.append_child(root, child);
    const int id = explicit_minimum ? 1120 : scrolling ? 1110 : 1100;
    const char* name = explicit_minimum ? "grid-scroll-explicit-minimum"
        : scrolling ? "grid-scroll-automatic-minimum"
        : "grid-visible-automatic-minimum";
    print(name,
        tree.compute(root, {AvailableSpace::definite(width), AvailableSpace::definite(20)}),
        {{id, root}, {id + 1, child}});
}

enum class GridSpanningCase { Flexible, Automatic, Scrolling };

void grid_spanning_minimum(GridSpanningCase scenario) {
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Grid;
    root_style.box.size = {Length::length(40), Length::length(20)};
    const auto track = scenario == GridSpanningCase::Flexible
        ? TrackSizing::fraction(1) : TrackSizing::automatic();
    root_style.grid.template_columns = {track, track};
    root_style.grid.template_rows = {TrackSizing::fixed(20)};
    const auto root = tree.create_node(root_style);
    Style child_style;
    child_style.grid.column = {GridLine::index(1), GridLine::span(2)};
    if (scenario == GridSpanningCase::Scrolling) {
        child_style.overflow_x = Overflow::Hidden;
        child_style.overflow_y = Overflow::Hidden;
    }
    const auto child = tree.create_node(child_style, [](const MeasureRequest& request) {
        return Measurement{{request.known_content_width.value_or(80.0f),
            request.known_content_height.value_or(10.0f)}, {}, {}, Size{80, 10}};
    });
    tree.append_child(root, child);
    const int id = scenario == GridSpanningCase::Flexible ? 1130
        : scenario == GridSpanningCase::Automatic ? 1140 : 1150;
    const char* name = scenario == GridSpanningCase::Flexible
        ? "grid-spanning-flex-minimum"
        : scenario == GridSpanningCase::Automatic
            ? "grid-spanning-auto-minimum" : "grid-spanning-scroll-minimum";
    print(name, tree.compute(root,
        {AvailableSpace::definite(40), AvailableSpace::definite(20)}),
        {{id, root}, {id + 1, child}});
}

void grid_intrinsic_query(std::string_view name) {
    const bool automatic = name == "grid-auto-min-query"
        || name == "grid-auto-max-query";
    const bool minimum = name == "grid-fr-small-min-query"
        || name == "grid-fr-weighted-min-query"
        || name == "grid-auto-min-query";
    const bool small = name == "grid-fr-small-min-query"
        || name == "grid-fr-small-max-query";
    const int id = automatic ? (minimum ? 1200 : 1210)
        : small ? (minimum ? 1160 : 1170) : (minimum ? 1180 : 1190);
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Grid;
    root_style.box.size.height = Length::length(20);
    root_style.grid.template_rows = {TrackSizing::fixed(20)};
    root_style.grid.template_columns = automatic
        ? std::vector<TemplateTrack>{TrackSizing::automatic(), TrackSizing::automatic()}
        : small
            ? std::vector<TemplateTrack>{TrackSizing::fraction(0.2f),
                TrackSizing::fraction(0.2f)}
            : std::vector<TemplateTrack>{TrackSizing::fraction(0.5f),
                TrackSizing::fraction(1)};
    const auto root = tree.create_node(root_style);
    const auto available = minimum ? AvailableSpace::min_content()
        : AvailableSpace::max_content();
    if (automatic) {
        std::vector<IdentifiedNode> nodes{{id, root}};
        for (int column = 1; column <= 2; ++column) {
            Style child_style;
            child_style.grid.column = {GridLine::index(static_cast<std::int16_t>(column)),
                GridLine::span(1)};
            const auto child = tree.create_node(child_style,
                [](const MeasureRequest& request) {
                    const Unit intrinsic = request.available_width.kind
                        == AvailableKind::MinContent ? 20.0f : 70.0f;
                    return Measurement{{request.known_content_width.value_or(intrinsic),
                        request.known_content_height.value_or(10.0f)}, {}, {},
                        Size{70, 10}};
                });
            tree.append_child(root, child);
            nodes.emplace_back(id + column, child);
        }
        print(name.data(), tree.compute(root, {available, AvailableSpace::definite(20)}), nodes);
    } else {
        Style child_style;
        child_style.grid.column = {GridLine::index(1), GridLine::span(2)};
        const auto child = tree.create_node(child_style,
            [](const MeasureRequest& request) {
                const Unit intrinsic = request.available_width.kind
                    == AvailableKind::MinContent ? 80.0f : 100.0f;
                return Measurement{{request.known_content_width.value_or(intrinsic),
                    request.known_content_height.value_or(10.0f)}, {}, {},
                    Size{100, 10}};
            });
        tree.append_child(root, child);
        print(name.data(), tree.compute(root, {available, AvailableSpace::definite(20)}),
            {{id, root}, {id + 1, child}});
    }
}

void grid_max_track_min_query() {
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Grid;
    root_style.box.size.height = Length::length(20);
    root_style.grid.template_columns = {TrackSizing::max_content()};
    root_style.grid.template_rows = {TrackSizing::fixed(20)};
    const auto root = tree.create_node(root_style);
    const auto child = tree.create_node({}, [](const MeasureRequest& request) {
        const Unit intrinsic = request.available_width.kind == AvailableKind::MinContent
            ? 20.0f : 70.0f;
        return Measurement{{request.known_content_width.value_or(intrinsic),
            request.known_content_height.value_or(10.0f)}, {}, {}, Size{70, 10}};
    });
    tree.append_child(root, child);
    print("grid-max-track-min-query", tree.compute(root,
        {AvailableSpace::min_content(), AvailableSpace::definite(20)}),
        {{1220, root}, {1221, child}});
}

void block_float_context_case(std::string_view name) {
    const bool ordinary = name == "block-float-ordinary-overlap";
    const bool fixed = name == "block-float-fixed-wrap";
    const bool both = name == "block-float-both-sides";
    const bool margin = name == "block-float-margin-context";
    const bool overflow = name == "block-float-overflow-context";
    const bool flex = name == "block-float-flex-context";
    const bool grid = name == "block-float-grid-context";
    const int id = ordinary ? 1230 : fixed ? 1250 : both ? 1260
        : margin ? 1270 : overflow ? 1280 : flex ? 1290 : grid ? 1300 : 1240;
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Block;
    root_style.box.size.width = Length::length(100);
    const auto root = tree.create_node(root_style);
    Style float_style;
    float_style.display = Display::Block;
    float_style.float_side = FloatSide::Left;
    float_style.box.size = {Length::length(30), Length::length(30)};
    const auto left = tree.create_node(float_style);
    tree.append_child(root, left);
    std::vector<IdentifiedNode> nodes{{id, root}, {id + 1, left}};
    if (both) {
        float_style.float_side = FloatSide::Right;
        const auto right = tree.create_node(float_style);
        tree.append_child(root, right);
        nodes.emplace_back(id + 2, right);
    }
    Style child_style;
    child_style.display = ordinary || overflow ? Display::Block
        : flex ? Display::Flex : grid ? Display::Grid : Display::FlowRoot;
    child_style.box.size.height = Length::length(10);
    if (fixed) child_style.box.size.width = Length::length(80);
    if (margin) {
        child_style.box.margin.left = Length::length(5);
        child_style.box.margin.right = Length::length(5);
        child_style.box.margin.top = Length::length(5);
    }
    if (overflow) {
        child_style.overflow_x = Overflow::Hidden;
        child_style.overflow_y = Overflow::Hidden;
    }
    const auto child = tree.create_node(child_style);
    tree.append_child(root, child);
    nodes.emplace_back(id + (both ? 3 : 2), child);
    print(name.data(), tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::max_content()}), nodes);
}

void block_float_self_clear() {
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Block;
    root_style.box.size.width = Length::length(100);
    const auto root = tree.create_node(root_style);
    Style float_style;
    float_style.display = Display::Block;
    float_style.float_side = FloatSide::Left;
    float_style.box.size = {Length::length(30), Length::length(30)};
    const auto left = tree.create_node(float_style);
    float_style.float_side = FloatSide::Right;
    float_style.clear = ClearSide::Right;
    float_style.box.size.height = Length::length(10);
    const auto right = tree.create_node(float_style);
    tree.append_child(root, left);
    tree.append_child(root, right);
    print("block-float-self-clear", tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::max_content()}),
        {{1310, root}, {1311, left}, {1312, right}});
}

void block_float_vertical_case(std::string_view name) {
    struct FloatSpec {
        FloatSide side;
        Unit width, height;
        ClearSide clear = ClearSide::None;
    };
    const bool vertical = name == "block-float-vertical-hole";
    const bool left = name == "block-float-staggered-left";
    const bool right = name == "block-float-staggered-right";
    const bool opposite = name == "block-float-opposite-hole";
    const bool clear = name == "block-float-clear-ceiling";
    const bool flow = name == "block-float-hole-flow-root";
    const bool wide_flow = name == "block-float-hole-wide-flow-root";
    const int id = vertical ? 1810 : left ? 1820 : right ? 1830
        : opposite ? 1840 : clear ? 1850 : flow ? 1860
        : wide_flow ? 1870 : 1880;
    std::vector<FloatSpec> specs;
    if (vertical || flow || wide_flow) {
        specs = {{FloatSide::Left, 50, 10}, {FloatSide::Left, 50, 30}};
        if (vertical) specs.push_back({FloatSide::Left, 50, 10});
    } else if (left || right) {
        const auto side = left ? FloatSide::Left : FloatSide::Right;
        specs = {{side, 30, 30}, {side, 30, 10}, {side, 70, 10}};
    } else if (opposite) {
        specs = {{FloatSide::Left, 50, 10}, {FloatSide::Right, 50, 30},
                 {FloatSide::Left, 50, 10}};
    } else if (clear) {
        specs = {{FloatSide::Left, 30, 20},
                 {FloatSide::Right, 30, 10, ClearSide::Both},
                 {FloatSide::Left, 30, 10}};
    } else {
        specs = {{FloatSide::Left, 50, 0}, {FloatSide::Left, 60, 10}};
    }
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Block;
    root_style.box.size.width = Length::length(100);
    const auto root = tree.create_node(root_style);
    std::vector<IdentifiedNode> nodes{{id, root}};
    for (const auto& spec : specs) {
        Style style;
        style.display = Display::Block;
        style.float_side = spec.side;
        style.clear = spec.clear;
        style.box.size = {Length::length(spec.width), Length::length(spec.height)};
        const auto child = tree.create_node(style);
        tree.append_child(root, child);
        nodes.emplace_back(id + static_cast<int>(nodes.size()), child);
    }
    if (flow || wide_flow) {
        Style style;
        style.display = Display::FlowRoot;
        style.box.size = {Length::length(flow ? 50.0f : 80.0f),
                          Length::length(10)};
        const auto child = tree.create_node(style);
        tree.append_child(root, child);
        nodes.emplace_back(id + static_cast<int>(nodes.size()), child);
    }
    print(name.data(), tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::max_content()}), nodes);
}

void block_float_negative_margin_case(std::string_view name) {
    const bool alongside = name == "block-float-negative-alongside";
    const bool below = name == "block-float-negative-below";
    const bool overwide = name == "block-float-negative-below-overwide";
    const int id = alongside ? 2140 : below ? 2150 : overwide ? 2160 : 2170;
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Block;
    root_style.box.size.width = Length::length(100);
    const auto root = tree.create_node(root_style);
    Style float_style;
    float_style.display = Display::Block;
    float_style.float_side = id == 2170 ? FloatSide::Right : FloatSide::Left;
    float_style.box.size = {Length::length(30), Length::length(10)};
    const auto floating = tree.create_node(float_style);
    Style child_style;
    child_style.display = Display::FlowRoot;
    child_style.box.size = {Length::length(overwide ? 110.0f
        : id == 2170 ? 80.0f : 20.0f), Length::length(10)};
    if (id == 2170) child_style.box.margin.right = Length::length(-10);
    else child_style.box.margin.left = Length::length(-10);
    if (below || overwide) child_style.box.margin.top = Length::length(20);
    const auto child = tree.create_node(child_style);
    tree.append_child(root, floating);
    tree.append_child(root, child);
    print(name.data(), tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::max_content()}),
        {{id, root}, {id + 1, floating}, {id + 2, child}});
}

void block_intrinsic_width_case(std::string_view name) {
    const bool floating = name.starts_with("block-float-intrinsic-");
    const bool overflow = name == "block-float-intrinsic-overflow";
    const bool pair = name == "block-float-intrinsic-pair";
    const bool minimum = name == "block-min-content-width";
    const bool flex = name == "block-float-intrinsic-flex";
    const bool grid = name == "block-float-intrinsic-grid";
    const int id = flex ? 1370 : grid ? 1380
        : pair ? 1340 : overflow ? 1330 : floating ? 1320
        : minimum ? 1360 : 1350;
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Block;
    if (floating) root_style.box.size.width = Length::length(100);
    const auto root = tree.create_node(root_style);
    std::vector<IdentifiedNode> nodes{{id, root}};
    NodeId parent = root;
    if (floating) {
        Style float_style;
        float_style.display = Display::Block;
        float_style.float_side = FloatSide::Left;
        parent = tree.create_node(float_style);
        tree.append_child(root, parent);
        nodes.emplace_back(id + 1, parent);
    }
    if (flex || grid) {
        Style container_style;
        container_style.display = flex ? Display::Flex : Display::Grid;
        const auto container = tree.create_node(container_style);
        tree.append_child(parent, container);
        nodes.emplace_back(id + 2, container);
        parent = container;
    }
    if (pair) {
        Style float_style;
        float_style.display = Display::Block;
        float_style.float_side = FloatSide::Left;
        float_style.box.size = {Length::length(30), Length::length(10)};
        for (int index = 0; index < 2; ++index) {
            const auto child = tree.create_node(float_style);
            tree.append_child(parent, child);
            nodes.emplace_back(id + index + 2, child);
        }
    } else {
        const int child_count = floating || minimum ? 1 : 2;
        for (int index = 0; index < child_count; ++index) {
            Style child_style;
            child_style.display = flex || grid ? Display::Flex : Display::Block;
            const Unit intrinsic_min = index == 0 ? 20.0f : 30.0f;
            const Unit intrinsic_max = overflow ? 150.0f
                : index == 0 ? 40.0f : 70.0f;
            const auto child = tree.create_node(child_style,
                [intrinsic_min, intrinsic_max](const MeasureRequest& request) {
                    const Unit width = request.known_content_width.value_or(
                        request.available_width.kind == AvailableKind::MinContent
                            ? intrinsic_min : intrinsic_max);
                    return Measurement{{width,
                        request.known_content_height.value_or(10.0f)},
                        {}, {}, Size{intrinsic_max, 10}};
                });
            tree.append_child(parent, child);
            nodes.emplace_back(id + index + (flex || grid ? 3
                : floating ? 2 : 1), child);
        }
    }
    print(name.data(), tree.compute(root,
        {floating ? AvailableSpace::definite(100)
            : minimum ? AvailableSpace::min_content()
            : AvailableSpace::max_content(),
            AvailableSpace::max_content()}), nodes);
}

void container_size_case(std::string_view name) {
    const bool block = name.starts_with("block-");
    const bool flex = name.starts_with("flex-");
    const bool ratio = name.find("-ratio-") != std::string_view::npos;
    const bool content_box = name.ends_with("-content-box");
    const bool width = content_box || (name.ends_with("-width")
        && !name.ends_with("-auto-width")
        && !name.ends_with("-auto-content-width"));
    const bool height = name.ends_with("-height");
    const int id = content_box ? (block ? 1480 : flex ? 1490 : 1500)
        : !ratio ? (flex ? 1510 : 1520)
        : block ? (width ? 1390 : height ? 1400 : 1410)
        : flex ? (width ? 1420 : height ? 1430 : 1440)
        : (width ? 1450 : height ? 1460 : 1470);
    LayoutTree tree;
    Style root_style;
    root_style.display = block ? Display::Block
        : flex ? Display::Flex : Display::Grid;
    if (ratio) root_style.box.aspect_ratio = 2.0f;
    if (content_box) {
        root_style.box.box_sizing = BoxSizing::ContentBox;
        root_style.box.padding.left = Length::length(10);
        root_style.box.padding.right = Length::length(10);
        root_style.box.padding.top = Length::length(5);
        root_style.box.padding.bottom = Length::length(5);
    }
    if (width) root_style.box.size.width = Length::length(100);
    if (height) root_style.box.size.height = Length::length(40);
    const auto root = tree.create_node(root_style);
    const auto child = tree.create_node({}, [](const MeasureRequest& request) {
        return Measurement{{request.known_content_width.value_or(20.0f),
            request.known_content_height.value_or(10.0f)},
            {}, {}, Size{20, 10}};
    });
    tree.append_child(root, child);
    print(name.data(), tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::max_content()}),
        {{id, root}, {id + 1, child}});
}

void flex_auto_max_wrap_case(bool row, FlexWrap wrap, std::string_view name,
                             int id) {
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Flex;
    root_style.flex_wrap = wrap;
    if (wrap == FlexWrap::Balance || wrap == FlexWrap::BalanceReverse) {
        root_style.flex_line_count = 2;
    }
    if (row) root_style.box.max_size.width = Length::length(60);
    else {
        root_style.flex_direction = FlexDirection::Column;
        root_style.box.max_size.height = Length::length(40);
    }
    const auto root = tree.create_node(root_style);
    std::vector<IdentifiedNode> nodes{{id, root}};
    for (int index = 0; index < 3; ++index) {
        Style child_style;
        child_style.box.size = {Length::length(30),
            Length::length(row ? 10.0f : 30.0f)};
        const auto child = tree.create_node(child_style);
        tree.append_child(root, child);
        nodes.emplace_back(id + index + 1, child);
    }
    print(name.data(), tree.compute(root, {AvailableSpace::definite(100),
        row ? AvailableSpace::max_content() : AvailableSpace::definite(100)}),
        nodes);
}

void percent_inset_case(std::string_view name) {
    const bool border = name == "absolute-percent-border";
    const bool stretch = name == "absolute-percent-stretch";
    const bool relative = name == "relative-percent-basic";
    const bool flex = name == "absolute-percent-flex";
    const bool grid_area = name == "absolute-percent-grid-area";
    const bool grid = name == "absolute-percent-grid" || grid_area;
    const int id = border ? 1590 : stretch ? 1600 : relative ? 1610
        : flex ? 1620 : grid_area ? 1640 : grid ? 1630 : 1580;
    LayoutTree tree;
    Style root_style;
    root_style.display = flex ? Display::Flex
        : grid ? Display::Grid : Display::Block;
    root_style.box.size = {Length::length(100), Length::length(80)};
    if (border) {
        root_style.box.border = {Length::length(5), Length::length(5),
            Length::length(5), Length::length(5)};
        root_style.box.padding = {Length::length(10), Length::length(10),
            Length::length(10), Length::length(10)};
    }
    if (grid_area) {
        root_style.grid.template_columns = {TrackSizing::fixed(50),
            TrackSizing::fixed(50)};
        root_style.grid.template_rows = {TrackSizing::fixed(40),
            TrackSizing::fixed(40)};
    }
    const auto root = tree.create_node(root_style);
    Style child_style;
    child_style.position = relative ? Position::Relative : Position::Absolute;
    child_style.box.size.height = Length::length(10);
    if (!stretch) child_style.box.size.width = Length::length(20);
    if (border) {
        child_style.inset.right = Length::percent(0.10f);
        child_style.inset.bottom = Length::percent(0.25f);
    } else if (stretch) {
        child_style.inset.left = Length::percent(0.10f);
        child_style.inset.right = Length::percent(0.20f);
        child_style.inset.top = Length::length(5);
    } else if (grid_area) {
        child_style.inset.left = Length::percent(0.10f);
        child_style.inset.top = Length::percent(0.25f);
        child_style.grid.column = {GridLine::index(2), GridLine::index(3)};
        child_style.grid.row = {GridLine::index(2), GridLine::index(3)};
    } else {
        child_style.inset.left = Length::percent(0.25f);
        child_style.inset.top = Length::percent(0.50f);
    }
    const auto child = tree.create_node(child_style);
    tree.append_child(root, child);
    print(name.data(), tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::definite(80)}),
        {{id, root}, {id + 1, child}});
}

void absolute_block_intrinsic_case(std::string_view name) {
    const bool percent = name == "absolute-block-auto-percent";
    const bool trailing = name == "absolute-block-auto-trailing";
    const bool floats = name == "absolute-block-auto-floats";
    const int id = percent ? 1660 : trailing ? 1670 : floats ? 1680 : 1650;
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Block;
    root_style.box.size = {Length::length(100), Length::length(80)};
    const auto root = tree.create_node(root_style);
    Style absolute_style;
    absolute_style.display = Display::Block;
    absolute_style.position = Position::Absolute;
    if (percent) {
        absolute_style.inset.left = Length::percent(0.10f);
        absolute_style.inset.top = Length::percent(0.25f);
    } else if (trailing) {
        absolute_style.inset.right = Length::percent(0.10f);
        absolute_style.inset.bottom = Length::percent(0.25f);
    } else if (floats) {
        absolute_style.inset.left = Length::length(5);
    }
    const auto positioned = tree.create_node(absolute_style);
    tree.append_child(root, positioned);
    std::vector<IdentifiedNode> nodes{{id, root}, {id + 1, positioned}};
    if (floats) {
        for (int index = 0; index < 2; ++index) {
            Style float_style;
            float_style.display = Display::Block;
            float_style.float_side = FloatSide::Left;
            float_style.box.size = {Length::length(index == 0 ? 30.0f : 20.0f),
                Length::length(10)};
            const auto child = tree.create_node(float_style);
            tree.append_child(positioned, child);
            nodes.emplace_back(id + index + 2, child);
        }
    } else {
        const auto child = tree.create_node({}, [](const MeasureRequest& request) {
            return Measurement{{request.known_content_width.value_or(20.0f),
                request.known_content_height.value_or(10.0f)},
                {}, {}, Size{20, 10}};
        });
        tree.append_child(positioned, child);
        nodes.emplace_back(id + 2, child);
    }
    print(name.data(), tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::definite(80)}), nodes);
}

void grid_track_phases_case(std::string_view name) {
    const bool fit_fr = name == "grid-fit-fr-tracks";
    const bool minmax_fr = name == "grid-minmax-fr-tracks";
    const bool minmax_auto = name == "grid-minmax-auto-tracks";
    const bool percent = name == "grid-percent-indefinite-tracks";
    const int id = fit_fr ? 1690 : minmax_fr ? 1700
        : minmax_auto ? 1710 : percent ? 1720 : 1730;
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Grid;
    if (!percent) root_style.box.size.width = Length::length(100);
    root_style.box.size.height = Length::length(10);
    root_style.grid.template_rows = {TrackSizing::fixed(10)};
    if (fit_fr) {
        root_style.grid.template_columns = {TrackSizing::fit_content_length(30),
            TrackSizing::fraction(1)};
    } else if (minmax_fr) {
        root_style.grid.template_columns = {TrackSizing::minmax(
            TrackBreadth::length(20), TrackBreadth::fraction(1)),
            TrackSizing::fraction(1)};
    } else if (minmax_auto) {
        root_style.grid.template_columns = {TrackSizing::minmax(
            TrackBreadth::min_content(), TrackBreadth::max_content()),
            TrackSizing::automatic()};
    } else if (percent) {
        root_style.grid.template_columns = {TrackSizing::percent(0.5f),
            TrackSizing::percent(0.5f)};
    } else {
        root_style.grid.template_columns = {TrackSizing::fit_content_length(30),
            TrackSizing::fit_content_length(40)};
    }
    const auto root = tree.create_node(root_style);
    std::vector<IdentifiedNode> nodes{{id, root}};
    for (int index = 0; index < 2; ++index) {
        Style item_style;
        item_style.grid.column = {GridLine::index(
            static_cast<std::int16_t>(index + 1)), GridLine::span(1)};
        const Unit minimum = index == 0 && minmax_fr ? 50.0f
            : index == 0 ? 20.0f : 10.0f;
        const Unit preferred = index == 0 ? 70.0f : 20.0f;
        const auto child = tree.create_node(item_style,
            [minimum, preferred](const MeasureRequest& request) {
                const Unit width = request.known_content_width.value_or(
                    request.available_width.kind == AvailableKind::MinContent
                        ? minimum : preferred);
                return Measurement{{width,
                    request.known_content_height.value_or(10.0f)},
                    {}, {}, Size{preferred, 10}};
            });
        tree.append_child(root, child);
        nodes.emplace_back(id + index + 1, child);
    }
    print(name.data(), tree.compute(root,
        {percent ? AvailableSpace::max_content() : AvailableSpace::definite(100),
            AvailableSpace::definite(10)}), nodes);
}

void grid_cyclic_percentage_rows() {
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Grid;
    root_style.box.size.width = Length::length(10);
    root_style.grid.template_columns = {TrackSizing::fixed(10)};
    root_style.grid.template_rows = {TrackSizing::percent(0.5f),
        TrackSizing::percent(0.5f)};
    const auto root = tree.create_node(root_style);
    std::vector<IdentifiedNode> nodes{{1740, root}};
    for (int index = 0; index < 2; ++index) {
        Style item_style;
        item_style.grid.row = {GridLine::index(
            static_cast<std::int16_t>(index + 1)), GridLine::span(1)};
        const Unit minimum = index == 0 ? 20.0f : 10.0f;
        const Unit preferred = index == 0 ? 70.0f : 20.0f;
        const auto child = tree.create_node(item_style,
            [minimum, preferred](const MeasureRequest& request) {
                const Unit height = request.known_content_height.value_or(
                    request.available_height.kind == AvailableKind::MinContent
                        ? minimum : preferred);
                return Measurement{{request.known_content_width.value_or(10.0f),
                    height}, {}, {}, Size{10, preferred}};
            });
        tree.append_child(root, child);
        nodes.emplace_back(1741 + index, child);
    }
    print("grid-percent-indefinite-rows", tree.compute(root,
        {AvailableSpace::definite(10), AvailableSpace::max_content()}), nodes);
}

void grid_percent_function_case(std::string_view name) {
    const bool fit = name == "grid-fit-percent-indefinite";
    const bool min = name == "grid-minmax-percent-min-indefinite";
    const int id = fit ? 1750 : min ? 1760 : 1770;
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Grid;
    root_style.box.size.height = Length::length(10);
    root_style.grid.template_rows = {TrackSizing::fixed(10)};
    root_style.grid.template_columns = {fit
        ? TrackSizing::fit_content_percent(0.5f)
        : min ? TrackSizing::minmax(TrackBreadth::percent(0.5f),
            TrackBreadth::automatic())
        : TrackSizing::minmax(TrackBreadth::automatic(),
            TrackBreadth::percent(0.5f))};
    const auto root = tree.create_node(root_style);
    const auto child = tree.create_node({}, [](const MeasureRequest& request) {
        const Unit width = request.known_content_width.value_or(
            request.available_width.kind == AvailableKind::MinContent
                ? 20.0f : 70.0f);
        return Measurement{{width, request.known_content_height.value_or(10.0f)},
            {}, {}, Size{70, 10}};
    });
    tree.append_child(root, child);
    print(name.data(), tree.compute(root,
        {AvailableSpace::max_content(), AvailableSpace::definite(10)}),
        {{id, root}, {id + 1, child}});
}

void grid_fixed_growth_case(std::string_view name) {
    const bool single = name == "grid-fixed-max-single";
    const bool auto_second = name == "grid-fixed-max-auto";
    const int id = single ? 1780 : auto_second ? 1790 : 1800;
    const auto capped = TrackSizing::minmax(TrackBreadth::automatic(),
        TrackBreadth::length(40));
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Grid;
    root_style.box.size = {Length::length(100), Length::length(10)};
    root_style.grid.template_rows = {TrackSizing::fixed(10)};
    root_style.grid.template_columns = {capped};
    if (!single) root_style.grid.template_columns.push_back(
        auto_second ? TrackSizing::automatic() : capped);
    const auto root = tree.create_node(root_style);
    std::vector<IdentifiedNode> nodes{{id, root}};
    for (int index = 0; index < (single ? 1 : 2); ++index) {
        Style child_style;
        child_style.grid.column = {GridLine::index(
            static_cast<std::int16_t>(index + 1)), GridLine::span(1)};
        const auto child = tree.create_node(child_style,
            [](const MeasureRequest& request) {
                const Unit width = request.known_content_width.value_or(
                    request.available_width.kind == AvailableKind::MinContent
                        ? 10.0f : 20.0f);
                return Measurement{{width,
                    request.known_content_height.value_or(10.0f)},
                    {}, {}, Size{20, 10}};
            });
        tree.append_child(root, child);
        nodes.emplace_back(id + index + 1, child);
    }
    print(name.data(), tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::definite(10)}), nodes);
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
        return Measurement{{width, height}, {}, {}, Size{30, 15}};
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
        else if (name == "block-float-self-clear") block_float_self_clear();
        else if (name == "block-float-intrinsic-child"
            || name == "block-float-intrinsic-overflow"
            || name == "block-float-intrinsic-pair"
            || name == "block-float-intrinsic-flex"
            || name == "block-float-intrinsic-grid"
            || name == "block-max-content-width"
            || name == "block-min-content-width") block_intrinsic_width_case(name);
        else if (name == "block-ratio-width" || name == "block-ratio-height"
            || name == "block-ratio-auto-width" || name == "flex-ratio-width"
            || name == "flex-ratio-height" || name == "flex-ratio-auto-width"
            || name == "grid-ratio-width" || name == "grid-ratio-height"
            || name == "grid-ratio-auto-width"
            || name == "block-ratio-content-box"
            || name == "flex-ratio-content-box"
            || name == "grid-ratio-content-box"
            || name == "flex-auto-content-width"
            || name == "grid-auto-content-width") container_size_case(name);
        else if (name == "flex-auto-max-width-wrap") {
            flex_auto_max_wrap_case(true, FlexWrap::Wrap, name, 1530);
        } else if (name == "flex-auto-max-height-wrap") {
            flex_auto_max_wrap_case(false, FlexWrap::Wrap, name, 1540);
        } else if (name == "flex-auto-max-width-wrap-reverse") {
            flex_auto_max_wrap_case(true, FlexWrap::WrapReverse, name, 1550);
        } else if (name == "flex-auto-max-width-balance") {
            flex_auto_max_wrap_case(true, FlexWrap::Balance, name, 1560);
        } else if (name == "flex-auto-max-width-balance-reverse") {
            flex_auto_max_wrap_case(true, FlexWrap::BalanceReverse, name, 1570);
        }
        else if (name == "absolute-percent-basic"
            || name == "absolute-percent-border"
            || name == "absolute-percent-stretch"
            || name == "relative-percent-basic"
            || name == "absolute-percent-flex"
            || name == "absolute-percent-grid"
            || name == "absolute-percent-grid-area") percent_inset_case(name);
        else if (name == "absolute-block-auto-static"
            || name == "absolute-block-auto-percent"
            || name == "absolute-block-auto-trailing"
            || name == "absolute-block-auto-floats") absolute_block_intrinsic_case(name);
        else if (name == "grid-fit-fr-tracks"
            || name == "grid-minmax-fr-tracks"
            || name == "grid-minmax-auto-tracks"
            || name == "grid-percent-indefinite-tracks"
            || name == "grid-fit-pair-tracks") grid_track_phases_case(name);
        else if (name == "grid-percent-indefinite-rows") {
            grid_cyclic_percentage_rows();
        }
        else if (name == "grid-fit-percent-indefinite"
            || name == "grid-minmax-percent-min-indefinite"
            || name == "grid-minmax-percent-max-indefinite") {
            grid_percent_function_case(name);
        }
        else if (name == "grid-fixed-max-single"
            || name == "grid-fixed-max-auto"
            || name == "grid-fixed-max-pair") grid_fixed_growth_case(name);
        else if (name == "block-adjacent-margins") block_adjacent_margins();
        else if (name == "grid-content-alignment") grid_content_alignment();
        else if (name == "flex-intrinsic-basis") flex_intrinsic_basis();
        else if (name == "flex-baseline") flex_baseline();
        else if (name == "grid-baseline") grid_baseline();
        else if (name.starts_with("nested-block-")
            || name.starts_with("nested-flex-")
            || name.starts_with("nested-grid-")) nested_container_baseline(name);
        else if (name == "block-second-child-baseline"
            || name == "block-scroll-child-baseline"
            || name == "flex-wrap-reverse-baseline"
            || name == "flex-column-reverse-baseline"
            || name == "grid-first-row-baseline"
            || name == "grid-aligned-baseline") container_baseline_edge(name);
        else if (name == "flex-scroll-baseline"
            || name == "grid-scroll-baseline") scroll_baseline_case(name);
        else if (name.starts_with("root-block-")
            || name.starts_with("root-flex-")
            || name.starts_with("root-grid-")) root_percentage_edges_case(name);
        else if (name.starts_with("repeat-gap-"))
            grid_repeat_percentage_gap_case(name);
        else if (name.starts_with("placement-"))
            grid_auto_placement_case(name);
        else if (name == "grid-auto-margins") grid_auto_margins();
        else if (name == "rtl-block-child") rtl_block_child();
        else if (name == "flex-scrollbar-reservation") flex_scrollbar_reservation();
        else if (name == "block-legacy-text-align") block_legacy_text_align();
        else if (name == "block-contain-float") block_contain_float();
        else if (name == "grid-absolute-area") grid_absolute_area();
        else if (name == "grid-implicit-negative-lines") grid_implicit_negative_lines();
        else if (name == "grid-implicit-positive-rows"
            || name == "grid-implicit-negative-rows"
            || name == "grid-implicit-mixed-rows") grid_implicit_rows(name);
        else if (name == "grid-self-ltr-rtl" || name == "grid-self-rtl-ltr"
            || name == "flex-self-ltr-rtl" || name == "flex-self-rtl-ltr")
            self_alignment_direction_case(name);
        else if (name == "flex-wrap-reverse-physical-row"
            || name == "flex-wrap-reverse-physical-column")
            flex_wrap_reverse_physical_case(name);
        else if (name == "leaf-intrinsic-ratio"
            || name == "leaf-intrinsic-ratio-max-height"
            || name == "leaf-intrinsic-ratio-max-width"
            || name == "block-replaced-ratio"
            || name == "flex-replaced-ratio"
            || name == "grid-replaced-ratio"
            || name == "flex-replaced-ratio-max-width"
            || name == "grid-replaced-ratio-fixed"
            || name == "grid-replaced-ratio-start"
            || name == "grid-replaced-ratio-max-width")
            leaf_intrinsic_ratio_case(name);
        else if (name == "flex-auto-percent-gap"
            || name == "flex-fixed-percent-gap"
            || name == "grid-auto-percent-gap"
            || name == "grid-fixed-percent-gap") percent_gap_case(name);
        else if (name == "flex-auto-cross-percent-gap"
            || name == "grid-auto-row-percent-gap") percent_cross_gap_case(name);
        else if (name == "grid-row-dense-holes") grid_row_dense_holes();
        else if (name == "grid-named-line-occurrences") grid_named_line_occurrences();
        else if (name == "block-parent-child-margins") block_parent_child_margins();
        else if (name == "block-empty-margin-chain") block_empty_margin_chain();
        else if (name == "block-negative-margin-chain") block_negative_margin_chain();
        else if (name == "block-padding-stops-collapse") block_margin_guard(false);
        else if (name == "block-fixed-height-collapse") block_margin_guard(true);
        else if (name == "flex-small-grow-factors"
            || name == "flex-small-shrink-factor"
            || name == "flex-grow-max-freeze"
            || name == "flex-shrink-min-freeze") flex_factor_case(name);
        else if (name == "absolute-auto-margins"
            || name == "absolute-one-auto-margin"
            || name == "absolute-negative-auto-margins"
            || name == "absolute-rtl-overconstraint") absolute_margin_case(name);
        else if (name == "absolute-flex-static-center"
            || name == "absolute-flex-static-end"
            || name == "absolute-grid-static-center"
            || name == "absolute-grid-static-end") absolute_static_alignment(name);
        else if (name == "absolute-block-static-padding"
            || name == "absolute-flex-static-padding-rtl"
            || name == "absolute-grid-static-padding") absolute_static_padding(name);
        else if (name == "absolute-block-static-flow") absolute_block_static_flow();
        else if (name == "block-min-width-before-children"
            || name == "flex-min-width-before-children"
            || name == "grid-min-width-before-tracks") constrained_container(name);
        else if (name == "flex-auto-min-height"
            || name == "grid-auto-min-height") auto_min_height(name);
        else if (name == "grid-negative-implicit-sequence") grid_negative_implicit_sequence();
        else if (name == "block-table-intrinsic") block_intrinsic_flag(true);
        else if (name == "block-replaced-intrinsic") block_intrinsic_flag(false);
        else if (name == "block-table-margin-boundary") block_table_margin_boundary();
        else if (name == "overflow-scroll-padding"
            || name == "overflow-rtl-origin"
            || name == "overflow-hidden-boundary"
            || name == "overflow-unreachable") overflow_case(name);
        else if (name == "overflow-absolute-padding") overflow_absolute_padding();
        else if (name == "overflow-border-origin") overflow_border_origin();
        else if (name == "grid-span-order-short-first") grid_span_order(false);
        else if (name == "grid-span-order-long-first") grid_span_order(true);
        else if (name == "grid-equal-intrinsic-growth") grid_intrinsic_growth(false);
        else if (name == "grid-capped-intrinsic-growth") grid_intrinsic_growth(true);
        else if (name == "grid-visible-automatic-minimum")
            grid_automatic_minimum(GridMinimumCase::Visible);
        else if (name == "grid-scroll-automatic-minimum")
            grid_automatic_minimum(GridMinimumCase::Scrolling);
        else if (name == "grid-scroll-explicit-minimum")
            grid_automatic_minimum(GridMinimumCase::ExplicitMinimum);
        else if (name == "grid-spanning-flex-minimum")
            grid_spanning_minimum(GridSpanningCase::Flexible);
        else if (name == "grid-spanning-auto-minimum")
            grid_spanning_minimum(GridSpanningCase::Automatic);
        else if (name == "grid-spanning-scroll-minimum")
            grid_spanning_minimum(GridSpanningCase::Scrolling);
        else if (name == "grid-fr-small-min-query"
            || name == "grid-fr-small-max-query"
            || name == "grid-fr-weighted-min-query"
            || name == "grid-fr-weighted-max-query"
            || name == "grid-auto-min-query"
            || name == "grid-auto-max-query") grid_intrinsic_query(name);
        else if (name == "grid-max-track-min-query") grid_max_track_min_query();
        else if (name == "block-float-ordinary-overlap"
            || name == "block-float-flow-root"
            || name == "block-float-fixed-wrap"
            || name == "block-float-both-sides"
            || name == "block-float-margin-context"
            || name == "block-float-overflow-context"
            || name == "block-float-flex-context"
            || name == "block-float-grid-context") block_float_context_case(name);
        else if (name == "block-float-vertical-hole"
            || name == "block-float-staggered-left"
            || name == "block-float-staggered-right"
            || name == "block-float-opposite-hole"
            || name == "block-float-clear-ceiling"
            || name == "block-float-hole-flow-root"
            || name == "block-float-hole-wide-flow-root"
            || name == "block-float-zero-height") block_float_vertical_case(name);
        else if (name == "block-float-negative-alongside"
            || name == "block-float-negative-below"
            || name == "block-float-negative-below-overwide"
            || name == "block-float-negative-trailing")
            block_float_negative_margin_case(name);
        else if (name == "mixed-block-flex-grid") mixed_tree();
        else throw std::invalid_argument("Unknown CSS fixture");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
