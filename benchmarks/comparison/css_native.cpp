#include "css_fixture.hpp"

#include <axisly/css_tree.hpp>
#include <yoga/Yoga.h>

#include <array>
#include <charconv>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string_view>

namespace {
using namespace axisly;
using namespace axisly::bench::comparison;

class Engine {
public:
    virtual ~Engine() = default;
    virtual void calculate(Size) = 0;
    virtual void edit(std::size_t) = 0;
    virtual Rect rect(std::size_t) const = 0;
    virtual void check_order() const {}
    std::size_t measurements{};
    double consume(std::size_t count) const {
        return fingerprint(rect(0)) + fingerprint(rect(count / 2))
            + fingerprint(rect(count - 1));
    }
};
css::Length dimension(float v) {
    return v == -1 ? css::Length::automatic()
        : v == -2 ? css::Length::percent(1) : css::Length::length(v);
}
css::Style style(const Node& n) {
    css::Style s;
    s.display = n.display == 'G' ? css::Display::Grid
        : n.display == 'B' ? css::Display::Block : css::Display::Flex;
    s.box.size = {dimension(n.width), dimension(n.height)};
    s.box.min_size = {css::Length::length(0), css::Length::length(0)};
    s.flex_direction = n.direction == 'R' ? css::FlexDirection::Row : css::FlexDirection::Column;
    s.flex_wrap = n.wrap ? css::FlexWrap::Wrap : css::FlexWrap::NoWrap;
    s.flex_basis = dimension(n.basis);
    s.flex_grow = n.grow;
    s.flex_shrink = n.shrink;
    const auto padding = css::Length::length(n.padding), margin = css::Length::length(n.margin);
    s.box.padding = {padding, padding, padding, padding};
    s.box.margin = {margin, margin, margin, margin};
    s.gap = {css::Length::length(n.gap), css::Length::length(n.gap)};
    s.align_items = {css::ItemPosition::Stretch};
    s.justify_items = {css::ItemPosition::Stretch};
    s.align_content = {css::ContentPosition::Start};
    s.justify_content = {css::ContentPosition::Start};
    auto tracks = [](const auto& source, auto& target) {
        for (const auto t : source) {
            if (t.kind == 'F') target.emplace_back(css::TrackSizing::fixed(t.value));
            else if (t.kind == 'A') target.emplace_back(css::TrackSizing::automatic());
            else target.emplace_back(css::TrackSizing::minmax(css::TrackBreadth::length(0),
                css::TrackBreadth::fraction(t.value)));
        }
    };
    tracks(n.columns, s.grid.template_columns);
    tracks(n.rows, s.grid.template_rows);
    if (n.column) s.grid.column.start = css::GridLine::index(static_cast<std::int16_t>(n.column));
    if (n.row) s.grid.row.start = css::GridLine::index(static_cast<std::int16_t>(n.row));
    s.grid.column.end = css::GridLine::span(static_cast<std::uint16_t>(n.column_span));
    s.grid.row.end = css::GridLine::span(static_cast<std::uint16_t>(n.row_span));
    return s;
}
class AxislyCss final : public Engine {
    const Fixture& fixture_;
    css::LayoutTree tree_;
    std::vector<css::NodeId> nodes_;
    std::vector<std::size_t> revisions_;
    const css::LayoutResult* output_{};
public:
    explicit AxislyCss(const Fixture& f) : fixture_(f), revisions_(f.nodes.size()) {
        nodes_.reserve(f.nodes.size());
        for (std::size_t i = 0; i < f.nodes.size(); ++i) {
            const auto& n = f.nodes[i];
            css::MeasureFunction callback;
            if (n.measure) callback = [this, i](const css::MeasureRequest& r) {
                ++measurements;
                const auto size = measure(fixture_.nodes[i], revisions_[i],
                    r.known_content_width, r.known_content_height,
                    r.available_width.definite_value(),
                    r.available_width.kind == css::AvailableKind::MinContent);
                return css::Measurement{.content_size = size};
            };
            nodes_.push_back(tree_.create_node(style(n), std::move(callback)));
            if (n.parent >= 0) tree_.append_child(nodes_[static_cast<std::size_t>(n.parent)], nodes_.back());
        }
    }
    void calculate(Size size) override {
        measurements = 0;
        output_ = &tree_.compute(nodes_[0], {css::AvailableSpace::definite(size.width),
            size.height < 0 ? css::AvailableSpace::max_content()
                : css::AvailableSpace::definite(size.height)});
        if (!output_->succeeded()) throw std::runtime_error(
            (output_->error.code == css::LayoutErrorCode::LimitExceeded ? "LAYOUT_LIMIT: " : "")
            + output_->error.message + "; measurements=" + std::to_string(output_->measurement_count)
            + "; layouts=" + std::to_string(output_->layout_count));
        require(output_->nodes.size() == nodes_.size(), "Incomplete CSS output");
        require(output_->measurement_count == measurements, "CSS measurement count mismatch");
    }
    void edit(std::size_t index) override {
        ++revisions_[index];
        tree_.mark_dirty(nodes_[index]);
    }
    Rect rect(std::size_t i) const override { return output_->nodes[i].border_box; }
    void check_order() const override {
        for (std::size_t i = 0; i < nodes_.size(); ++i)
            require(output_->nodes[i].node == nodes_[i], "CSS output order differs from fixture");
    }
};
class Yoga final : public Engine {
    struct Context { Yoga* owner{}; std::size_t index{}, revision{}; };
    const Fixture& fixture_;
    YGConfigRef config_{};
    std::vector<YGNodeRef> nodes_;
    std::vector<Context> contexts_;
    static YGSize callback(YGNodeConstRef node, float w, YGMeasureMode wm,
                           float h, YGMeasureMode hm) {
        const auto& c = *static_cast<const Context*>(YGNodeGetContext(node));
        ++c.owner->measurements;
        const auto size = measure(c.owner->fixture_.nodes[c.index], c.revision,
            wm == YGMeasureModeExactly ? std::optional<float>(w) : std::nullopt,
            hm == YGMeasureModeExactly ? std::optional<float>(h) : std::nullopt,
            wm == YGMeasureModeAtMost ? std::optional<float>(w) : std::nullopt, false);
        return {size.width, hm == YGMeasureModeAtMost ? std::min(size.height, h) : size.height};
    }
public:
    explicit Yoga(const Fixture& f) : fixture_(f), contexts_(f.nodes.size()) {
        for (const auto& n : f.nodes) require(n.display == 'F', "Yoga requires Flex-only fixture");
        config_ = YGConfigNew();
        YGConfigSetUseWebDefaults(config_, true);
        YGConfigSetPointScaleFactor(config_, 0);
        nodes_.reserve(f.nodes.size());
        for (std::size_t i = 0; i < f.nodes.size(); ++i) {
            const auto& n = f.nodes[i];
            const auto node = YGNodeNewWithConfig(config_);
            nodes_.push_back(node);
            YGNodeStyleSetBoxSizing(node, YGBoxSizingBorderBox);
            if (n.width == -2) YGNodeStyleSetWidthPercent(node, 100);
            else if (n.width == -1) YGNodeStyleSetWidthAuto(node);
            else YGNodeStyleSetWidth(node, n.width);
            if (n.height == -2) YGNodeStyleSetHeightPercent(node, 100);
            else if (n.height == -1) YGNodeStyleSetHeightAuto(node);
            else YGNodeStyleSetHeight(node, n.height);
            YGNodeStyleSetMinWidth(node, 0);
            YGNodeStyleSetMinHeight(node, 0);
            if (n.basis == -2) YGNodeStyleSetFlexBasisPercent(node, 100);
            else if (n.basis == -1) YGNodeStyleSetFlexBasisAuto(node);
            else YGNodeStyleSetFlexBasis(node, n.basis);
            YGNodeStyleSetFlexGrow(node, n.grow);
            YGNodeStyleSetFlexShrink(node, n.shrink);
            YGNodeStyleSetFlexDirection(node, n.direction == 'R' ? YGFlexDirectionRow : YGFlexDirectionColumn);
            YGNodeStyleSetFlexWrap(node, n.wrap ? YGWrapWrap : YGWrapNoWrap);
            YGNodeStyleSetAlignItems(node, YGAlignStretch);
            YGNodeStyleSetAlignContent(node, YGAlignFlexStart);
            YGNodeStyleSetJustifyContent(node, YGJustifyFlexStart);
            YGNodeStyleSetPadding(node, YGEdgeAll, n.padding);
            YGNodeStyleSetMargin(node, YGEdgeAll, n.margin);
            YGNodeStyleSetGap(node, YGGutterAll, n.gap);
            if (n.measure) {
                contexts_[i] = {this, i, 0};
                YGNodeSetContext(node, &contexts_[i]);
                YGNodeSetMeasureFunc(node, callback);
            }
            if (n.parent >= 0) {
                const auto parent = nodes_[static_cast<std::size_t>(n.parent)];
                YGNodeInsertChild(parent, node, YGNodeGetChildCount(parent));
            }
        }
    }
    ~Yoga() override { YGNodeFreeRecursive(nodes_[0]); YGConfigFree(config_); }
    void edit(std::size_t i) override { ++contexts_[i].revision; YGNodeMarkDirty(nodes_[i]); }
    void calculate(Size size) override {
        measurements = 0;
        YGNodeCalculateLayout(nodes_[0], size.width, size.height < 0 ? YGUndefined : size.height, YGDirectionLTR);
    }
    Rect rect(std::size_t i) const override {
        const auto node = nodes_[i];
        Rect r{YGNodeLayoutGetLeft(node), YGNodeLayoutGetTop(node),
            YGNodeLayoutGetWidth(node), YGNodeLayoutGetHeight(node)};
        for (auto parent = fixture_.nodes[i].parent; parent >= 0;
             parent = fixture_.nodes[static_cast<std::size_t>(parent)].parent) {
            r.x += YGNodeLayoutGetLeft(nodes_[static_cast<std::size_t>(parent)]);
            r.y += YGNodeLayoutGetTop(nodes_[static_cast<std::size_t>(parent)]);
        }
        return r;
    }
};
std::size_t positive(const char* text) {
    const std::string_view input(text);
    std::size_t value{};
    const auto [end, error] = std::from_chars(input.data(), input.data() + input.size(), value);
    require(error == std::errc{} && end == input.data() + input.size()
        && value > 0 && value <= 100'000, "Invalid iteration count");
    return value;
}
void values(const std::vector<double>& data) {
    for (std::size_t i = 0; i < data.size(); ++i) {
        if (i) std::cout << ',';
        std::cout << data[i];
    }
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 6, "Usage: css_native axisly_css|yoga fixture [verify_]cold|resize|same|leaf_edit iterations samples");
        const std::string engine(argv[1]);
        std::string mode(argv[3]);
        const bool verify = mode.starts_with("verify_");
        if (verify) mode.erase(0, 7);
        require(mode == "cold" || mode == "resize" || mode == "same" || mode == "leaf_edit", "Unknown mode");
        require(engine == "axisly_css" || engine == "yoga", "Unknown engine");
        const auto f = read_fixture(argv[2]);
        const auto iterations = positive(argv[4]), samples = positive(argv[5]);
        require(mode != "leaf_edit" || !f.measured.empty(), "Edit requires measured leaves");
        auto create = [&]() -> std::unique_ptr<Engine> {
            if (engine == "axisly_css") return std::make_unique<AxislyCss>(f);
            return std::make_unique<Yoga>(f);
        };
        auto step = [&](Engine& e, std::size_t frame) {
            if (mode == "leaf_edit") e.edit(f.measured[(frame * 17) % f.measured.size()]);
            e.calculate(mode == "same" || mode == "leaf_edit" ? f.sizes.back() : f.sizes[frame % f.sizes.size()]);
        };
        std::cout << std::setprecision(12);
        if (verify) {
            auto e = create();
            if (mode != "cold") e->calculate(f.sizes.back());
            std::cout << "{\"frames\":[";
            for (std::size_t frame = 0; frame < iterations; ++frame) {
                if (mode == "cold") e = create();
                step(*e, frame);
                e->check_order();
                if (frame) std::cout << ',';
                std::cout << '[';
                for (std::size_t i = 0; i < f.nodes.size(); ++i) {
                    if (i) std::cout << ',';
                    const auto r = e->rect(i);
                    std::cout << '[' << r.x << ',' << r.y << ',' << r.width << ',' << r.height << ']';
                }
                std::cout << ']';
            }
            std::cout << "]}\n";
            return 0;
        }
        for (const auto size : f.sizes) { auto e = create(); e->calculate(size); }
        std::vector<double> times, checksums, counts;
        for (std::size_t sample = 0; sample < samples; ++sample) {
            auto e = create();
            if (mode != "cold") e->calculate(f.sizes.back());
            double elapsed{}, checksum{}, measurements{};
            for (std::size_t frame = 0; frame < iterations; ++frame) {
                if (mode == "cold") e = create();
                const auto start = std::chrono::steady_clock::now();
                step(*e, frame);
                const auto consumed = e->consume(f.nodes.size());
                const auto end = std::chrono::steady_clock::now();
                elapsed += std::chrono::duration<double, std::micro>(end - start).count();
                checksum += consumed;
                measurements += static_cast<double>(e->measurements);
            }
            times.push_back(elapsed / static_cast<double>(iterations));
            checksums.push_back(checksum);
            counts.push_back(measurements / static_cast<double>(iterations));
        }
        std::cout << "{\"build\":\"" << AXISLY_BENCH_CONFIG
            << "\",\"axisly_version\":\"" << AXISLY_BENCH_VERSION << "\",\"samples_us\":[";
        values(times); std::cout << "],\"checksums\":["; values(checksums);
        std::cout << "],\"measurements\":["; values(counts); std::cout << "]}\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
