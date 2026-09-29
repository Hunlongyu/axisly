#include <axisly/css_tree.hpp>

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

void tracks_and_placement() {
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Grid;
    root_style.box.size = {Length::length(200), Length::length(100)};
    root_style.gap.width = Length::length(5);
    root_style.grid.template_columns = {
        TrackSizing::fixed(50),
        TrackSizing::minmax(TrackBreadth::length(20), TrackBreadth::fraction(1)),
    };
    root_style.grid.template_rows = {TrackSizing::fixed(30), TrackSizing::fixed(40)};
    root_style.grid.column_line_names = {{"left"}, {"mid"}, {"right"}};
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
    const auto& result = tree.compute(root,
        {AvailableSpace::definite(200), AvailableSpace::definite(100)});
    require(result.succeeded(), "CSS Grid fixture must succeed");
    require(result.find(a)->border_box == Rect{55, 0, 20, 10}, "Second column placement");
    require(result.find(b)->border_box == Rect{0, 30, 10, 15}, "Second row placement");
    require(result.grid_tracks.size() == 1
        && result.grid_tracks[0].columns == std::vector<Unit>{50, 145}
        && result.grid_tracks[0].rows == std::vector<Unit>{30, 40},
        "CSS track geometry must be observable");
}

void implicit_and_negative_lines() {
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Grid;
    root_style.box.size = {Length::length(100), Length::length(100)};
    root_style.grid.template_columns = {TrackSizing::fixed(30), TrackSizing::fixed(30)};
    root_style.grid.template_rows = {TrackSizing::fixed(20)};
    root_style.grid.auto_columns = {TrackSizing::fixed(15)};
    const auto root = tree.create_node(root_style);
    Style item;
    item.box.size = {Length::length(10), Length::length(10)};
    item.grid.column = {GridLine::index(-2), GridLine::span(1)};
    item.grid.row = {GridLine::index(1), GridLine::span(1)};
    const auto a = tree.create_node(item);
    item.grid.column = {GridLine::index(4), GridLine::span(1)};
    const auto b = tree.create_node(item);
    tree.append_child(root, a);
    tree.append_child(root, b);
    const auto& result = tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::definite(100)});
    require(result.succeeded(), "Implicit and negative grid lines must succeed");
    require(result.find(a)->border_box.x == 30 && result.find(b)->border_box.x == 75,
        "Negative line is relative to explicit grid; positive overflow adds implicit track");
    require(result.grid_tracks[0].columns == std::vector<Unit>{30, 30, 15, 15},
        "Implicit track sizing must use grid-auto-columns");
}

void invalid_style() {
    LayoutTree tree;
    Style invalid;
    invalid.display = Display::Grid;
    invalid.grid.template_areas = std::vector<std::vector<std::string>>{
        {"header", "header"}, {"header", "."},
    };
    bool rejected = false;
    try { (void)tree.create_node(invalid); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Nonrectangular named area must be rejected before layout");
    rejected = false;
    try { (void)GridLine::index(0); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Grid line zero must be rejected");
}

void span_and_fit_content() {
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Grid;
    root_style.box.size = {Length::length(200), Length::length(30)};
    root_style.gap.width = Length::length(10);
    root_style.grid.template_columns = {TrackSizing::automatic(), TrackSizing::automatic()};
    const auto root = tree.create_node(root_style);
    Style item;
    item.grid.column = {GridLine::index(1), GridLine::span(2)};
    const auto leaf = tree.create_node(item, [](const MeasureRequest& request) {
        const auto width = request.available_width.kind == AvailableKind::MinContent ? 80.0f : 130.0f;
        return Measurement{{width, 10}, {}, {}};
    });
    tree.append_child(root, leaf);
    const auto& spanning = tree.compute(root,
        {AvailableSpace::definite(200), AvailableSpace::definite(30)});
    require(spanning.succeeded()
        && spanning.grid_tracks[0].columns == std::vector<Unit>{95, 95}
        && spanning.find(leaf)->border_box.width == 200,
        "Spanning intrinsic contribution and auto-track stretch must fill the grid");

    root_style.grid.template_columns = {TrackSizing::fit_content_length(40)};
    item.grid.column = {};
    tree.set_style(root, root_style);
    tree.set_style(leaf, item);
    const auto& fitted = tree.compute(root,
        {AvailableSpace::definite(200), AvailableSpace::definite(30)});
    require(fitted.succeeded() && fitted.grid_tracks[0].columns == std::vector<Unit>{80},
        "fit-content limit must not undercut its min-content floor");
    tree.set_measure(leaf, [](const MeasureRequest& request) {
        const auto width = request.available_width.kind == AvailableKind::MinContent ? 20.0f : 130.0f;
        return Measurement{{width, 10}, {}, {}};
    });
    const auto& limited = tree.compute(root,
        {AvailableSpace::definite(200), AvailableSpace::definite(30)});
    require(limited.succeeded() && limited.grid_tracks[0].columns == std::vector<Unit>{40},
        "fit-content track must cap growth above min-content");
}

void named_lines_and_areas() {
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
    Style item;
    item.box.size = {Length::length(20), Length::length(10)};
    item.grid.column = {GridLine::named("mid"), GridLine::span(1)};
    item.grid.row = {GridLine::index(1), GridLine::span(1)};
    const auto named = tree.create_node(item);
    item.grid.column = {GridLine::named("main"), GridLine::named("main")};
    item.grid.row = {GridLine::named("main"), GridLine::named("main")};
    const auto area = tree.create_node(item);
    tree.append_child(root, named);
    tree.append_child(root, area);
    const auto& result = tree.compute(root,
        {AvailableSpace::definite(200), AvailableSpace::definite(100)});
    require(result.succeeded(), "Named Grid lines and areas must resolve");
    require(result.find(named)->border_box == Rect{50, 0, 20, 10},
        "Named column line selects the second column");
    require(result.find(area)->border_box == Rect{50, 30, 20, 10},
        "Named area supplies start and end lines on both axes");
}

void automatic_repetition() {
    LayoutTree tree;
    Style root_style;
    root_style.display = Display::Grid;
    root_style.box.size = {Length::length(100), Length::length(30)};
    root_style.gap.width = Length::length(5);
    root_style.grid.template_columns = {GridRepeat{
        RepeatKind::AutoFill, 1, {TrackSizing::fixed(30)}, {}}};
    root_style.grid.template_rows = {TrackSizing::fixed(20)};
    const auto root = tree.create_node(root_style);
    Style item;
    item.box.size = {Length::length(10), Length::length(10)};
    const auto child = tree.create_node(item);
    tree.append_child(root, child);
    const auto& filled = tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::definite(30)});
    require(filled.succeeded() && filled.grid_tracks[0].columns == std::vector<Unit>{30, 30, 30}
        && filled.find(child)->border_box == Rect{0, 0, 10, 10},
        "auto-fill must expand to the maximum repetitions fitting width and gaps");
    root_style.grid.template_columns = {GridRepeat{
        RepeatKind::AutoFit, 1, {TrackSizing::fixed(30)}, {}}};
    tree.set_style(root, root_style);
    const auto& fitted = tree.compute(root,
        {AvailableSpace::definite(100), AvailableSpace::definite(30)});
    require(fitted.succeeded() && fitted.grid_tracks[0].columns == std::vector<Unit>{30, 0, 0}
        && fitted.find(child)->border_box == Rect{0, 0, 10, 10},
        "auto-fit must collapse unoccupied tracks and surrounding gutters");
}
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected test case name");
        const std::string_view name = argv[1];
        if (name == "tracks_and_placement") tracks_and_placement();
        else if (name == "implicit_and_negative_lines") implicit_and_negative_lines();
        else if (name == "invalid_style") invalid_style();
        else if (name == "span_and_fit_content") span_and_fit_content();
        else if (name == "named_lines_and_areas") named_lines_and_areas();
        else if (name == "automatic_repetition") automatic_repetition();
        else throw std::runtime_error("Unknown test case");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
