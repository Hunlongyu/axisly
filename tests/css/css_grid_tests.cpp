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

void definite_tracks_measure_once() {
    for (const bool flexible : {false, true}) {
        LayoutTree tree;
        Style grid;
        grid.display = Display::Grid;
        grid.box.size = {Length::length(100), Length::length(50)};
        grid.grid.template_columns = {flexible
            ? TrackSizing::minmax(TrackBreadth::length(0), TrackBreadth::fraction(1))
            : TrackSizing::fixed(80)};
        grid.grid.template_rows = {TrackSizing::fixed(40)};
        grid.align_items = ItemAlignment{ItemPosition::Start};
        grid.justify_items = ItemAlignment{ItemPosition::Start};
        const auto root = tree.create_node(grid);
        std::size_t calls{};
        const auto leaf = tree.create_node({}, [&](const MeasureRequest&) {
            ++calls;
            return Measurement{{27, 12}, 9, 10};
        });
        tree.append_child(root, leaf);
        const auto& result = tree.compute(root, {});
        require(result.succeeded() && calls == 1 && result.measurement_count == 1,
            "Content-independent tracks need only the final leaf measurement");
        require(result.find(leaf)->border_box == Rect{0, 0, 27, 12}
            && result.find(leaf)->first_baseline == 9
            && result.grid_tracks.front().columns.front() == (flexible ? 100 : 80),
            "Skipping unused intrinsic contributions must retain geometry and baselines");
    }
}

void retained_structure_and_reports() {
    LayoutTree tree;
    Style grid;
    grid.display = Display::Grid;
    grid.box.size = {Length::percent(1), Length::length(100)};
    grid.grid.template_columns = {GridRepeat{RepeatKind::AutoFill, 1, {TrackSizing::fixed(30)}, {}}};
    grid.grid.template_rows = {TrackSizing::fixed(20)};
    grid.grid.auto_rows = {TrackSizing::fixed(20)};
    const auto root = tree.create_node(grid);
    Style item;
    item.box.size = {Length::length(10), Length::length(10)};
    std::vector<NodeId> children;
    for (int i = 0; i < 4; ++i) {
        children.push_back(tree.create_node(item));
        tree.append_child(root, children.back());
    }
    const auto compute = [&](Unit width) -> const LayoutResult& {
        const auto& result = tree.compute(root, {AvailableSpace::definite(width), AvailableSpace::max_content()});
        require(result.succeeded(), "Retained Grid structure must produce a complete result");
        return result;
    };
    auto snapshot = compute(100);
    require(snapshot.grid_tracks.front().columns == std::vector<Unit>{30, 30, 30}
        && snapshot.find(children[3])->border_box == Rect{0, 20, 10, 10},
        "Initial automatic repetition and placement must match the available width");
    snapshot.grid_tracks.front().columns[0] = 777;
    snapshot.grid_tracks.front().items.clear();
    const auto& narrower = compute(70);
    require(narrower.grid_tracks.front().columns == std::vector<Unit>{30, 30}
        && narrower.find(children[2])->border_box == Rect{0, 20, 10, 10},
        "Automatic-repeat input changes must rebuild retained placement");
    item.grid.column.start = GridLine::index(2);
    tree.set_style(children[0], item);
    require(compute(70).find(children[0])->border_box.x == 30,
        "A child's grid-line edit must invalidate its parent's retained placement");
    item.display = Display::None;
    tree.set_style(children[0], item);
    require(compute(70).grid_tracks.front().items.size() == 3,
        "Hidden children must leave the retained placement plan");
    tree.remove(children[1]);
    const auto& removed = compute(70);
    require(removed.nodes.size() == 4 && removed.grid_tracks.front().items.size() == 2,
        "Topology changes must remove stale placement identities");
    auto positioned = tree.style(children[2]);
    positioned.position = Position::Absolute;
    positioned.inset.left = Length::length(5);
    tree.set_style(children[2], positioned);
    const auto& absolute = compute(70);
    require(absolute.grid_tracks.front().items.size() == 1
        && absolute.find(children[2])->border_box.x == 5,
        "Position changes must rebuild the absolute-placement metadata");
    grid.grid.template_columns = {TrackSizing::fixed(50)};
    tree.set_style(root, grid);
    require(compute(100).grid_tracks.front().columns.front() == 50,
        "Template edits must invalidate the compiled Grid structure");
    tree.remove(root);
    require(snapshot.grid_tracks.front().columns.front() == 777
        && snapshot.grid_tracks.front().items.empty()
        && snapshot.find(children[3])->border_box == Rect{0, 20, 10, 10},
        "Published geometry and mutable track reports must outlive and remain independent of the tree");
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

    LayoutTree bounded;
    Style bounded_style;
    bounded_style.display = Display::Grid;
    bounded_style.box.max_size.width = Length::length(100);
    bounded_style.gap.width = Length::percent(0.1f);
    bounded_style.grid.template_columns = {GridRepeat{
        RepeatKind::AutoFill, 1, {TrackSizing::fixed(20)}, {}}};
    bounded_style.grid.template_rows = {TrackSizing::fixed(10)};
    const auto bounded_root = bounded.create_node(bounded_style);
    std::vector<NodeId> children;
    for (int i = 0; i < 4; ++i) {
        Style item_style;
        item_style.box.size = {Length::length(10), Length::length(10)};
        const auto new_item = bounded.create_node(item_style);
        bounded.append_child(bounded_root, new_item);
        children.push_back(new_item);
    }
    const AvailableSize indefinite{AvailableSpace::max_content(),
                                   AvailableSpace::max_content()};
    const auto& maximum = bounded.compute(bounded_root, indefinite);
    require(maximum.succeeded() && maximum.size.width == 60
        && maximum.grid_tracks[0].columns == std::vector<Unit>{20, 20, 20}
        && maximum.find(children.back())->border_box.y == 10,
        "Maximum width and percentage gap must limit automatic repetitions");
    bounded_style.box.max_size.width = Length::automatic();
    bounded_style.box.min_size.width = Length::length(100);
    bounded.set_style(bounded_root, bounded_style);
    const auto& minimum = bounded.compute(bounded_root, indefinite);
    require(minimum.succeeded() && minimum.size.width == 110
        && minimum.grid_tracks[0].columns == std::vector<Unit>{20, 20, 20, 20}
        && minimum.find(children.back())->border_box.x == 93,
        "Minimum width and percentage gap must produce four tracks and expand the container");
}

void overlarge_grid() {
    LayoutTree tree;
    Style grid;
    grid.display = Display::Grid;
    grid.box.size = {Length::length(30), Length::length(10)};
    grid.grid.template_columns = {GridRepeat{RepeatKind::Count, 65535,
        {TrackSizing::fixed(1), TrackSizing::fixed(2), TrackSizing::fixed(3)}, {}},
        TrackSizing::fixed(99)};
    grid.grid.template_rows = {TrackSizing::fixed(1)};
    const auto root = tree.create_node(grid);
    Style item;
    item.box.size = {Length::length(1), Length::length(1)};
    item.grid.column = {GridLine::index(32767), GridLine::span(65535)};
    const auto child = tree.create_node(item);
    tree.append_child(root, child);
    const AvailableSize available{AvailableSpace::max_content(),
        AvailableSpace::max_content()};
    const auto& repeated = tree.compute(root, available);
    require(repeated.succeeded(), "Overlarge explicit repetitions must be truncated");
    const auto& repeated_tracks = repeated.grid_tracks.front();
    require(repeated_tracks.explicit_columns == 10000
        && repeated_tracks.columns.size() == 10000
        && repeated_tracks.columns[0] == 1 && repeated_tracks.columns[1] == 2
        && repeated_tracks.columns[2] == 3 && repeated_tracks.columns.back() == 1,
        "Explicit truncation must preserve the source-order track pattern");
    require(repeated_tracks.items.front().column_start == 9999
        && repeated_tracks.items.front().column_end == 10000,
        "An out-of-grid item must retain one track at the positive boundary");

    grid.grid.template_columns = {TrackSizing::fixed(1)};
    grid.grid.template_rows = {TrackSizing::fixed(1)};
    grid.grid.auto_columns = {TrackSizing::fixed(1)};
    grid.grid.auto_rows = {TrackSizing::fixed(1)};
    item.grid.column = {GridLine::index(-32768), GridLine::index(32767)};
    item.grid.row = item.grid.column;
    tree.set_style(root, grid);
    tree.set_style(child, item);
    const auto& implicit = tree.compute(root, available);
    require(implicit.succeeded(), "Both directions of the implicit grid must be supported");
    const auto& implicit_tracks = implicit.grid_tracks.front();
    require(implicit_tracks.column_origin == -10000 && implicit_tracks.row_origin == -10000
        && implicit_tracks.columns.size() == 20000 && implicit_tracks.rows.size() == 20000
        && implicit_tracks.items.front().column_start == -10000
        && implicit_tracks.items.front().column_end == 10000
        && implicit_tracks.items.front().row_start == -10000
        && implicit_tracks.items.front().row_end == 10000,
        "Grid limits apply separately on each side of the explicit origin");

    grid.box.size.width = Length::length(50000);
    grid.grid.template_columns = {GridRepeat{RepeatKind::Count, 9999,
        {TrackSizing::fixed(1)}, {}}, GridRepeat{RepeatKind::AutoFill, 1,
        {TrackSizing::fixed(0), TrackSizing::fixed(9)}, {}}, TrackSizing::fixed(99)};
    item.grid.column = {};
    item.grid.row = {};
    tree.set_style(root, grid);
    tree.set_style(child, item);
    const auto& automatic = tree.compute(root, available);
    require(automatic.succeeded()
        && automatic.grid_tracks.front().explicit_columns == 10000
        && automatic.grid_tracks.front().columns.back() == 0,
        "An auto-repeat may be clipped partway through its track list after the prefix");
}
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected test case name");
        const std::string_view name = argv[1];
        if (name == "tracks_and_placement") tracks_and_placement();
        else if (name == "definite_tracks_measure_once") definite_tracks_measure_once();
        else if (name == "retained_structure_and_reports") retained_structure_and_reports();
        else if (name == "implicit_and_negative_lines") implicit_and_negative_lines();
        else if (name == "invalid_style") invalid_style();
        else if (name == "span_and_fit_content") span_and_fit_content();
        else if (name == "named_lines_and_areas") named_lines_and_areas();
        else if (name == "automatic_repetition") automatic_repetition();
        else if (name == "overlarge_grid") overlarge_grid();
        else throw std::runtime_error("Unknown test case");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
