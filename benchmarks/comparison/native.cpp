#include <gridly/gridly.hpp>
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
using gridly::Rect;
using gridly::Size;
struct Track { char kind{}; float value{}; };
struct Item { unsigned column{}, row{}, column_span{}, row_span{}; Size preferred; };
struct Fixture {
    std::string name, kind;
    bool measured{};
    float padding{}, gap{};
    std::vector<Track> columns, rows;
    std::vector<Item> items;
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
    in >> magic >> f.name >> f.kind >> cols >> rows >> count >> frames >> f.measured >> f.padding >> f.gap;
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
class Gridly final : public Engine {
    const Fixture& fixture;
    gridly::GridSpec spec;
    std::vector<gridly::GridItem> items;
    gridly::GridResult result;
    std::unique_ptr<gridly::GridCache> cache;
    const gridly::GridResult* output{};
    static gridly::MeasureResult measure(gridly::ItemId id, const gridly::MeasureInput&, void* context) {
        auto& self = *static_cast<Gridly*>(context);
        ++self.measurements;
        const auto size = self.fixture.items[static_cast<std::size_t>(id)].preferred;
        return {size, size};
    }
public:
    explicit Gridly(const Fixture& f, bool cached = false) : fixture(f) {
        auto track = [](Track t) {
            if (t.kind == 'F') return gridly::Track::fixed(t.value);
            if (t.kind == 'W') return gridly::Track::weighted(t.value);
            return gridly::Track::content();
        };
        for (auto t : f.columns) spec.columns.push_back(track(t));
        for (auto t : f.rows) spec.rows.push_back(track(t));
        spec.padding = {f.padding,f.padding,f.padding,f.padding};
        spec.column_gap = spec.row_gap = f.gap;
        items.reserve(f.items.size());
        for (const auto& item : f.items) items.push_back({.id = items.size(), .column = item.column, .row = item.row,
            .column_span = item.column_span, .row_span = item.row_span, .preferred_size = item.preferred});
        if (cached) {
            cache = std::make_unique<gridly::GridCache>(std::move(spec), items);
            cache->set_measure(fixture.measured ? measure : nullptr, this);
            std::vector<gridly::GridItem>{}.swap(items);
        }
    }
    void calculate(Size size) override {
        measurements = 0;
        if (cache) {
            const auto call = cache->compute(size);
            output = &call.result;
            require(call.measurement_count == measurements, "Cached callback count mismatch");
        } else {
            result = gridly::compute_grid(spec,items,size,fixture.measured ? measure : nullptr,this);
            output = &result;
        }
        require(output->succeeded(), "gridly failed");
    }
    Rect rect(std::size_t index) const override { return output->items[index].rect; }
    // Cached output belongs to the cache. Its replacement on a miss is timed.
    void release_result() override { if (!cache) result = {}; }
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
}
int main(int argc,char** argv) {
    try {
        require(argc == 6, "Usage: native gridly|gridly_cached|yoga fixture verify|cold|resize|same iterations samples");
        const std::string engine(argv[1]), mode(argv[3]);
        require(engine == "gridly" || engine == "gridly_cached" || engine == "yoga", "Unknown engine");
        require(mode == "verify" || mode == "cold" || mode == "resize" || mode == "same", "Unknown mode");
        const auto f = read(argv[2]);
        const auto iterations = positive(argv[4]), samples = positive(argv[5]);
        auto create = [&]() -> std::unique_ptr<Engine> {
            if (engine == "gridly") return std::make_unique<Gridly>(f);
            if (engine == "gridly_cached") return std::make_unique<Gridly>(f, true);
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
                if (engine == "gridly_cached") {
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
        std::cout << "{\"build\":\"" << GRIDLY_BENCH_CONFIG << "\",\"gridly_version\":\""
                  << GRIDLY_BENCH_VERSION << "\",\"samples_us\":[";
        auto values = [](const auto& items) { for (std::size_t i=0;i<items.size();++i) { if(i) std::cout << ','; std::cout << items[i]; } };
        values(times); std::cout << "],\"checksums\":["; values(checksums);
        std::cout << "],\"measurements\":["; values(counts); std::cout << "]}\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
