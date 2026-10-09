// Same-fixture before/after benchmark. Timing, allocations and work counters
// are separate executables/builds; construction is outside the measured step.
#include <axisly/css_source.hpp>
#include "../src/css/grid_metrics.hpp"
#ifdef AXISLY_COUNT_ALLOCATIONS
#include "allocation_probe.hpp"
#endif

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

namespace {
using namespace axisly;
using namespace axisly::css;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
struct Cell {
    Style style;
    std::optional<NodeId> parent;
    std::vector<NodeId> children;
};
struct Source final : LayoutSource {
    std::vector<Cell> cells;
    std::optional<LayoutNodeView> node(NodeId id) const override {
        if (!id.value || id.value > cells.size()) return std::nullopt;
        const auto& cell = cells[static_cast<std::size_t>(id.value - 1)];
        return LayoutNodeView{cell.style, cell.children, cell.parent};
    }
};
struct Provider final : MeasureProvider {
    Measurement measure(NodeId, const Style&, void*, const MeasureRequest& request) const override {
        return {{request.known_content_width.value_or(
            request.available_width.kind == AvailableKind::MinContent ? 160.0F : 480.0F),
            request.known_content_height.value_or(12)}};
    }
};

class Scene {
public:
    Scene(std::string_view name, std::size_t count) : name_(name), source_mode_(name == "style_source") {
        Style root;
        root.display = Display::Grid;
        root.box.size.width = Length::percent(1);
        root.grid.template_columns.assign(16, TrackSizing::fixed(16));
        root.grid.auto_rows = {TrackSizing::fixed(16)};
        if (name == "auto_column") {
            root.grid.auto_flow = GridAutoFlow::Column;
            root.grid.template_columns.clear();
            root.grid.template_rows.assign(16, TrackSizing::fixed(16));
            root.grid.auto_columns = {TrackSizing::fixed(16)};
        } else if (name == "auto_dense") {
            root.grid.auto_flow = GridAutoFlow::RowDense;
        } else if (name == "auto_fit" || name == "repeat_nested") {
            root.grid.template_columns = {GridRepeat{
                name == "auto_fit" ? RepeatKind::AutoFit : RepeatKind::AutoFill,
                1, {TrackSizing::fixed(16)}, {}}};
            if (name == "auto_fit") width_ = static_cast<Unit>(count * 32);
        } else if (name == "span_limits") {
            root.box.size.width = Length::automatic();
            root.grid.template_columns.assign(std::max<std::size_t>(16, std::min<std::size_t>(count, 512)),
                TrackSizing::minmax(TrackBreadth::automatic(), TrackBreadth::max_content()));
            root.grid.auto_rows = {TrackSizing::automatic()};
        } else if (name == "float") {
            root.display = Display::Block;
            root.grid = {};
        }
        auto parent = add(root);
        if (name == "repeat_nested") {
            source_.cells[0].style = {};
            source_.cells[0].style.display = Display::Flex;
            source_.cells[0].style.box.size.width = Length::percent(1);
            root.box.size.width = Length::automatic();
            root.flex_grow = 1;
            root.flex_shrink = 1;
            parent = add(root, parent);
        }
        for (std::size_t i = 0; i < count; ++i) {
            auto item_parent = parent;
            if (name == "style_nested" || source_mode_) {
                Style panel;
                panel.display = Display::Block;
                item_parent = add(panel, parent);
            }
            Style item;
            item.box.size = {Length::length(8), Length::length(8)};
            if (name == "auto_spans") {
                item.grid.column.end = GridLine::span(static_cast<std::uint16_t>(1 + i % 4));
                item.grid.row.end = GridLine::span(static_cast<std::uint16_t>(1 + i % 3));
            } else if (name == "span_limits") {
                item.box.size = {};
                const auto span = std::min<std::size_t>(2 + i % 63, root.grid.template_columns.size());
                item.grid.column.start = GridLine::index(static_cast<std::int16_t>(
                    1 + i % (root.grid.template_columns.size() - span + 1)));
                item.grid.column.end = GridLine::span(static_cast<std::uint16_t>(span));
                item.grid.row.start = GridLine::index(static_cast<std::int16_t>(1 + i % 8));
            } else if (name == "float") {
                item.float_side = i % 2 ? FloatSide::Right : FloatSide::Left;
                item.box.size.height = Length::length(static_cast<Unit>(8 + i % 5));
            }
            leaves_.push_back(add(item, item_parent));
        }
        if (source_mode_) {
            session_ = std::make_unique<LayoutSession>(source_);
        } else {
            tree_ = std::make_unique<LayoutTree>();
            for (const auto& cell : source_.cells) ids_.push_back(tree_->create_node(cell.style));
            for (std::size_t i = 0; i < source_.cells.size(); ++i) {
                std::vector<NodeId> children;
                for (const auto id : source_.cells[i].children) children.push_back(ids_[slot(id)]);
                if (!children.empty()) tree_->set_children(ids_[i], children);
            }
        }
    }
    const LayoutResult& step(std::string_view mode, std::size_t frame, bool full = false) {
        if (mode == "edit") {
            const auto id = leaves_[frame % leaves_.size()];
            auto& style = source_.cells[slot(id)].style;
            // Every visit changes the value, even when frames wrap.
            const auto next = style.box.size.height == Length::length(8) ? 10.0F : 8.0F;
            style.box.size.height = Length::length(next);
            if (session_) session_->mark_dirty(id);
            else tree_->set_style(ids_[slot(id)], style);
        } else if (mode == "content") {
            if (session_) session_->mark_dirty(leaves_[frame % leaves_.size()]);
            else tree_->mark_dirty(ids_[slot(leaves_[frame % leaves_.size()])]);
        }
        const auto width = width_ + (mode == "resize" ? static_cast<Unit>(frame % 17) : 0);
        const AvailableSize available{
            name_ == "span_limits" ? AvailableSpace::max_content() : AvailableSpace::definite(width),
            AvailableSpace::max_content()};
        LayoutOptions options;
        options.publish_nodes = full;
        options.collect_grid_tracks = full;
        if (name_ == "span_limits") options.measure_provider = &provider_;
        const auto& result = session_ ? session_->compute({1}, available, options)
            : tree_->compute(ids_[0], available, options);
        if (!result.succeeded()) throw std::runtime_error(result.error.message);
        return result;
    }
    double consume(const LayoutResult& result) const {
        double sum = result.size.width + result.size.height;
        for (const auto i : {std::size_t{0}, leaves_.size() / 2, leaves_.size() - 1}) {
            const auto id = leaves_[i];
            const auto value = session_ ? session_->layout(id) : tree_->layout(ids_[slot(id)]);
            require(bool(value), "Missing benchmark geometry");
            sum += value->border_box.x + value->border_box.y + value->border_box.width + value->border_box.height;
        }
        return sum;
    }
    void dump(const LayoutResult& result) const {
        std::unordered_map<std::uint64_t, std::size_t> indices;
        for (std::size_t i = 0; i < source_.cells.size(); ++i) indices.emplace(
            session_ ? i + 1 : ids_[i].value, i);
        const auto value = [](Unit v) { std::cout << ',' << std::bit_cast<std::uint32_t>(v); };
        const auto rect = [&](Rect v) { value(v.x); value(v.y); value(v.width); value(v.height); };
        const auto insets = [&](Insets v) { value(v.left); value(v.top); value(v.right); value(v.bottom); };
        std::cout << "size"; value(result.size.width); value(result.size.height); std::cout << '\n';
        for (const auto& node : result.nodes) {
            std::cout << "node," << indices.at(node.node.value);
            rect(node.border_box); rect(node.content_box); insets(node.margin); insets(node.padding);
            insets(node.border); rect(node.scrollable_overflow_rect);
            value(node.scrollbar.width); value(node.scrollbar.height);
            std::cout << ',' << node.first_baseline.has_value(); value(node.first_baseline.value_or(0));
            std::cout << ',' << node.last_baseline.has_value(); value(node.last_baseline.value_or(0));
            std::cout << '\n';
        }
        for (const auto& grid : result.grid_tracks) {
            std::cout << "grid," << indices.at(grid.node.value) << ',' << grid.row_origin << ',' << grid.column_origin;
            std::cout << ',' << grid.explicit_rows << ',' << grid.explicit_columns << '\n';
            for (const auto& axis : {grid.rows, grid.columns}) {
                std::cout << "tracks"; for (auto v : axis) value(v); std::cout << '\n';
            }
            for (const auto& axis : {grid.row_positions, grid.column_positions}) {
                std::cout << "positions"; for (auto v : axis) { value(v.start); value(v.end); } std::cout << '\n';
            }
            for (const auto& item : grid.items) std::cout << "item," << indices.at(item.node.value)
                << ',' << item.row_start << ',' << item.row_end << ',' << item.column_start << ',' << item.column_end << '\n';
        }
    }
private:
    static std::size_t slot(NodeId id) { return static_cast<std::size_t>(id.value - 1); }
    NodeId add(const Style& style, std::optional<NodeId> parent = {}) {
        const NodeId id{source_.cells.size() + 1};
        source_.cells.push_back({style, parent, {}});
        if (parent) source_.cells[slot(*parent)].children.push_back(id);
        return id;
    }
    std::string name_;
    bool source_mode_{};
    Unit width_{512};
    Source source_;
    Provider provider_;
    std::vector<NodeId> ids_, leaves_;
    std::unique_ptr<LayoutTree> tree_;
    std::unique_ptr<LayoutSession> session_;
};
constexpr std::array cases{"auto_row", "auto_column", "auto_dense", "auto_spans", "style_nested",
    "style_source", "auto_fit", "repeat_nested", "span_limits", "float"};
std::size_t number(std::string_view value) {
    std::size_t n{};
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), n);
    require(error == std::errc{} && end == value.data() + value.size() && n > 0 && n <= 10'000,
        "Expected an integer in 1..10000");
    return n;
}
void run(std::string_view name, std::string_view mode, std::size_t count,
    std::size_t iterations, std::size_t samples, bool verify, bool full) {
    if (verify) {
        Scene scene(name, count);
        for (std::size_t frame = 0; frame < 4; ++frame) scene.dump(scene.step(mode, frame, true));
        return;
    }
    for (std::size_t sample = 0; sample < samples; ++sample) {
        auto scene = std::make_unique<Scene>(name, count);
        if (mode != "cold") for (std::size_t frame = 0; frame < 4; ++frame) scene->step(mode, frame, full);
        double micros = 0, checksum = 0;
        std::size_t allocations = 0, bytes = 0, peak = 0;
        detail::GridMetrics work;
        for (std::size_t frame = 0; frame < iterations; ++frame) {
            if (mode == "cold" && frame) scene = std::make_unique<Scene>(name, count);
#ifdef AXISLY_ENABLE_GRID_METRICS
            detail::grid_metrics = {};
#endif
#ifdef AXISLY_COUNT_ALLOCATIONS
            axisly::bench::begin_allocations();
#endif
            const auto start = std::chrono::steady_clock::now();
            checksum += scene->consume(scene->step(mode, frame + 4, full));
            micros += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
#ifdef AXISLY_COUNT_ALLOCATIONS
            const auto stats = axisly::bench::end_allocations();
            allocations += stats.allocations; bytes += stats.requested_bytes;
            peak = std::max(peak, stats.peak_live_bytes);
#endif
#ifdef AXISLY_ENABLE_GRID_METRICS
            const auto& m = detail::grid_metrics;
            work.structure_builds += m.structure_builds; work.structure_hits += m.structure_hits;
            work.placement_probes += m.placement_probes; work.occupied_visits += m.occupied_visits;
            work.auto_fit_visits += m.auto_fit_visits; work.limit_visits += m.limit_visits;
#endif
        }
        const auto divisor = static_cast<double>(iterations);
        std::cout << name << ',' << mode << ',' << count << ',' << sample << ',' << full << ','
            << micros / divisor << ',' << allocations / divisor << ',' << bytes / divisor << ',' << peak << ','
            << work.structure_builds / divisor << ',' << work.structure_hits / divisor << ','
            << work.placement_probes / divisor << ',' << work.occupied_visits / divisor << ','
            << work.auto_fit_visits / divisor << ',' << work.limit_visits / divisor << ',' << checksum << '\n';
    }
}
} // namespace
int main(int argc, char** argv) {
    try {
        std::string name = "auto_row", mode = "cold";
        std::size_t count = 1000, iterations = 3, samples = 7;
        bool smoke = false, verify = false, full = false;
        for (int i = 1; i < argc; ++i) {
            const std::string_view arg = argv[i];
            if (arg == "--smoke") smoke = true;
            else if (arg == "--verify") verify = true;
            else if (arg == "--full") full = true;
            else {
                require(i + 1 < argc, "Missing argument");
                const std::string_view value = argv[++i];
                if (arg == "--case") name = value;
                else if (arg == "--mode") mode = value;
                else if (arg == "--count") count = number(value);
                else if (arg == "--iterations") iterations = number(value);
                else if (arg == "--samples") samples = number(value);
                else throw std::runtime_error("Unknown argument");
            }
        }
        require(std::find(cases.begin(), cases.end(), name) != cases.end(), "Unknown case");
        require(mode == "cold" || mode == "resize" || mode == "edit" || mode == "content" || mode == "same", "Unknown mode");
#ifdef AXISLY_COUNT_ALLOCATIONS
        axisly::bench::allocation_self_test();
#endif
        std::cout << std::setprecision(17);
        if (!verify) std::cout << "case,mode,count,sample,full,us,allocations,bytes,peak_incremental_bytes,builds,hits,probes,occupied_visits,fit_visits,limit_visits,checksum\n";
        if (smoke) for (const auto* scenario : cases) {
            for (const auto* step : {"cold", "edit", "resize", "content", "same"}) run(scenario, step, 32, 1, 1, false, true);
        } else run(name, mode, count, iterations, samples, verify, full);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
