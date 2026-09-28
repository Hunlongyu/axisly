#include <axisly/axisly.hpp>
#include <yoga/Yoga.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using axisly::Rect;
using axisly::Size;
struct Track { char kind{}; float value{}; };
struct Item { unsigned column{}, row{}, column_span{}, row_span{}; Size preferred; };
struct NestedNode { int parent{}; char kind{}, sizing{}; float value{}; };
struct Fixture {
    std::string name, kind;
    bool measured{};
    float padding{}, gap{};
    std::vector<Track> columns, rows;
    std::vector<Item> items;
    std::vector<NestedNode> nested;
    std::vector<Size> sizes;
};
void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
std::size_t positive(const char* arg) {
    const std::string text(arg);
    std::size_t value{};
    const auto result = std::from_chars(text.data(), text.data()+text.size(), value);
    require(result.ec == std::errc{} && result.ptr == text.data()+text.size() && value > 0 && value <= 1000000,
            "Invalid benchmark count");
    return value;
}
Fixture read(const char* path) {
    std::ifstream in(path);
    Fixture f;
    std::string magic;
    std::size_t cols{}, rows{}, count{}, frames{};
    in >> magic;
    if (magic == "FLCMP1") {
        in >> f.name >> f.kind >> count >> frames >> f.padding >> f.gap;
        require(in.good() && f.kind == "nested" && count >= 2 && count <= 10000
            && frames > 1 && frames <= 100 && std::isfinite(f.padding) && f.padding >= 0
            && std::isfinite(f.gap) && f.gap >= 0, "Invalid nested fixture header");
        f.nested.resize(count);
        for (std::size_t i = 0; i < count; ++i) {
            auto& node = f.nested[i];
            in >> node.parent >> node.kind >> node.sizing >> node.value;
            require(!in.fail() && (node.kind == 'R' || node.kind == 'C' || node.kind == 'L')
                && (node.sizing == 'F' || node.sizing == 'W') && std::isfinite(node.value)
                && node.value >= 0 && (i == 0 ? node.parent == -1 && node.kind != 'L'
                    : node.parent >= 0 && static_cast<std::size_t>(node.parent) < i),
                "Invalid nested fixture node");
        }
        for (std::size_t i = 1; i < count; ++i)
            require(f.nested[static_cast<std::size_t>(f.nested[i].parent)].kind != 'L',
                "Nested leaf has children");
        f.items.resize(count - 1);
        f.sizes.resize(frames);
        for (auto& size : f.sizes) in >> size.width >> size.height;
        require(!in.fail(), "Truncated nested fixture");
        std::string extra;
        require(!(in >> extra), "Trailing nested fixture data");
        return f;
    }
    in >> f.name >> f.kind >> cols >> rows >> count >> frames >> f.measured >> f.padding >> f.gap;
    require(in.good() && magic == "GLCMP1" && cols > 0 && cols <= 1000 && rows > 0 && rows <= 1000 &&
        count > 0 && count <= 100000 && frames > 1 && frames <= 100 &&
        (f.kind == "row" || f.kind == "column" || f.kind == "grid"), "Invalid fixture header");
    auto tracks = [&in](std::size_t n) {
        std::vector<Track> tracks(n);
        for (auto& t : tracks) {
            in >> t.kind >> t.value;
            require((t.kind == 'F' || t.kind == 'W' || t.kind == 'C') && std::isfinite(t.value) && t.value >= 0 &&
                (t.kind != 'W' || t.value >= 1), "Invalid fixture track");
        }
        return tracks;
    };
    f.columns = tracks(cols);
    f.rows = tracks(rows);
    f.items.resize(count);
    for (auto& item : f.items) {
        in >> item.column >> item.row >> item.column_span >> item.row_span >> item.preferred.width >> item.preferred.height;
        require(item.column < cols && item.row < rows && item.column_span > 0 && item.row_span > 0 &&
            item.column_span <= cols-item.column && item.row_span <= rows-item.row, "Invalid fixture placement");
    }
    f.sizes.resize(frames);
    for (auto& size : f.sizes) in >> size.width >> size.height;
    require(!in.fail(), "Truncated fixture");
    std::string extra;
    require(!(in >> extra), "Trailing fixture data");
    return f;
}
double fingerprint(Rect r) {
    return r.x + static_cast<double>(r.y)*0.5 + static_cast<double>(r.width)*0.25 + static_cast<double>(r.height)*0.125;
}
class Engine {
public:
    virtual ~Engine() = default;
    virtual void calculate(Size) = 0;
    virtual Rect rect(std::size_t) const = 0;
    virtual void release_result() {} // Retire host-owned output before the next layout.
    std::size_t measurements{};
    double consume(std::size_t count) const {
        return fingerprint(rect(0)) + fingerprint(rect(count/2)) + fingerprint(rect(count-1));
    }
};
class Axisly final : public Engine {
    const Fixture& fixture;
    axisly::GridSpec spec;
    std::vector<axisly::GridItem> items;
    axisly::GridResult result;
    std::unique_ptr<axisly::GridCache> cache;
    const axisly::GridResult* output{};
    static axisly::MeasureResult measure(axisly::ItemId id, const axisly::MeasureInput&, void* context) {
        auto& self = *static_cast<Axisly*>(context);
        ++self.measurements;
        const auto size = self.fixture.items[static_cast<std::size_t>(id)].preferred;
        return {size, size};
    }
public:
    explicit Axisly(const Fixture& f, bool cached = false) : fixture(f) {
        auto track = [](Track t) {
            if (t.kind == 'F') return axisly::Track::fixed(t.value);
            if (t.kind == 'W') return axisly::Track::weighted(t.value);
            return axisly::Track::content();
        };
        for (auto t : f.columns) spec.columns.push_back(track(t));
        for (auto t : f.rows) spec.rows.push_back(track(t));
        spec.padding = {f.padding,f.padding,f.padding,f.padding};
        spec.column_gap = spec.row_gap = f.gap;
        items.reserve(f.items.size());
        for (const auto& item : f.items) items.push_back({.id = items.size(), .column = item.column, .row = item.row,
            .column_span = item.column_span, .row_span = item.row_span, .preferred_size = item.preferred});
        if (cached) {
            cache = std::make_unique<axisly::GridCache>(std::move(spec), items);
            cache->set_measure(fixture.measured ? measure : nullptr, this);
            std::vector<axisly::GridItem>{}.swap(items);
        }
    }
    void calculate(Size size) override {
        measurements = 0;
        if (cache) {
            const auto call = cache->compute(size);
            output = &call.result;
            require(call.measurement_count == measurements, "Cached callback count mismatch");
        } else {
            result = axisly::compute_grid(spec,items,size,fixture.measured ? measure : nullptr,this);
            output = &result;
        }
        require(output->succeeded(), "axisly failed");
    }
    Rect rect(std::size_t index) const override { return output->items[index].rect; }
    // Cached output belongs to the cache. Its replacement on a miss is timed.
    void release_result() override { if (!cache) result = {}; }
};
class AxislyFlex final : public Engine {
    const Fixture& fixture;
    axisly::FlexSpec spec;
    std::vector<axisly::FlexItem> items;
    axisly::FlexResult result;
    static axisly::FlexMeasureResult measure(axisly::ItemId id, const axisly::FlexMeasureInput&, void* context) {
        auto& self = *static_cast<AxislyFlex*>(context);
        ++self.measurements;
        const auto size = self.fixture.items[static_cast<std::size_t>(id)].preferred;
        return {size, size, size};
    }
public:
    explicit AxislyFlex(const Fixture& f) : fixture(f) {
        require(f.kind == "row" || f.kind == "column", "Flex needs single-axis fixture");
        const bool row = f.kind == "row";
        require(row ? f.rows.size() == 1 : f.columns.size() == 1, "Invalid flex fixture");
        spec.direction = row ? axisly::FlexDirection::Row : axisly::FlexDirection::Column;
        spec.align_items.value = axisly::FlexAlign::FlexStart;
        spec.padding = {f.padding, f.padding, f.padding, f.padding};
        spec.column_gap = spec.row_gap = f.gap;
        items.reserve(f.items.size());
        for (const auto& source : f.items) {
            require(source.column_span == 1 && source.row_span == 1, "Flex fixture cannot span");
            const auto main = row ? f.columns[source.column] : f.rows[source.row];
            const auto cross = row ? f.rows[0] : f.columns[0];
            require(cross.kind == 'F', "Unsupported flex cross axis");
            axisly::FlexItem item;
            item.id = items.size();
            item.shrink = 0;
            item.minimum_width = item.minimum_height = axisly::FlexLength::fixed(0);
            if (row) item.height = axisly::FlexLength::fixed(cross.value);
            else item.width = axisly::FlexLength::fixed(cross.value);
            if (main.kind == 'F') item.basis = axisly::FlexLength::fixed(main.value);
            else if (main.kind == 'W') {
                item.basis = axisly::FlexLength::fixed(0);
                item.grow = main.value;
            }
            item.min_content = item.max_content = item.preferred_size = source.preferred;
            items.push_back(item);
        }
    }
    void calculate(Size size) override {
        measurements = 0;
        result = axisly::compute_flex(spec, items, size, fixture.measured ? measure : nullptr, this);
        require(result.succeeded(), "Axisly Flex failed");
        require(result.measurement_count == measurements, "Flex callback count mismatch");
    }
    Rect rect(std::size_t index) const override { return result.items[index].rect; }
    void release_result() override { result = {}; }
};
class AxislyNested final : public Engine {
    const Fixture& fixture;
    std::vector<std::vector<std::size_t>> children;
    std::vector<axisly::FlexSpec> specs;
    std::vector<std::vector<axisly::FlexItem>> node_items;
    std::vector<Rect> rectangles;
public:
    explicit AxislyNested(const Fixture& f) : fixture(f), children(f.nested.size()),
        specs(f.nested.size()), node_items(f.nested.size()),
        rectangles(f.nested.size()) {
        require(f.kind == "nested", "Expected nested fixture");
        for (std::size_t i = 1; i < f.nested.size(); ++i)
            children[static_cast<std::size_t>(f.nested[i].parent)].push_back(i);
        for (std::size_t parent = 0; parent < children.size(); ++parent) {
            auto& spec = specs[parent];
            spec.direction = fixture.nested[parent].kind == 'R'
                ? axisly::FlexDirection::Row : axisly::FlexDirection::Column;
            spec.padding = {f.padding, f.padding, f.padding, f.padding};
            spec.column_gap = spec.row_gap = f.gap;
            auto& items = node_items[parent];
            items.reserve(children[parent].size());
            for (auto child : children[parent]) {
                const auto& node = f.nested[child];
                axisly::FlexItem item;
                item.id = child;
                item.shrink = 0;
                item.minimum_width = item.minimum_height = axisly::FlexLength::fixed(0);
                item.basis = axisly::FlexLength::fixed(node.sizing == 'F' ? node.value : 0);
                if (node.sizing == 'W') item.grow = node.value;
                items.push_back(item);
            }
        }
    }
    void calculate(Size size) override {
        measurements = 0;
        rectangles[0] = {0, 0, size.width, size.height};
        for (std::size_t parent = 0; parent < children.size(); ++parent) {
            if (children[parent].empty()) continue;
            const auto& outer = rectangles[parent];
            auto result = axisly::compute_flex(specs[parent], node_items[parent],
                {outer.width, outer.height});
            require(result.succeeded(), "Axisly nested Flex failed");
            for (std::size_t i = 0; i < children[parent].size(); ++i) {
                auto rect = result.items[i].rect;
                rect.x += outer.x;
                rect.y += outer.y;
                rectangles[children[parent][i]] = rect;
            }
        }
    }
    Rect rect(std::size_t index) const override { return rectangles[index + 1]; }
};
class Yoga final : public Engine {
    struct Context { Yoga* owner; Size preferred; };
    YGConfigRef config{};
    YGNodeRef root{};
    std::vector<YGNodeRef> children;
    std::vector<Context> contexts;
    static YGSize measure(YGNodeConstRef node, float width, YGMeasureMode width_mode, float height, YGMeasureMode height_mode) {
        const auto& context = *static_cast<const Context*>(YGNodeGetContext(node));
        ++context.owner->measurements;
        auto axis = [](float desired,float constraint,YGMeasureMode mode) {
            if (mode == YGMeasureModeExactly) return constraint;
            if (mode == YGMeasureModeAtMost) return std::min(desired,constraint);
            return desired;
        };
        return {axis(context.preferred.width,width,width_mode),axis(context.preferred.height,height,height_mode)};
    }
public:
    explicit Yoga(const Fixture& f) {
        require(f.kind != "grid", "Yoga does not support these Grid fixtures");
        const bool row = f.kind == "row";
        require(row ? f.rows.size() == 1 : f.columns.size() == 1, "Invalid single-axis fixture");
        config = YGConfigNew();
        YGConfigSetPointScaleFactor(config,0);
        root = YGNodeNewWithConfig(config);
        YGNodeStyleSetWidthPercent(root,100);
        YGNodeStyleSetHeightPercent(root,100);
        YGNodeStyleSetFlexDirection(root,row ? YGFlexDirectionRow : YGFlexDirectionColumn);
        YGNodeStyleSetAlignItems(root,YGAlignFlexStart);
        YGNodeStyleSetPadding(root,YGEdgeAll,f.padding);
        YGNodeStyleSetGap(root,YGGutterAll,f.gap);
        children.reserve(f.items.size());
        contexts.reserve(f.items.size());
        for (const auto& item : f.items) {
            require(item.column_span == 1 && item.row_span == 1, "Yoga fixture cannot span");
            auto node = YGNodeNewWithConfig(config);
            const auto main = row ? f.columns[item.column] : f.rows[item.row];
            const auto cross = row ? f.rows[0] : f.columns[0];
            require(cross.kind == 'F', "Unsupported Yoga cross axis");
            YGNodeStyleSetMinWidth(node,0);
            YGNodeStyleSetMinHeight(node,0);
            YGNodeStyleSetFlexShrink(node,0);
            if (row) YGNodeStyleSetHeight(node,cross.value); else YGNodeStyleSetWidth(node,cross.value);
            if (main.kind == 'F') {
                if (row) YGNodeStyleSetWidth(node,main.value); else YGNodeStyleSetHeight(node,main.value);
            } else if (main.kind == 'W') {
                YGNodeStyleSetFlexBasis(node,0);
                YGNodeStyleSetFlexGrow(node,main.value);
            }
            if (f.measured) {
                contexts.push_back({this,item.preferred});
                YGNodeSetContext(node,&contexts.back());
                YGNodeSetMeasureFunc(node,measure);
            }
            YGNodeInsertChild(root,node,children.size());
            children.push_back(node);
        }
    }
    ~Yoga() override { YGNodeFreeRecursive(root); YGConfigFree(config); }
    void calculate(Size size) override {
        measurements = 0;
        YGNodeCalculateLayout(root,size.width,size.height,YGDirectionLTR);
    }
    Rect rect(std::size_t i) const override {
        return {YGNodeLayoutGetLeft(children[i]),YGNodeLayoutGetTop(children[i]),
            YGNodeLayoutGetWidth(children[i]),YGNodeLayoutGetHeight(children[i])};
    }
};
class YogaNested final : public Engine {
    const Fixture& fixture;
    YGConfigRef config{};
    std::vector<YGNodeRef> nodes;
public:
    explicit YogaNested(const Fixture& f) : fixture(f) {
        require(f.kind == "nested", "Expected nested fixture");
        config = YGConfigNew();
        YGConfigSetPointScaleFactor(config, 0);
        nodes.reserve(f.nested.size());
        for (std::size_t i = 0; i < f.nested.size(); ++i) {
            const auto& source = f.nested[i];
            auto node = YGNodeNewWithConfig(config);
            YGNodeStyleSetMinWidth(node, 0);
            YGNodeStyleSetMinHeight(node, 0);
            YGNodeStyleSetFlexShrink(node, 0);
            if (source.kind != 'L') {
                YGNodeStyleSetFlexDirection(node, source.kind == 'R'
                    ? YGFlexDirectionRow : YGFlexDirectionColumn);
                YGNodeStyleSetPadding(node, YGEdgeAll, f.padding);
                YGNodeStyleSetGap(node, YGGutterAll, f.gap);
            }
            if (i == 0) {
                YGNodeStyleSetWidthPercent(node, 100);
                YGNodeStyleSetHeightPercent(node, 100);
            } else {
                const auto& parent = f.nested[static_cast<std::size_t>(source.parent)];
                if (source.sizing == 'F') {
                    if (parent.kind == 'R') YGNodeStyleSetWidth(node, source.value);
                    else YGNodeStyleSetHeight(node, source.value);
                } else {
                    YGNodeStyleSetFlexBasis(node, 0);
                    YGNodeStyleSetFlexGrow(node, source.value);
                }
                YGNodeInsertChild(nodes[static_cast<std::size_t>(source.parent)], node,
                    YGNodeGetChildCount(nodes[static_cast<std::size_t>(source.parent)]));
            }
            nodes.push_back(node);
        }
    }
    ~YogaNested() override { YGNodeFreeRecursive(nodes[0]); YGConfigFree(config); }
    void calculate(Size size) override {
        measurements = 0;
        YGNodeCalculateLayout(nodes[0], size.width, size.height, YGDirectionLTR);
    }
    Rect rect(std::size_t index) const override {
        const auto node = nodes[index + 1];
        Rect r{0, 0, YGNodeLayoutGetWidth(node), YGNodeLayoutGetHeight(node)};
        for (std::size_t i = index + 1; i != 0; i = static_cast<std::size_t>(fixture.nested[i].parent)) {
            r.x += YGNodeLayoutGetLeft(nodes[i]);
            r.y += YGNodeLayoutGetTop(nodes[i]);
        }
        return r;
    }
};
}
int main(int argc,char** argv) {
    try {
        require(argc == 6, "Usage: native axisly|axisly_cached|axisly_flex|yoga fixture verify|cold|resize|same iterations samples");
        const std::string engine(argv[1]), mode(argv[3]);
        require(engine == "axisly" || engine == "axisly_cached" || engine == "axisly_flex"
            || engine == "axisly_nested" || engine == "yoga", "Unknown engine");
        require(mode == "verify" || mode == "cold" || mode == "resize" || mode == "same", "Unknown mode");
        const auto f = read(argv[2]);
        const auto iterations = positive(argv[4]), samples = positive(argv[5]);
        auto create = [&]() -> std::unique_ptr<Engine> {
            if (engine == "axisly") return std::make_unique<Axisly>(f);
            if (engine == "axisly_cached") return std::make_unique<Axisly>(f, true);
            if (engine == "axisly_flex") return std::make_unique<AxislyFlex>(f);
            if (engine == "axisly_nested") return std::make_unique<AxislyNested>(f);
            if (f.kind == "nested") return std::make_unique<YogaNested>(f);
            return std::make_unique<Yoga>(f);
        };
        std::cout << std::setprecision(12);
        if (mode == "verify") {
            auto instance = create();
            std::cout << "{\"frames\":[";
            // Exercise both reused and fresh state at every size; Python checks all rectangles.
            for (std::size_t frame = 0; frame < f.sizes.size()*2; ++frame) {
                if (frame >= f.sizes.size()) instance = create();
                instance->release_result();
                instance->calculate(f.sizes[frame%f.sizes.size()]);
                if (engine == "axisly_cached") {
                    // Validate every retained rectangle on hits as well as all
                    // sizes/fresh cache instances; this is outside all timers.
                    instance->calculate(f.sizes[frame%f.sizes.size()]);
                    require(instance->measurements == 0, "Cache hit unexpectedly measured");
                }
                if (frame) std::cout << ',';
                std::cout << '[';
                for (std::size_t i = 0; i < f.items.size(); ++i) {
                    const auto r = instance->rect(i);
                    if (i) std::cout << ',';
                    std::cout << '[' << r.x << ',' << r.y << ',' << r.width << ',' << r.height << ']';
                }
                std::cout << ']';
            }
            std::cout << "]}\n";
            return 0;
        }
        // Warm code paths; cold samples still use a newly created, uncomputed tree per call.
        for (std::size_t i = 0; i < f.sizes.size(); ++i) { auto e = create(); e->calculate(f.sizes[i]); }
        std::vector<double> times, checksums, counts;
        for (std::size_t sample = 0; sample < samples; ++sample) {
            auto instance = create();
            if (mode != "cold") instance->calculate(f.sizes.back());
            double elapsed{}, checksum{}, measurements{};
            for (std::size_t i = 0; i < iterations; ++i) {
                if (mode == "cold") instance = create();
                const auto size = mode == "same" ? f.sizes.back() : f.sizes[i%f.sizes.size()];
                const auto start = std::chrono::steady_clock::now();
                instance->release_result();
                instance->calculate(size);
                const auto consumed = instance->consume(f.items.size());
                const auto stop = std::chrono::steady_clock::now();
                elapsed += std::chrono::duration<double,std::micro>(stop-start).count();
                checksum += consumed;
                measurements += static_cast<double>(instance->measurements);
            }
            times.push_back(elapsed/static_cast<double>(iterations));
            checksums.push_back(checksum);
            counts.push_back(measurements/static_cast<double>(iterations));
        }
        std::cout << "{\"build\":\"" << AXISLY_BENCH_CONFIG << "\",\"axisly_version\":\""
                  << AXISLY_BENCH_VERSION << "\",\"samples_us\":[";
        auto values = [](const auto& items) { for (std::size_t i=0;i<items.size();++i) { if(i) std::cout << ','; std::cout << items[i]; } };
        values(times); std::cout << "],\"checksums\":["; values(checksums);
        std::cout << "],\"measurements\":["; values(counts); std::cout << "]}\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
