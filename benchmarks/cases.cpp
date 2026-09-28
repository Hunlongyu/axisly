#include "cases.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <stdexcept>

namespace gridly::bench {
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
MeasureResult measure(ItemId id, const MeasureInput& input, void*) {
    const Unit width = 80 + static_cast<Unit>(id % 9) * 24;
    const Unit lines = input.known_width ? std::ceil(width/std::max(1.0f,*input.known_width)) : 1;
    return {{24,20*lines}, {width,20*lines}};
}
Case make_flat(std::string name, std::uint32_t side, bool content = false) {
    Case c;
    c.name = std::move(name);
    c.spec.columns.assign(side,content ? Track::content() : Track::weighted());
    c.spec.rows.assign(side,content ? Track::content() : Track::weighted());
    c.spec.column_gap = c.spec.row_gap = 4;
    c.spec.padding = {8,8,8,8};
    c.measured = content;
    c.containers = {{1280,900}};
    c.items.reserve(static_cast<std::size_t>(side)*side);
    for (std::uint32_t row = 0; row < side; ++row)
        for (std::uint32_t col = 0; col < side; ++col)
            c.items.push_back({.id = c.items.size(), .column = col, .row = row,
                .margin = {1,1,1,1}, .preferred_size = {80,24}});
    return c;
}
void add_resize(Case& c) {
    c.containers.clear();
    for (std::size_t i = 0; i < 17; ++i)
        c.containers.push_back({320+static_cast<Unit>(i)*100,400+static_cast<Unit>(i%5)*80});
}
Case make_spans(std::uint32_t side) {
    auto c = make_flat("spans_"+std::to_string(side)+"x"+std::to_string(side),side,true);
    for (std::size_t i = 0; i < c.spec.columns.size(); ++i) {
        if (i%3 == 1) c.spec.columns[i] = Track::range(Track::content(),Track::fixed(200));
        if (i%3 == 2) c.spec.columns[i] = Track::range(Track::fixed(30),Track::weighted());
    }
    for (auto& item : c.items) {
        item.column_span = std::min(2+static_cast<std::uint32_t>(item.id%3),side-item.column);
        item.row_span = std::min(2+static_cast<std::uint32_t>(item.id%2),side-item.row);
    }
    return c;
}
double checksum(Rect rect) {
    return rect.x+static_cast<double>(rect.y)*0.5+static_cast<double>(rect.width)*0.25+static_cast<double>(rect.height)*0.125;
}
template<class Result>
double fingerprint(const Result& result) {
    double sum = result.size.width+static_cast<double>(result.size.height)*0.5;
    if (!result.items.empty()) {
        for (const auto i : {std::size_t{0},result.items.size()/2,result.items.size()-1}) {
            sum += checksum(result.items[i].rect)+checksum(result.items[i].cell_rect)*0.25;
        }
    }
    return sum;
}
GridResult compute(const Case& c, Size size) {
    return compute_grid(c.spec,c.items,size,c.measured ? measure : nullptr);
}
void check_rect(Rect r) {
    require(std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.width) && std::isfinite(r.height),"Nonfinite benchmark geometry");
    require(r.width >= 0 && r.height >= 0,"Negative benchmark geometry");
}
}

NestedScene::NestedScene() {
    leaf.spec = {.columns = {Track::weighted()}, .rows = {Track::content()}, .padding = {8,8,8,8}};
    leaf.items = {{.id = 1}};
    leaf.measure = measure;
    panel.spec = {.columns = {Track::weighted()}, .rows = {Track::fixed(28),Track::content()},
        .row_gap = 6, .padding = {6,6,6,6}};
    panel.items = {{.id = 1}, {.id = 2, .row = 1}};
    panel.children = {{2,&leaf}};
    root.spec = {.columns = {Track::fixed(100),Track::weighted()}, .rows = {Track::content()},
        .column_gap = 12, .padding = {10,10,10,10}};
    root.items = {{.id = 1, .preferred_size = {100,24}}, {.id = 2, .column = 1}};
    root.children = {{2,&panel}};
}
std::vector<Case> make_cases() {
    std::vector<Case> cases;
    auto fixed = make_flat("fixed_3x3",3);
    fixed.spec.columns.assign(3,Track::fixed(100));
    fixed.spec.rows.assign(3,Track::fixed(40));
    cases.push_back(std::move(fixed));
    cases.push_back(make_flat("weighted_20x20",20));
    // Match the upstream Taffy grid/wide node counts, not its random CSS
    // styles: percentage tracks and auto-placement are outside our API.
    cases.push_back(make_flat("weighted_31x31",31));
    cases.push_back(make_flat("weighted_50x50",50));
    cases.push_back(make_flat("weighted_100x100",100));
    auto wide = make_flat("weighted_316x316",316);
    wide.containers = {{12000,12000}}; // Keep nonzero tracks at the 99,856-item scale.
    cases.push_back(std::move(wide));
    auto shuffled = make_flat("weighted_shuffled_100x100",100);
    // Exercise the entire ID width, both extremes, and unordered input order.
    // An odd multiplier is a permutation modulo 2^64, so IDs remain unique.
    for (auto& item : shuffled.items) item.id *= 0x9e3779b97f4a7c15ULL;
    shuffled.items.back().id = std::numeric_limits<ItemId>::max();
    std::mt19937 engine(20260928);
    std::shuffle(shuffled.items.begin(),shuffled.items.end(),engine);
    cases.push_back(std::move(shuffled));
    auto resize = make_flat("resize_50x50",50);
    add_resize(resize);
    cases.push_back(std::move(resize));
    cases.push_back(make_flat("content_20x20",20,true));
    auto content_resize = make_flat("content_resize_20x20",20,true);
    add_resize(content_resize);
    cases.push_back(std::move(content_resize));
    auto range = make_flat("range_20x20",20,true);
    for (std::size_t i = 0; i < range.spec.columns.size(); ++i)
        range.spec.columns[i] = i%2 == 0 ? Track::range(Track::content(),Track::fixed(120))
            : Track::weighted(2,30,160.0f);
    cases.push_back(std::move(range));
    cases.push_back(make_spans(50));
    cases.push_back(make_spans(100));
    Case nested;
    nested.name = "nested_resize_3levels";
    nested.nested = std::make_unique<NestedScene>();
    add_resize(nested);
    cases.push_back(std::move(nested));
    return cases;
}
void validate(const Case& c) {
    require(!c.containers.empty(),"Benchmark has no containers");
    for (const auto size : c.containers) {
        if (c.nested) {
            const auto result = examples::compute_nested_grid(c.nested->root,size);
            const auto repeat = examples::compute_nested_grid(c.nested->root,size);
            require(result.succeeded() && repeat.succeeded() && result.items.size() == 5,"Nested benchmark failed");
            require(result.size == repeat.size && result.measurement_count == repeat.measurement_count &&
                result.layout_count == repeat.layout_count,"Unstable nested benchmark counts");
            for (std::size_t i = 0; i < result.items.size(); ++i) {
                check_rect(result.items[i].rect);
                check_rect(result.items[i].cell_rect);
                require(result.items[i].path == repeat.items[i].path && result.items[i].rect == repeat.items[i].rect &&
                    result.items[i].cell_rect == repeat.items[i].cell_rect,"Unstable nested benchmark geometry");
            }
        } else {
            const auto result = compute(c,size);
            const auto repeat = compute(c,size);
            require(result.succeeded() && repeat.succeeded() && result.items.size() == c.items.size(),"Benchmark layout failed");
            require(result.size == repeat.size,"Unstable benchmark extent");
            require(result.measurement_count == (c.measured ? c.items.size()*2 : 0),"Benchmark measurement count changed");
            for (std::size_t i = 0; i < result.items.size(); ++i) {
                check_rect(result.items[i].rect);
                check_rect(result.items[i].cell_rect);
                require(result.items[i].id == c.items[i].id && result.items[i].rect == repeat.items[i].rect &&
                    result.items[i].cell_rect == repeat.items[i].cell_rect,"Unstable benchmark geometry or ID");
            }
        }
    }
}
Observation run(const Case& c, std::size_t iteration) {
    const auto size = c.containers[iteration%c.containers.size()];
    if (c.nested) {
        const auto result = examples::compute_nested_grid(c.nested->root,size);
        require(result.succeeded(),"Nested benchmark failed in sample");
        return {fingerprint(result),result.measurement_count,result.layout_count};
    }
    const auto result = compute(c,size);
    require(result.succeeded(),"Benchmark failed in sample");
    return {fingerprint(result),result.measurement_count,1};
}
} // namespace gridly::bench
