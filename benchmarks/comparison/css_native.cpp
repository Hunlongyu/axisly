#include "css_axisly_adapter.hpp"

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
        require(argc == 6, "Usage: css_native axisly_css|yoga fixture [verify_]cold|resize|same|leaf_edit|build iterations samples");
        const std::string engine(argv[1]);
        std::string mode(argv[3]);
        const bool verify = mode.starts_with("verify_");
        if (verify) mode.erase(0, 7);
        require(mode == "cold" || mode == "resize" || mode == "same" || mode == "leaf_edit" || mode == "build", "Unknown mode");
        require(engine == "axisly_css" || engine == "axisly_css_reuse" || engine == "axisly_css_retained" || engine == "yoga", "Unknown engine");
        const bool axisly_engine = engine != "yoga";
        const auto memory = engine == "axisly_css_reuse" ? std::make_shared<css::LayoutMemory>() : nullptr;
        const auto f = read_fixture(argv[2]);
        const auto iterations = positive(argv[4]), samples = positive(argv[5]);
        require(mode != "leaf_edit" || !f.measured.empty(), "Edit requires measured leaves");
        // Taffy's fixture reader already produces ready-to-install Styles.
        // Prepare this API's style values at the same boundary. Construction
        // timing still includes copying them into each newly created node.
        std::vector<css::Style> styles;
        if (axisly_engine) {
            styles.reserve(f.nodes.size());
            for (const auto& node : f.nodes) styles.push_back(style(node));
        }
        auto create = [&]() -> std::unique_ptr<Engine> {
            if (axisly_engine) return std::make_unique<AxislyCss>(f, styles, memory, engine == "axisly_css_retained");
            return std::make_unique<Yoga>(f);
        };
        auto step = [&](Engine& e, std::size_t frame) {
            if (mode == "leaf_edit") e.edit(f.measured[(frame * 17) % f.measured.size()]);
            e.calculate(mode == "same" || mode == "leaf_edit" ? f.sizes.back() : f.sizes[frame % f.sizes.size()]);
        };
        std::cout << std::setprecision(12);
        if (verify) {
            auto e = create();
            if (mode != "cold" && mode != "build") e->calculate(f.sizes.back());
            std::cout << "{\"frames\":[";
            for (std::size_t frame = 0; frame < iterations; ++frame) {
                if (mode == "cold" || mode == "build") e = create();
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
        if (mode == "build") {
            // Construction belongs inside this timer for every engine. The
            // previous tree has already been destroyed before the next frame.
            for (std::size_t sample = 0; sample < samples; ++sample) {
                double elapsed{}, checksum{}, measurements{};
                for (std::size_t frame = 0; frame < iterations; ++frame) {
                    const auto start = std::chrono::steady_clock::now();
                    auto e = create();
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
        } else {
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
        }
        std::cout << "{\"build\":\"" << AXISLY_BENCH_CONFIG
            << "\",\"axisly_version\":\"" << AXISLY_BENCH_VERSION << "\",\"samples_us\":[";
        values(times); std::cout << "],\"checksums\":["; values(checksums);
        std::cout << "],\"measurements\":["; values(counts); std::cout << ']';
        if (memory) {
            const auto stats = memory->stats();
            std::cout << ",\"memory\":{\"cache_limit\":" << memory->cache_limit()
                << ",\"cached_bytes\":" << stats.cached_bytes << ",\"live_bytes\":" << stats.live_bytes
                << ",\"peak_live_bytes\":" << stats.peak_live_bytes
                << ",\"upstream_allocations\":" << stats.upstream_allocations
                << ",\"reuse_count\":" << stats.reuse_count << '}';
        }
        std::cout << "}\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
