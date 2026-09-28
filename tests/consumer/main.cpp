#include <axisly/axisly.hpp>

#include <array>
#include <span>
#include <variant>
#include <vector>

int main() {
    const auto track = axisly::Track::range(axisly::Track::fixed(100), axisly::Track::weighted(2));
    const auto& range = std::get<axisly::RangeTrack>(track.definition());
    const axisly::GridSpec spec{
        .columns = {axisly::Track::fixed(100), axisly::Track::fixed(200)},
        .rows = {axisly::Track::fixed(60)},
        .column_gap = 8,
        .padding = {4, 6, 10, 12}
    };
    const std::vector<axisly::GridItem> items{{.id = 7, .column = 1}};
    const std::span<const axisly::GridItem> view{items};
    const auto result = axisly::compute_grid(spec, view, {400, 100});

    const axisly::GridSpec weighted_spec{
        .columns = {axisly::Track::weighted(1, 50, 70.0f), axisly::Track::weighted()},
        .rows = {axisly::Track::fixed(10)},
        .column_gap = 4,
        .padding = {1, 2, 3, 4}
    };
    const auto weighted = axisly::compute_grid(weighted_spec, view, {204, 20});
    const auto minimum = axisly::compute_grid(weighted_spec, view, {40, 20});
    const std::vector<axisly::GridItem> aligned_items{{
        .id = 7, .column = 1, .margin = {10, 5, 20, 15},
        .horizontal_alignment = axisly::HorizontalAlignment::Center,
        .vertical_alignment = axisly::VerticalAlignment::End,
        .preferred_size = {50, 20}
    }};
    const auto aligned = axisly::compute_grid(spec, aligned_items, {400, 100});

    int measurement_calls = 0;
    const auto measure = +[](axisly::ItemId, const axisly::MeasureInput& input, void* context) -> axisly::MeasureResult {
        ++*static_cast<int*>(context);
        const axisly::Unit height = input.known_width && *input.known_width < 200 ? 60.0f : 20.0f;
        return {{40, height}, {400, height}};
    };
    const axisly::GridSpec measured_spec{.columns = {axisly::Track::content()}, .rows = {axisly::Track::content()}};
    const auto measured = axisly::compute_grid(measured_spec, std::array{axisly::GridItem{}}, {150, 200}, measure, &measurement_calls);
    int spanning_calls = 0;
    const axisly::GridSpec spanning_spec{
        .columns = {axisly::Track::weighted(), axisly::Track::weighted(2)},
        .rows = {axisly::Track::content(), axisly::Track::fixed(10)}, .column_gap = 10, .row_gap = 5
    };
    const auto spanning = axisly::compute_grid(spanning_spec,
        std::array{axisly::GridItem{.id = 33, .column_span = 2, .row_span = 2}}, {150, 200}, measure, &spanning_calls);
    int range_calls = 0;
    const axisly::GridSpec range_spec{
        .columns = {axisly::Track::range(axisly::Track::content(), axisly::Track::fixed(150))},
        .rows = {axisly::Track::range(axisly::Track::content(), axisly::Track::weighted())}
    };
    const auto ranged = axisly::compute_grid(range_spec,
        std::array{axisly::GridItem{.id = 44}}, {500, 0}, measure, &range_calls);
    int lambda_calls = 0;
    const auto lambda_result = axisly::compute_grid(measured_spec, std::array{axisly::GridItem{}}, {150, 200},
        [&](axisly::ItemId id, const axisly::MeasureInput& input) -> axisly::MeasureResult {
            return measure(id, input, &lambda_calls);
        });
    axisly::GridCache cache(measured_spec, std::array{axisly::GridItem{}});
    int cached_calls = 0;
    cache.set_measure(measure, &cached_calls);
    const auto first = cache.compute({150, 200});
    const bool first_valid = !first.cache_hit && first.result.succeeded() && first.measurement_count == 2;
    const auto hit = cache.compute({150, 200});
    const bool hit_valid = hit.cache_hit && hit.measurement_count == 0 && cached_calls == 2
        && hit.result.items.front().rect == axisly::Rect{0, 0, 150, 60};
    const auto changed = cache.compute({150, 200}, 1);
    axisly::GridSpec advanced_spec{.columns = {axisly::Track::percentage(.5f), axisly::Track::weighted()},
        .rows = {axisly::Track::content()}, .auto_flow = axisly::AutoFlow::RowDense, .allow_implicit_tracks = true};
    axisly::GridTree tree;
    const auto root = tree.create_grid(advanced_spec);
    const auto leaf = tree.create_leaf({.id = 99, .column = axisly::auto_position, .row = axisly::auto_position,
        .vertical_alignment = axisly::VerticalAlignment::Baseline, .preferred_size = {10, 12}, .baseline = 8.f});
    tree.append_child(root, leaf);
    const auto first_tree = tree.compute(root, {100, 100}, axisly::PixelRounding{2});
    const bool advanced_valid = first_tree.succeeded() && first_tree.find(leaf)->rect.width == 50
        && tree.compute(root, {100, 100}, axisly::PixelRounding{2}).cache_hit;
    const auto snapped = axisly::round_layout(result);
    axisly::MeasurementCache measurements;
    int standalone_calls = 0;
    measurements.set_measure(measure, &standalone_calls);
    (void)measurements.measure(7, {{}, {}, {100, 100}});
    (void)measurements.measure(7, {{}, {}, {100, 100}});
    const bool measurement_cache_valid = measurements.measurement_count() == 1 && measurements.hit_count() == 1 && standalone_calls == 1;
    const std::array flex_items{axisly::FlexItem{.id = 1, .preferred_size = {20, 10}},
        axisly::FlexItem{.id = 2, .preferred_size = {10, 10}}};
    const auto flex = axisly::compute_flex({}, flex_items, {50, 20});
    axisly::OverlayItem anchored;
    anchored.id = 3;
    anchored.anchors.right = axisly::AnchorLength::fixed(5);
    anchored.width = 10.0f;
    anchored.height = 5.0f;
    const auto overlay = axisly::compute_overlay({}, std::array{anchored}, {50, 20});
    return advanced_valid && measurement_cache_valid && snapped.succeeded()
            && flex.succeeded() && flex.items.size() == 2
            && flex.items[1].rect == axisly::Rect{20, 0, 10, 20}
            && overlay.succeeded() && overlay.items.size() == 1
            && overlay.items[0].rect == axisly::Rect{35, 0, 10, 5}
            && first_valid && hit_valid && !changed.cache_hit && cached_calls == 4
            && track.kind() == axisly::TrackKind::Range
            && std::get<axisly::FixedTrack>(range.minimum).size == 100
            && std::get<axisly::WeightedTrack>(range.maximum).weight == 2
            && result.succeeded()
            && result.size == axisly::Size{322, 78}
            && result.items.size() == 1
            && result.items.front().id == 7
            && result.items.front().rect == axisly::Rect{112, 6, 200, 60}
            && weighted.succeeded() && weighted.size == axisly::Size{204, 16}
            && weighted.items.size() == 1 && weighted.items.front().id == 7
            && weighted.items.front().rect == axisly::Rect{75, 2, 126, 10}
            && minimum.succeeded() && minimum.size == axisly::Size{58, 16}
            && minimum.items.size() == 1 && minimum.items.front().id == 7
            && minimum.items.front().rect == axisly::Rect{55, 2, 0, 10}
            && aligned.succeeded() && aligned.size == axisly::Size{322, 78}
            && aligned.items.size() == 1 && aligned.items.front().id == 7
            && aligned.items.front().cell_rect == axisly::Rect{112, 6, 200, 60}
            && aligned.items.front().rect == axisly::Rect{182, 31, 50, 20}
            && measured.succeeded() && measured.size == axisly::Size{150, 60}
            && measured.items.size() == 1 && measured.items.front().rect == axisly::Rect{0, 0, 150, 60}
            && measured.measurement_count == 2 && measurement_calls == 2
            && spanning.succeeded() && spanning.size == axisly::Size{150, 60}
            && spanning.items.size() == 1 && spanning.items.front().id == 33
            && spanning.items.front().cell_rect == axisly::Rect{0, 0, 150, 60}
            && spanning.items.front().rect == axisly::Rect{0, 0, 150, 60}
            && spanning.measurement_count == 2 && spanning_calls == 2
            && ranged.succeeded() && ranged.size == axisly::Size{150, 60}
            && ranged.items.size() == 1 && ranged.items.front().id == 44
            && ranged.items.front().rect == axisly::Rect{0, 0, 150, 60}
            && ranged.measurement_count == 2 && range_calls == 2
            && lambda_result.succeeded() && lambda_result.size == axisly::Size{150, 60}
            && lambda_result.measurement_count == 2 && lambda_calls == 2
        ? 0 : 1;
}
