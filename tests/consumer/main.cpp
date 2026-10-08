#include <axisly/axisly.hpp>

#if __has_include(<axisly/grid.hpp>) || __has_include(<axisly/track.hpp>) \
    || __has_include(<axisly/tree.hpp>) || __has_include(<axisly/cache.hpp>)
#error "The installed package must expose only the unified CSS layout API"
#endif

#include <array>
#include <span>
#include <utility>
#include <variant>
#include <vector>

bool css_consumer_valid() {
    namespace css = axisly::css;
    css::LayoutTree tree;
    css::Style grid_style;
    grid_style.display = css::Display::Grid;
    grid_style.box.size = {css::Length::length(200), css::Length::length(60)};
    grid_style.grid.template_columns = {css::TrackSizing::fixed(200)};
    grid_style.grid.template_rows = {css::TrackSizing::fixed(60)};
    const auto root = tree.create_node(grid_style);
    css::Style flex_style;
    flex_style.display = css::Display::Flex;
    const auto flex = tree.create_node(flex_style);
    tree.append_child(root, flex);
    css::Style block_style;
    block_style.display = css::Display::Block;
    axisly::Unit text_width = 20;
    const auto leaf = tree.create_node(block_style,
        [&](const css::MeasureRequest& request) {
            return css::Measurement{{request.known_content_width.value_or(text_width), 10}, {}, {}};
        });
    tree.append_child(flex, leaf);
    const css::AvailableSize available{css::AvailableSpace::definite(200),
        css::AvailableSpace::definite(60)};
    const auto& first = tree.compute(root, available);
    if (!first.succeeded() || first.size != axisly::Size{200, 60}
        || first.nodes.size() != 3 || !first.find(leaf)) return false;
    const auto* storage = first.nodes.data();
    const auto& reused = tree.compute(root, available);
    if (!reused.cache_hit || reused.measurement_count != 0
        || reused.nodes.data() != storage) return false;
    text_width = 25;
    tree.mark_dirty(leaf);
    const auto& changed = tree.compute(root, available);
    if (!changed.succeeded() || changed.cache_hit
        || changed.measurement_count == 0 || changed.nodes.size() != 3) return false;
    css::LayoutOptions options;
    options.publish_nodes = false;
    options.collect_grid_tracks = false;
    options.rounding = axisly::PixelRounding{1.5F};
    if (!tree.compute(root, available, options).succeeded() || !tree.unrounded_layout(leaf)
        || !tree.layout(leaf, css::CoordinateSpace::Parent)) return false;
    css::LayoutBatch batch;
    const std::array requests{css::LayoutRequest{&tree, root, available}};
    if (!batch.compute(requests).front().succeeded()) return false;
    tree.set_style(root, grid_style);
    if (!tree.compute(root, available).cache_hit) return false;
    auto updated_grid = grid_style;
    updated_grid.box.size.width = css::Length::length(240);
    tree.set_style(root, std::move(updated_grid));
    if (tree.compute(root, available).size.width != 240) return false;

    struct Host final : css::LayoutSource {
        css::Style parent_style, child_style;
        std::array<css::NodeId, 1> children{css::NodeId{202}};
        Host() { child_style.box.size = {css::Length::length(23), css::Length::length(11)}; }
        std::optional<css::LayoutNodeView> node(css::NodeId id) const override {
            if (id.value == 101) return css::LayoutNodeView{parent_style, children};
            if (id.value == 202) return css::LayoutNodeView{child_style, {}, css::NodeId{101}};
            return std::nullopt;
        }
    } host;
    auto memory = std::make_shared<css::LayoutMemory>();
    css::LayoutSession session(host, {}, memory);
    if (!session.compute({101}, {}).succeeded() || session.layout({202})->border_box.width != 23) return false;
    host.child_style.box.size.width = css::Length::length(29);
    session.mark_dirty({202});
    return session.compute({101}, {}).succeeded() && session.layout({202})->border_box.width == 29;
}

int main() { return css_consumer_valid() ? 0 : 1; }
