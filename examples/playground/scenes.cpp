#include "scenes.hpp"
#include "../nested_grid_adapter.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace gridly::playground {
namespace {
struct Definition {
    GridSpec spec;
    std::vector<GridItem> items;
    std::vector<std::string> labels;
    std::vector<std::string> columns;
    std::vector<std::string> rows;
    bool wrap{};
};

void add(Definition& scene, std::uint32_t col, std::uint32_t row,
         std::string label, std::uint32_t col_span = 1, std::uint32_t row_span = 1) {
    scene.items.push_back({.id = scene.items.size() + 1, .column = col, .row = row,
        .column_span = col_span, .row_span = row_span, .margin = {6, 6, 6, 6},
        .preferred_size = {96, 36}});
    scene.labels.push_back(std::move(label));
}

Definition define(std::size_t index) {
    Definition d;
    d.spec.padding = {16, 16, 16, 16};
    d.spec.column_gap = 12;
    d.spec.row_gap = 12;
    d.spec.rows = {Track::fixed(92), Track::weighted()};
    d.rows = {"Fixed 92", "Weight 1"};
    switch (index) {
    case 0:
        d.spec.columns = {Track::fixed(140), Track::fixed(220), Track::fixed(100)};
        d.spec.rows = {Track::fixed(80), Track::fixed(120)};
        d.columns = {"Fixed 140", "Fixed 220", "Fixed 100"};
        d.rows = {"Fixed 80", "Fixed 120"};
        break;
    case 1:
        d.spec.columns = {Track::weighted(), Track::weighted()};
        d.columns = {"Weight 1", "Weight 1"};
        break;
    case 2:
        d.spec.columns = {Track::weighted(), Track::weighted(2), Track::weighted(3)};
        d.columns = {"Weight 1", "Weight 2", "Weight 3"};
        break;
    case 3:
        d.spec.columns = {Track::fixed(180), Track::weighted(), Track::weighted(2)};
        d.columns = {"Fixed 180", "Weight 1", "Weight 2"};
        break;
    case 4:
        d.spec.columns = {Track::content(), Track::weighted()};
        d.spec.rows = {Track::content(), Track::content(), Track::content()};
        d.columns = {"Content", "Weight 1"};
        d.rows = {"Content", "Content", "Content"};
        d.wrap = true;
        for (std::uint32_t row = 0; row < 3; ++row) {
            add(d, 0, row, std::array{"Name", "Server URL", "Very long label"}[row]);
            d.items.back().preferred_size = {std::array{64.0f, 96.0f, 160.0f}[row], 32};
            add(d, 1, row, std::array{"gridly", "https://example.test", "Wraps at the final item width"}[row]);
            d.items.back().preferred_size = {std::array{140.0f, 300.0f, 620.0f}[row], 32};
        }
        return d;
    case 5:
        d.spec.columns = {Track::weighted(), Track::weighted(), Track::weighted()};
        d.spec.rows = {Track::fixed(76), Track::weighted(), Track::weighted()};
        d.columns = {"Weight 1", "Weight 1", "Weight 1"};
        d.rows = {"Fixed 76", "Weight 1", "Weight 1"};
        add(d, 0, 0, "Header / span 3 columns", 3);
        for (std::uint32_t row = 1; row < 3; ++row)
            for (std::uint32_t col = 0; col < 3; ++col)
                add(d, col, row, "Card " + std::to_string((row - 1) * 3 + col + 1));
        return d;
    case 6:
        d.spec.columns = {Track::fixed(180), Track::weighted(), Track::weighted()};
        d.spec.rows = {Track::weighted(), Track::weighted()};
        d.columns = {"Fixed 180", "Weight 1", "Weight 1"};
        d.rows = {"Weight 1", "Weight 1"};
        add(d, 0, 0, "Sidebar / span 2 rows", 1, 2);
        for (std::uint32_t row = 0; row < 2; ++row)
            for (std::uint32_t col = 1; col < 3; ++col) add(d, col, row, "Panel");
        return d;
    case 7:
        d.spec.columns.assign(4, Track::weighted());
        d.spec.rows.assign(4, Track::weighted());
        d.columns.assign(4, "Weight 1");
        d.rows.assign(4, "Weight 1");
        for (std::uint32_t row = 0; row < 4; ++row) {
            for (std::uint32_t col = 0; col < 4; ++col) {
                const auto names = std::array{"S", "C", "E", "F"};
                add(d, col, row, std::string(names[col]) + "/" + names[row]);
                d.items.back().horizontal_alignment = static_cast<HorizontalAlignment>(col);
                d.items.back().vertical_alignment = static_cast<VerticalAlignment>(row);
                d.items.back().preferred_size = {72, 32};
            }
        }
        return d;
    case 8:
        d.spec.columns = {Track::weighted(1, 120, 220.0f),
            Track::range(Track::fixed(160), Track::weighted()), Track::weighted()};
        d.columns = {"W1 [120..220]", "Range 160..W1", "Weight 1"};
        break;
    default: throw std::out_of_range("Unknown scene");
    }
    for (std::uint32_t row = 0; row < d.spec.rows.size(); ++row)
        for (std::uint32_t col = 0; col < d.spec.columns.size(); ++col)
            add(d, col, row, std::string(1, static_cast<char>('A' + d.items.size())));
    return d;
}

// Every scene has at least one nonspanning item on every root track. Read its
// public cell rectangle instead of adding probe items that could alter sizing.
void record_tracks(Frame& frame, const VisualItem& item) {
    if (item.depth != 0) return;
    if (item.input.column_span == 1) frame.columns[item.input.column] = item.cell.width;
    if (item.input.row_span == 1) frame.rows[item.input.row] = item.cell.height;
}

Frame nested(Size available) {
    using examples::NestedGrid;
    const NestedGrid child{
        .spec = {.columns = {Track::weighted(), Track::weighted(2)},
            .rows = {Track::fixed(60), Track::weighted()}, .column_gap = 10, .row_gap = 10,
            .padding = {14, 38, 14, 14}},
        .items = {{.id = 1, .margin = {4,4,4,4}, .preferred_size = {70,32}},
                  {.id = 2, .column = 1, .margin = {4,4,4,4}, .preferred_size = {120,32}},
                  {.id = 3, .row = 1, .column_span = 2, .margin = {4,4,4,4}, .preferred_size = {240,60}}}
    };
    const NestedGrid root{
        .spec = {.columns = {Track::fixed(160), Track::weighted()},
            .rows = {Track::weighted()}, .column_gap = 12, .padding = {16,16,16,16}},
        .items = {{.id = 1, .margin = {6,6,6,6}, .preferred_size = {100,40}},
                  {.id = 2, .column = 1, .margin = {6,6,6,6}}},
        .children = {{2, &child}}
    };
    const auto result = examples::compute_nested_grid(root, available);
    Frame frame{.available = available, .occupied = result.size, .columns = {0,0}, .rows = {0},
        .column_rules = {"Fixed 160", "Weight 1"}, .row_rules = {"Weight 1"},
        .measurements = result.measurement_count, .layouts = result.layout_count};
    if (!result.succeeded()) { frame.error = result.error.message; return frame; }
    for (const auto& placed : result.items) {
        const bool descendant = placed.path.size() > 1;
        const auto& definition = descendant ? child : root;
        const auto& input = definition.items.at(static_cast<std::size_t>(placed.path.back() - 1));
        std::string path;
        for (const auto id : placed.path) path += "/" + std::to_string(id);
        frame.items.push_back({path, descendant ? "Child " + path : (input.id == 1 ? "Sidebar" : "Parent /2"),
            input, placed.rect, placed.cell_rect, input.preferred_size, placed.path.size() - 1,
            descendant || input.id != 2});
        record_tracks(frame, frame.items.back());
    }
    return frame;
}
} // namespace

Frame arrange_scene(std::size_t index, Size available) {
    if (index >= scenes.size()) throw std::out_of_range("Unknown scene");
    if (index == 9) return nested(available);
    const auto d = define(index);
    std::vector<Size> preferred(d.items.size());
    const auto result = compute_grid(d.spec, d.items, available,
        [&](ItemId id, const MeasureInput& input) -> MeasureResult {
            auto pref = d.items.at(static_cast<std::size_t>(id - 1)).preferred_size;
            if (d.wrap && input.known_width)
                pref.height *= std::ceil(pref.width / std::max(1.0f, *input.known_width));
            preferred.at(static_cast<std::size_t>(id - 1)) = pref;
            return {{d.wrap ? 32.0f : 0.0f, d.wrap ? pref.height : 0.0f}, pref};
        });
    Frame frame{.available = available, .occupied = result.size,
        .columns = std::vector<Unit>(d.spec.columns.size()), .rows = std::vector<Unit>(d.spec.rows.size()),
        .column_rules = d.columns, .row_rules = d.rows, .measurements = result.measurement_count, .layouts = 1};
    if (!result.succeeded()) { frame.error = result.error.message; return frame; }
    for (std::size_t i = 0; i < result.items.size(); ++i) {
        const auto& placed = result.items[i];
        frame.items.push_back({"/" + std::to_string(placed.id), d.labels[i], d.items[i],
            placed.rect, placed.cell_rect, preferred[i], 0});
        record_tracks(frame, frame.items.back());
    }
    return frame;
}
} // namespace gridly::playground
