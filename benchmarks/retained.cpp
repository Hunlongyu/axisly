// Development-only retained-tree benchmark. No renderer or window is created.
#include <gridly/gridly.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <locale>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <dwrite.h>
#include <wrl/client.h>
#endif

namespace {
using namespace gridly;
constexpr std::size_t group_size = 25;
constexpr ItemId root_item = 1'000'000;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }

class TextMetrics {
public:
    explicit TextMetrics(bool real_text) : real_(real_text) {
#ifdef _WIN32
        if (real_) {
            check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                reinterpret_cast<IUnknown**>(factory_.GetAddressOf())));
            Microsoft::WRL::ComPtr<IDWriteFontCollection> fonts;
            check(factory_->GetSystemFontCollection(fonts.GetAddressOf()));
            UINT32 index{}; BOOL exists{};
            check(fonts->FindFamilyName(L"Consolas", &index, &exists));
            require(exists != FALSE, "Consolas is required for a reproducible text benchmark");
            check(factory_->CreateTextFormat(L"Consolas", fonts.Get(), DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 16, L"en-us", format_.GetAddressOf()));
            check(format_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP));
        }
#else
        require(!real_, "DirectWrite is available only on Windows");
#endif
    }
    MeasureResult measure(std::size_t id, std::uint64_t revision, const MeasureInput& input) {
        const auto variant = (id + static_cast<std::size_t>(revision)) % texts_.size();
#ifdef _WIN32
        if (real_) {
            const auto text = texts_[variant];
            Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
            check(factory_->CreateTextLayout(text.data(), static_cast<UINT32>(text.size()), format_.Get(),
                std::max(0.001f, input.known_width.value_or(1'000'000.f)), 1'000'000.f, layout.GetAddressOf()));
            DWRITE_TEXT_METRICS metrics{};
            check(layout->GetMetrics(&metrics));
            return {{}, {metrics.widthIncludingTrailingWhitespace, metrics.height}};
        }
#endif
        const auto width = static_cast<Unit>(texts_[variant].size()) * 8;
        const auto lines = input.known_width ? std::ceil(width / std::max(1.f, *input.known_width)) : 1.f;
        return {{}, {width, 18 * lines}};
    }
    [[nodiscard]] const char* name() const { return real_ ? "directwrite_consolas_16" : "synthetic_wrapping"; }
    void check_wrapping() {
        if (!real_) return;
        const auto narrow = measure(3, 0, {80, {}, {80, 600}});
        const auto wide = measure(3, 0, {600, {}, {600, 600}});
        require(narrow.preferred.height > wide.preferred.height && wide.preferred.width > 0,
                "Real font measurement did not respond to width constraints");
    }
private:
    bool real_{};
    const std::array<std::wstring_view, 5> texts_{
        L"Status: ready", L"Temperature setpoint 24.5 C", L"Channel 07: acquisition in progress, 128 samples collected.",
        L"Grid layout cache: changing this label invalidates its ancestors while the other panels keep their measured geometry.",
        L"Long diagnostic text wraps at the allocated width. A window resize must remeasure it; reusing an old height here would hide a layout bug."};
#ifdef _WIN32
    static void check(HRESULT result) { if (FAILED(result)) throw std::runtime_error("DirectWrite measurement failed"); }
    Microsoft::WRL::ComPtr<IDWriteFactory> factory_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> format_;
#endif
};

struct Context {
    TextMetrics* metrics{};
    std::size_t id{};
    std::uint64_t revision{};
};
MeasureResult measure(ItemId, const MeasureInput& input, void* raw) {
    auto& context = *static_cast<Context*>(raw);
    return context.metrics->measure(context.id, context.revision, input);
}

class Scene {
public:
    Scene(std::size_t count, TextMetrics& metrics) : contexts_(count) {
        require(count >= 100 && count % (4 * group_size) == 0, "Invalid retained workload size");
        root_spec_.columns.assign(4, Track::weighted());
        root_spec_.rows.assign(count / (4 * group_size), Track::weighted());
        root_spec_.column_gap = root_spec_.row_gap = 8;
        root_spec_.padding = {8, 8, 8, 8};
        child_spec_.columns = {Track::weighted()};
        child_spec_.rows.assign(group_size, Track::content());
        child_spec_.row_gap = 2;
        root_ = tree_.create_grid(root_spec_, {.id = root_item});
        for (std::size_t group = 0; group < count / group_size; ++group) {
            GridItem placement{.id = root_item + 1 + group, .column = static_cast<std::uint32_t>(group % 4),
                               .row = static_cast<std::uint32_t>(group / 4)};
            root_items_.push_back(placement);
            const auto node = tree_.create_grid(child_spec_, placement);
            groups_.push_back(node);
            tree_.append_child(root_, node);
            for (std::size_t row = 0; row < group_size; ++row) {
                const auto i = group * group_size + row;
                contexts_[i] = {&metrics, i, 0};
                const auto leaf = tree_.create_leaf({.id = i, .row = static_cast<std::uint32_t>(row)}, measure, &contexts_[i]);
                leaves_.push_back(leaf);
                tree_.append_child(node, leaf);
            }
        }
        require(tree_.compute(root_, available_).succeeded(), "Retained warm-up failed");
    }
    const TreeResult& step(std::string_view mode, std::size_t frame, bool full) {
        if (mode == "leaf_edit") {
            const auto i = (frame * 17) % leaves_.size();
            ++contexts_[i].revision;
            if (!full) tree_.mark_dirty(leaves_[i]);
        } else if (mode == "branch_edit") {
            const auto group = frame % groups_.size();
            for (std::size_t i = group * group_size; i < (group + 1) * group_size; ++i) ++contexts_[i].revision;
            if (!full) tree_.mark_dirty(groups_[group]);
        } else if (mode == "resize") {
            available_ = {960 + static_cast<Unit>(frame % 17) * 17, 900 + static_cast<Unit>(frame % 7) * 19};
        } else require(mode == "same", "Unknown retained benchmark mode");
        if (full) tree_.mark_dirty(root_);
        const auto& output = tree_.compute(root_, available_);
        require(output.succeeded(), "Retained benchmark layout failed");
        return output;
    }
    // Independent stateless geometry oracle: this workload has numeric weighted
    // root tracks and unspanned panels whose floors fit the root allocation.
    std::vector<NodeLayout> oracle() {
        const auto root = compute_grid(root_spec_, root_items_, available_);
        require(root.succeeded(), "Stateless root failed");
        const Rect extent{0, 0, root.size.width, root.size.height};
        std::vector<NodeLayout> output{{{}, root_item, extent, extent}};
        std::vector<GridItem> items(group_size);
        for (std::size_t group = 0; group < groups_.size(); ++group) {
            const auto& panel = root.items[group];
            output.push_back({{}, panel.id, panel.rect, panel.cell_rect});
            for (std::size_t row = 0; row < group_size; ++row)
                items[row] = {.id = group * group_size + row, .row = static_cast<std::uint32_t>(row)};
            const auto layout = compute_grid(child_spec_, items, {panel.rect.width, panel.rect.height},
                [&](ItemId id, const MeasureInput& input) { return measure(id, input, &contexts_[static_cast<std::size_t>(id)]); });
            require(layout.succeeded(), "Stateless panel failed");
            for (const auto& item : layout.items) {
                auto rect = item.rect, cell = item.cell_rect;
                rect.x += panel.rect.x; rect.y += panel.rect.y;
                cell.x += panel.rect.x; cell.y += panel.rect.y;
                output.push_back({{}, item.id, rect, cell});
            }
        }
        return output;
    }
private:
    std::vector<Context> contexts_;
    GridTree tree_;
    NodeId root_;
    std::vector<NodeId> groups_, leaves_;
    GridSpec root_spec_, child_spec_;
    std::vector<GridItem> root_items_;
    Size available_{1100, 980};
};

bool close(Unit a, Unit b) { return std::isfinite(a) && std::isfinite(b) && std::abs(a - b) <= 0.0001f + 0.000001f * std::max(std::abs(a), std::abs(b)); }
bool close(Rect a, Rect b) { return close(a.x, b.x) && close(a.y, b.y) && close(a.width, b.width) && close(a.height, b.height); }
void equal(std::span<const NodeLayout> a, std::span<const NodeLayout> b) {
    require(a.size() == b.size(), "Retained geometry node count mismatch");
    for (std::size_t i = 0; i < a.size(); ++i)
        require(a[i].item_id == b[i].item_id && close(a[i].rect, b[i].rect) && close(a[i].cell_rect, b[i].cell_rect),
                "Retained geometry differs from stateless/full-invalidation oracle");
}
void validate(std::size_t count, TextMetrics& metrics, std::string_view mode, std::size_t frames) {
    Scene targeted(count, metrics), full(count, metrics);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const auto& a = targeted.step(mode, frame, false);
        const auto& b = full.step(mode, frame, true);
        equal(a.items, b.items);
        equal(a.items, targeted.oracle());
        require(a.size == b.size, "Retained occupied extent differs");
        if (mode == "same") require(a.cache_hit && a.measurement_count == 0 && a.layout_count == 0, "Unchanged tree was recomputed");
        if (mode == "leaf_edit" || mode == "branch_edit")
            require(a.measurement_count < b.measurement_count, "Targeted invalidation remeasured unrelated branches");
    }
}

struct Sample { double us{}, measurements{}, layouts{}, checksum{}; };
Sample batch(std::size_t count, TextMetrics& metrics, std::string_view mode, bool full, std::size_t iterations) {
    Scene scene(count, metrics); // Construction, font setup, warm-up and destruction are outside the timer.
    Sample sample;
    const auto start = std::chrono::steady_clock::now();
    for (std::size_t frame = 0; frame < iterations; ++frame) {
        const auto& output = scene.step(mode, frame, full); // Mutation and invalidation ARE timed.
        sample.measurements += static_cast<double>(output.measurement_count);
        sample.layouts += static_cast<double>(output.layout_count);
        for (const auto index : {std::size_t{0}, output.items.size() / 2, output.items.size() - 1}) {
            const auto& r = output.items[index].rect;
            sample.checksum += static_cast<double>(r.x) + r.y + r.width + r.height;
        }
    }
    sample.us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count()
                / static_cast<double>(iterations);
    sample.measurements /= static_cast<double>(iterations);
    sample.layouts /= static_cast<double>(iterations);
    return sample;
}

struct Options {
    std::size_t iterations{20}, samples{7};
    bool smoke{};
    std::string output, raw_output;
};
std::size_t positive(std::string_view text) {
    std::size_t value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    require(error == std::errc{} && end == text.data() + text.size() && value > 0 && value <= 10000, "Invalid benchmark count");
    return value;
}
Options options(int argc, char** argv) {
    Options result;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--smoke") { result.smoke = true; continue; }
        require(i + 1 < argc, "Missing benchmark argument");
        if (arg == "--iterations") result.iterations = positive(argv[++i]);
        else if (arg == "--samples") result.samples = positive(argv[++i]);
        else if (arg == "--output") result.output = argv[++i];
        else if (arg == "--samples-output") result.raw_output = argv[++i];
        else throw std::runtime_error("Unknown retained benchmark argument");
    }
    if (result.smoke) { result.iterations = 2; result.samples = 2; }
    require(result.output.empty() || result.output != result.raw_output, "Summary and raw output paths must differ");
    return result;
}

void report(std::ostream& out, std::ostream* raw, std::size_t count, TextMetrics& metrics,
            std::string_view mode, const Options& config) {
    validate(count, metrics, mode, config.smoke ? 3 : std::max<std::size_t>(32, config.iterations));
    std::array<std::vector<Sample>, 2> results;
    for (std::size_t i = 0; i < config.samples; ++i) {
        // Alternate paired engine order so only one path is not always colder.
        for (std::size_t position = 0; position < 2; ++position) {
            const std::size_t engine = (i + position) % 2;
            const auto sample = batch(count, metrics, mode, engine == 1, config.iterations);
            results[engine].push_back(sample);
            if (raw) *raw << metrics.name() << ',' << mode << ',' << count << ',' << (engine ? "full_invalidation" : "targeted")
                          << ',' << i << ',' << config.iterations << ',' << sample.us << ',' << sample.measurements << ','
                          << sample.layouts << ',' << sample.checksum << '\n';
        }
        require(std::abs(results[0].back().checksum - results[1].back().checksum) < 0.0001, "Paired benchmark checksums differ");
    }
    for (std::size_t engine = 0; engine < 2; ++engine) {
        std::vector<double> times;
        for (const auto& sample : results[engine]) {
            require(sample.checksum == results[engine][0].checksum && sample.measurements == results[engine][0].measurements
                    && sample.layouts == results[engine][0].layouts, "Retained batches are not repeatable");
            times.push_back(sample.us);
        }
        std::sort(times.begin(), times.end());
        const auto mid = times.size() / 2;
        const auto median = times.size() % 2 ? times[mid] : (times[mid - 1] + times[mid]) / 2;
        const auto p95 = times[static_cast<std::size_t>(std::ceil(static_cast<double>(times.size()) * .95)) - 1];
        const auto& first = results[engine][0];
        out << metrics.name() << ',' << mode << ',' << count << ',' << count / group_size << ','
            << (engine ? "full_invalidation" : "targeted") << ',' << GRIDLY_BENCH_CONFIG << ',' << GRIDLY_BENCH_VERSION << ','
            << config.iterations << ',' << config.samples << ',' << median << ',' << p95 << ','
            << first.measurements << ',' << first.layouts << ',' << first.checksum << '\n';
    }
}
}

int main(int argc, char** argv) {
    try {
        const auto config = options(argc, argv);
        std::ofstream file, raw;
        if (!config.output.empty()) { file.open(config.output); require(static_cast<bool>(file), "Cannot open summary output"); }
        if (!config.raw_output.empty()) { raw.open(config.raw_output); require(static_cast<bool>(raw), "Cannot open samples output"); }
        auto& out = config.output.empty() ? std::cout : file;
        out.imbue(std::locale::classic()); raw.imbue(std::locale::classic());
        out << std::fixed << std::setprecision(6); raw << std::fixed << std::setprecision(6);
        out << "backend,mode,leaves,groups,engine,build,version,iterations,samples,median_us,p95_batch_us,measurements_per_frame,layouts_per_frame,checksum\n";
        if (raw.is_open()) raw << "backend,mode,leaves,engine,batch,iterations,us,measurements_per_frame,layouts_per_frame,checksum\n";
        TextMetrics synthetic(false);
        for (const auto count : {100u, 1000u})
            for (const std::string_view mode : {"same", "leaf_edit", "branch_edit", "resize"})
                report(out, raw.is_open() ? &raw : nullptr, count, synthetic, mode, config);
#ifdef _WIN32
        TextMetrics text(true);
        text.check_wrapping();
        for (const std::string_view mode : {"same", "leaf_edit", "branch_edit", "resize"})
            report(out, raw.is_open() ? &raw : nullptr, 100, text, mode, config);
#endif
        out.flush(); require(static_cast<bool>(out), "Failed to write retained summary");
        if (raw.is_open()) { raw.flush(); require(static_cast<bool>(raw), "Failed to write retained samples"); }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
