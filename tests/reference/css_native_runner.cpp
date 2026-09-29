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
        else if (name == "block-adjacent-margins") block_adjacent_margins();
        else if (name == "grid-content-alignment") grid_content_alignment();
        else if (name == "flex-intrinsic-basis") flex_intrinsic_basis();
        else if (name == "flex-baseline") flex_baseline();
        else if (name == "grid-baseline") grid_baseline();
        else if (name == "grid-auto-margins") grid_auto_margins();
        else if (name == "rtl-block-child") rtl_block_child();
        else if (name == "flex-scrollbar-reservation") flex_scrollbar_reservation();
        else if (name == "block-legacy-text-align") block_legacy_text_align();
        else if (name == "block-contain-float") block_contain_float();
        else if (name == "grid-absolute-area") grid_absolute_area();
        else if (name == "grid-implicit-negative-lines") grid_implicit_negative_lines();
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
        else if (name == "mixed-block-flex-grid") mixed_tree();
        else throw std::invalid_argument("Unknown CSS fixture");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
