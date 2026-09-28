#include <gridly/gridly.hpp>

#include <array>
#include <span>
#include <variant>
#include <vector>

int main() {
    const auto track = gridly::Track::range(gridly::Track::fixed(100), gridly::Track::weighted(2));
    const auto& range = std::get<gridly::RangeTrack>(track.definition());
    const gridly::GridSpec spec{
        .columns = {gridly::Track::fixed(100), gridly::Track::fixed(200)},
        .rows = {gridly::Track::fixed(60)},
        .column_gap = 8,
        .padding = {4, 6, 10, 12}
    };
    const std::vector<gridly::GridItem> items{{.id = 7, .column = 1}};
    const std::span<const gridly::GridItem> view{items};
    const auto result = gridly::compute_grid(spec, view, {400, 100});

    const gridly::GridSpec weighted_spec{
        .columns = {gridly::Track::weighted(1, 50, 70.0f), gridly::Track::weighted()},
        .rows = {gridly::Track::fixed(10)},
        .column_gap = 4,
        .padding = {1, 2, 3, 4}
    };
    const auto weighted = gridly::compute_grid(weighted_spec, view, {204, 20});
    const auto minimum = gridly::compute_grid(weighted_spec, view, {40, 20});
    const std::vector<gridly::GridItem> aligned_items{{
        .id = 7, .column = 1, .margin = {10, 5, 20, 15},
        .horizontal_alignment = gridly::HorizontalAlignment::Center,
        .vertical_alignment = gridly::VerticalAlignment::End,
        .preferred_size = {50, 20}
    }};
    const auto aligned = gridly::compute_grid(spec, aligned_items, {400, 100});

    int measurement_calls = 0;
    const auto measure = +[](gridly::ItemId, const gridly::MeasureInput& input, void* context) -> gridly::MeasureResult {
        ++*static_cast<int*>(context);
        const gridly::Unit height = input.known_width && *input.known_width < 200 ? 60.0f : 20.0f;
        return {{40, height}, {400, height}};
    };
    const gridly::GridSpec measured_spec{.columns = {gridly::Track::content()}, .rows = {gridly::Track::content()}};
    const auto measured = gridly::compute_grid(measured_spec, std::array{gridly::GridItem{}}, {150, 200}, measure, &measurement_calls);
    int spanning_calls = 0;
    const gridly::GridSpec spanning_spec{
        .columns = {gridly::Track::weighted(), gridly::Track::weighted(2)},
        .rows = {gridly::Track::content(), gridly::Track::fixed(10)}, .column_gap = 10, .row_gap = 5
    };
    const auto spanning = gridly::compute_grid(spanning_spec,
        std::array{gridly::GridItem{.id = 33, .column_span = 2, .row_span = 2}}, {150, 200}, measure, &spanning_calls);
    int range_calls = 0;
    const gridly::GridSpec range_spec{
        .columns = {gridly::Track::range(gridly::Track::content(), gridly::Track::fixed(150))},
        .rows = {gridly::Track::range(gridly::Track::content(), gridly::Track::weighted())}
    };
    const auto ranged = gridly::compute_grid(range_spec,
        std::array{gridly::GridItem{.id = 44}}, {500, 0}, measure, &range_calls);
    int lambda_calls = 0;
    const auto lambda_result = gridly::compute_grid(measured_spec, std::array{gridly::GridItem{}}, {150, 200},
        [&](gridly::ItemId id, const gridly::MeasureInput& input) -> gridly::MeasureResult {
            return measure(id, input, &lambda_calls);
        });
    gridly::GridCache cache(measured_spec, std::array{gridly::GridItem{}});
    int cached_calls = 0;
    cache.set_measure(measure, &cached_calls);
    const auto first = cache.compute({150, 200});
    const bool first_valid = !first.cache_hit && first.result.succeeded() && first.measurement_count == 2;
    const auto hit = cache.compute({150, 200});
    const bool hit_valid = hit.cache_hit && hit.measurement_count == 0 && cached_calls == 2
        && hit.result.items.front().rect == gridly::Rect{0, 0, 150, 60};
    const auto changed = cache.compute({150, 200}, 1);
    gridly::GridSpec advanced_spec{.columns = {gridly::Track::percentage(.5f), gridly::Track::weighted()},
        .rows = {gridly::Track::content()}, .auto_flow = gridly::AutoFlow::RowDense, .allow_implicit_tracks = true};
    gridly::GridTree tree;
    const auto root = tree.create_grid(advanced_spec);
    const auto leaf = tree.create_leaf({.id = 99, .column = gridly::auto_position, .row = gridly::auto_position,
        .vertical_alignment = gridly::VerticalAlignment::Baseline, .preferred_size = {10, 12}, .baseline = 8.f});
    tree.append_child(root, leaf);
    const auto first_tree = tree.compute(root, {100, 100}, gridly::PixelRounding{2});
    const bool advanced_valid = first_tree.succeeded() && first_tree.find(leaf)->rect.width == 50
        && tree.compute(root, {100, 100}, gridly::PixelRounding{2}).cache_hit;
    const auto snapped = gridly::round_layout(result);
    gridly::MeasurementCache measurements;
    int standalone_calls = 0;
    measurements.set_measure(measure, &standalone_calls);
    (void)measurements.measure(7, {{}, {}, {100, 100}});
    (void)measurements.measure(7, {{}, {}, {100, 100}});
    const bool measurement_cache_valid = measurements.measurement_count() == 1 && measurements.hit_count() == 1 && standalone_calls == 1;
    return advanced_valid && measurement_cache_valid && snapped.succeeded()
            && first_valid && hit_valid && !changed.cache_hit && cached_calls == 4
            && track.kind() == gridly::TrackKind::Range
            && std::get<gridly::FixedTrack>(range.minimum).size == 100
            && std::get<gridly::WeightedTrack>(range.maximum).weight == 2
            && result.succeeded()
            && result.size == gridly::Size{322, 78}
            && result.items.size() == 1
            && result.items.front().id == 7
            && result.items.front().rect == gridly::Rect{112, 6, 200, 60}
            && weighted.succeeded() && weighted.size == gridly::Size{204, 16}
            && weighted.items.size() == 1 && weighted.items.front().id == 7
            && weighted.items.front().rect == gridly::Rect{75, 2, 126, 10}
            && minimum.succeeded() && minimum.size == gridly::Size{58, 16}
            && minimum.items.size() == 1 && minimum.items.front().id == 7
            && minimum.items.front().rect == gridly::Rect{55, 2, 0, 10}
            && aligned.succeeded() && aligned.size == gridly::Size{322, 78}
            && aligned.items.size() == 1 && aligned.items.front().id == 7
            && aligned.items.front().cell_rect == gridly::Rect{112, 6, 200, 60}
            && aligned.items.front().rect == gridly::Rect{182, 31, 50, 20}
            && measured.succeeded() && measured.size == gridly::Size{150, 60}
            && measured.items.size() == 1 && measured.items.front().rect == gridly::Rect{0, 0, 150, 60}
            && measured.measurement_count == 2 && measurement_calls == 2
            && spanning.succeeded() && spanning.size == gridly::Size{150, 60}
            && spanning.items.size() == 1 && spanning.items.front().id == 33
            && spanning.items.front().cell_rect == gridly::Rect{0, 0, 150, 60}
            && spanning.items.front().rect == gridly::Rect{0, 0, 150, 60}
            && spanning.measurement_count == 2 && spanning_calls == 2
            && ranged.succeeded() && ranged.size == gridly::Size{150, 60}
            && ranged.items.size() == 1 && ranged.items.front().id == 44
            && ranged.items.front().rect == gridly::Rect{0, 0, 150, 60}
            && ranged.measurement_count == 2 && range_calls == 2
            && lambda_result.succeeded() && lambda_result.size == gridly::Size{150, 60}
            && lambda_result.measurement_count == 2 && lambda_calls == 2
        ? 0 : 1;
}
