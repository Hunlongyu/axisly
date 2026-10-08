#include <axisly/css_batch.hpp>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace axisly;
using namespace axisly::css;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void compare(const LayoutResult& a, const LayoutResult& b) {
    require(a.succeeded() && b.succeeded() && a.size == b.size && a.nodes.size() == b.nodes.size(), "Result mismatch");
    for (std::size_t i = 0; i < a.nodes.size(); ++i) {
        const auto& x = a.nodes[i]; const auto& y = b.nodes[i];
        require(x.border_box == y.border_box && x.content_box == y.content_box
            && x.padding == y.padding && x.border == y.border && x.margin == y.margin
            && x.scrollable_overflow_rect == y.scrollable_overflow_rect
            && x.first_baseline == y.first_baseline, "Geometry changed after rounding cycle");
    }
}
Style sized(Unit width, Unit height) {
    Style result; result.box.size = {Length::length(width), Length::length(height)}; return result;
}
void nested_and_negative() {
    LayoutTree tree;
    const auto root = tree.create_node(sized(50.25F, 40.25F));
    auto absolute = sized(20.25F, 15.25F);
    absolute.position = Position::Absolute;
    absolute.inset.left = Length::length(-1.5F);
    absolute.inset.top = Length::length(1.25F);
    const auto parent = tree.create_node(absolute);
    tree.append_child(root, parent);
    absolute.inset.left = Length::length(0.25F);
    absolute.inset.top = Length::length(0.25F);
    absolute.box.size = {Length::length(10.25F), Length::length(7.25F)};
    const auto child = tree.create_node(absolute);
    tree.append_child(parent, child);
    const auto logical = tree.compute(root, {});
    const auto& result = tree.compute(root, {}, PixelRounding{});
    require(result.succeeded(), "Rounding failed");
    require(result.find(parent)->border_box == Rect{-1, 1, 20, 16}, "Negative tie or parent edge rounding");
    require(result.find(child)->border_box == Rect{-1, 1, 10, 7}, "Nested local positions or cumulative sizes");
    require(result.size == Size{50, 40}, "Root size was not rounded");
    compare(tree.compute(root, {}), logical);
    const auto& scaled = tree.compute(root, {}, PixelRounding{2});
    require(scaled.find(parent)->border_box.x == -1.5F, "DPI scaling not applied");
    require(scaled.find(child)->border_box.x == -1, "Nested scaled position");
    compare(tree.compute(root, {}), logical);
}
void shared_edges_and_origins() {
    LayoutTree tree;
    auto container = sized(5, 8);
    container.display = Display::Flex;
    const auto root = tree.create_node(container);
    auto item = sized(0, 8); item.flex_grow = 1;
    for (int i = 0; i < 6; ++i) tree.append_child(root, tree.create_node(item));
    for (const auto options : {PixelRounding{}, PixelRounding{1.25F, 0.2F, -0.3F}, PixelRounding{2}}) {
        const auto& result = tree.compute(root, {}, options);
        require(result.succeeded() && result.nodes.size() == 7, "Missing rounded nodes");
        if (options.origin_x == 0) {
            for (std::size_t i = 1; i + 1 < result.nodes.size(); ++i) {
                const auto& a = result.nodes[i].border_box;
                require(a.x + a.width == result.nodes[i + 1].border_box.x, "Shared edge split");
            }
        }
        const auto* storage = result.nodes.data();
        const auto& cached = tree.compute(root, {}, options);
        require(cached.cache_hit && cached.measurement_count == 0 && cached.layout_count == 0
            && storage == cached.nodes.data(), "Repeated rounded compute rebuilt output");
    }
    const auto& shifted = tree.compute(root, {}, PixelRounding{1, 0.5F, -0.5F});
    require(shifted.nodes.front().border_box.x == 0 && shifted.nodes.front().border_box.y == 0,
        "Origin leaked into root-relative position");
}
void failure_and_recovery() {
    LayoutTree tree;
    const auto root = tree.create_node({}, [](const MeasureRequest&) { return Measurement{{11.25F, 8.75F}}; });
    const auto saved = tree.compute(root, {}, PixelRounding{});
    for (const auto options : {PixelRounding{0}, PixelRounding{-1},
         PixelRounding{std::numeric_limits<Unit>::infinity()}, PixelRounding{1, std::nanf("")}}) {
        bool thrown = false;
        try { (void)tree.compute(root, {}, options); } catch (const std::invalid_argument&) { thrown = true; }
        require(thrown, "Invalid rounding accepted");
        compare(tree.compute(root, {}, PixelRounding{}), saved);
    }
    const auto& stable = tree.compute(root, {}, PixelRounding{});
    const auto* storage = stable.nodes.data();
    tree.set_measure(root, [](const MeasureRequest&) -> Measurement { throw std::runtime_error("host"); });
    bool thrown = false;
    try { (void)tree.compute(root, {}, PixelRounding{2}); } catch (const std::runtime_error&) { thrown = true; }
    require(thrown && stable.nodes.data() == storage, "Host exception replaced snapshot");
    compare(stable, saved);
    tree.set_measure(root, [](const MeasureRequest&) { return Measurement{{11.25F, 8.75F}}; });
    const auto& failed = tree.compute(root, {}, PixelRounding{std::numeric_limits<Unit>::max()});
    require(failed.error.code == LayoutErrorCode::NumericOverflow && failed.nodes.empty()
        && failed.grid_tracks.empty() && failed.size == Size{}, "Partial result after rounding overflow");
    LayoutOptions sparse;
    sparse.publish_nodes = false;
    sparse.rounding = PixelRounding{std::numeric_limits<Unit>::max()};
    const auto& sparse_failure = tree.compute(root, {}, sparse);
    require(sparse_failure.error.code == LayoutErrorCode::NumericOverflow && !tree.layout(root),
        "Sparse publication skipped numeric overflow checks");
    compare(tree.compute(root, {}, PixelRounding{}), saved);
    LayoutTree second;
    const auto other = second.create_node(sized(3.6F, 4.2F));
    LayoutBatch batch(2);
    std::array requests{LayoutRequest{&tree, root, {}, PixelRounding{2}},
        LayoutRequest{&second, other, {}, PixelRounding{}}};
    const auto results = batch.compute(requests, BatchExecution::Parallel);
    require(results[0].succeeded() && results[1].succeeded()
        && results[0].layout->size == Size{11.5F, 9} && results[1].layout->size == Size{4, 4}, "Batch rounding");
}
}
int main() {
    try { nested_and_negative(); shared_edges_and_origins(); failure_and_recovery(); return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
