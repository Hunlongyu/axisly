#include <gridly/gridly.hpp>
#include "console_session.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

namespace {
struct Label { gridly::Unit text_width, line_height, baseline; };
gridly::MeasureResult measure(gridly::ItemId, const gridly::MeasureInput& input, void* raw) {
    const auto& label = *static_cast<Label*>(raw);
    const float lines = input.known_width ? std::max(1.f, std::ceil(label.text_width / std::max(1.f, *input.known_width))) : 1.f;
    return {{0, label.line_height}, {label.text_width, lines * label.line_height}, label.baseline};
}
bool print(const gridly::TreeResult& result) {
    if (!result.succeeded()) { std::cerr << result.error.message << '\n'; return false; }
    std::cout << "layouts=" << result.layout_count << " host measurements=" << result.measurement_count
              << " full cache hit=" << result.cache_hit << '\n';
    for (const auto& item : result.items) std::cout << "  item " << item.item_id << ": " << item.rect.x << ", "
        << item.rect.y << "  " << item.rect.width << " x " << item.rect.height << '\n';
    return true;
}
}

int main() {
    gridly::examples::ConsoleSession console;
    using namespace gridly;
    GridSpec spec{.columns = {Track::percentage(.3f), Track::weighted(), Track::weighted()},
        .rows = {Track::content()}, .column_gap = 8, .row_gap = 8, .padding = {8, 8, 8, 8},
        .auto_flow = AutoFlow::RowDense, .allow_implicit_tracks = true};
    GridTree tree;
    const auto root = tree.create_grid(spec, {.id = 100});
    std::vector<Label> labels{{100, 18, 14}, {140, 24, 18}, {70, 16, 12}, {200, 20, 15}};
    std::vector<NodeId> nodes;
    for (std::size_t i = 0; i < labels.size(); ++i) {
        nodes.push_back(tree.create_leaf({.id = i + 1, .column = auto_position, .row = auto_position,
            .vertical_alignment = VerticalAlignment::Baseline}, measure, &labels[i]));
        tree.append_child(root, nodes.back());
    }
    std::cout << "Automatic placement, implicit content rows, percentages and first baselines:\n";
    if (!print(tree.compute(root, {420, 240}, PixelRounding{1.25f}))) return 1;
    std::cout << "Unchanged frame:\n";
    if (!print(tree.compute(root, {420, 240}, PixelRounding{1.25f}))) return 1;
    labels[1].text_width = 230;
    tree.mark_dirty(nodes[1]);
    std::cout << "One label changed (only its two measurement constraints miss):\n";
    if (!print(tree.compute(root, {420, 240}, PixelRounding{1.25f}))) return 1;
    std::cout << "Window resized:\n";
    return print(tree.compute(root, {280, 240}, PixelRounding{1.25f})) ? 0 : 1;
}
