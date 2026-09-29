// Development-only benchmark for the CSS Block/Flex/Grid retained tree.
#include <axisly/css_tree.hpp>

#include "css_text_metrics.hpp"

#ifdef AXISLY_COUNT_ALLOCATIONS
#include "allocation_probe.hpp"
#endif

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {
using namespace axisly;
using namespace axisly::css;

constexpr std::size_t panel_size = 8;
constexpr std::size_t panel_columns = 4;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct TextContext {
    std::size_t index{};
    std::uint64_t revision{};
};

class Scene {
public:
    Scene(std::size_t count, axisly::bench::CssTextMetrics& metrics)
        : contexts_(count), metrics_(metrics) {
        require(count >= panel_size * panel_columns
            && count % (panel_size * panel_columns) == 0,
            "Leaf count must be a multiple of 32");
        const std::size_t panel_count = count / panel_size;
        Style root_style;
        root_style.display = Display::Grid;
        root_style.grid.template_columns.assign(panel_columns,
            TrackSizing::fraction(1));
        root_style.grid.template_rows.assign(panel_count / panel_columns,
            TrackSizing::automatic());
        root_style.box.padding = {Length::length(8), Length::length(8),
            Length::length(8), Length::length(8)};
        root_style.gap = {Length::length(8), Length::length(8)};
        root_style.align_items = {ItemPosition::Start};
        root_style.justify_items = {ItemPosition::Stretch};
        root_ = tree_.create_node(root_style);
        panels_.reserve(panel_count);
        leaves_.reserve(count);
        for (std::size_t panel_index = 0; panel_index < panel_count; ++panel_index) {
            Style panel_style;
            panel_style.display = panel_index % 3 == 0 ? Display::Block
                : panel_index % 3 == 1 ? Display::Flex : Display::Grid;
            panel_style.grid.column.start = GridLine::index(
                static_cast<std::int16_t>(panel_index % panel_columns + 1));
            panel_style.grid.row.start = GridLine::index(
                static_cast<std::int16_t>(panel_index / panel_columns + 1));
            panel_style.box.margin = {Length::length(3), Length::length(3),
                Length::length(3), Length::length(3)};
            panel_style.box.padding = {Length::length(5), Length::length(5),
                Length::length(5), Length::length(5)};
            panel_style.gap = {Length::length(2), Length::length(2)};
            panel_style.flex_direction = FlexDirection::Column;
            panel_style.align_items = {ItemPosition::Stretch};
            if (panel_style.display == Display::Grid) {
                panel_style.grid.template_columns = {TrackSizing::fraction(1)};
                panel_style.grid.template_rows.assign(panel_size,
                    TrackSizing::automatic());
            }
            const auto panel = tree_.create_node(panel_style);
            tree_.append_child(root_, panel);
            panels_.push_back(panel);
            for (std::size_t row = 0; row < panel_size; ++row) {
                const auto index = panel_index * panel_size + row;
                contexts_[index].index = index;
                Style leaf_style;
                leaf_style.display = Display::Block;
                leaf_style.flex_shrink = 0;
                if (panel_style.display == Display::Grid) {
                    leaf_style.grid.row.start = GridLine::index(
                        static_cast<std::int16_t>(row + 1));
                }
                const auto leaf = tree_.create_node(leaf_style,
                    [this, index](const MeasureRequest& request) {
                        const auto& context = contexts_[index];
                        return metrics_.measure(context.index, context.revision, request);
                    });
                tree_.append_child(panel, leaf);
                leaves_.push_back(leaf);
            }
        }
    }

    const LayoutResult& step(std::string_view mode, std::size_t frame,
                             bool full_invalidation) {
        if (mode == "leaf_edit" || (mode == "mixed" && frame % 2 == 0)) {
            const std::size_t index = frame * 17 % leaves_.size();
            ++contexts_[index].revision;
            if (!full_invalidation) tree_.mark_dirty(leaves_[index]);
        } else if (mode == "branch_edit") {
            const std::size_t panel = frame % panels_.size();
            for (std::size_t index = panel * panel_size;
                 index < (panel + 1) * panel_size; ++index) {
                ++contexts_[index].revision;
            }
            if (!full_invalidation) tree_.mark_dirty(panels_[panel]);
        } else if (mode == "resize" || mode == "mixed") {
            available_.width = AvailableSpace::definite(
                880 + static_cast<Unit>(frame % 17) * 13);
        } else require(mode == "same" || mode == "first", "Unknown CSS benchmark mode");
        if (full_invalidation && mode != "first") tree_.mark_dirty(root_);
        const auto& result = tree_.compute(root_, available_);
        if (!result.succeeded()) {
            throw std::runtime_error("CSS benchmark layout failed: "
                + result.error.message);
        }
        require(result.nodes.size() == leaves_.size() + panels_.size() + 1,
            "CSS benchmark returned incomplete layout");
        return result;
    }

private:
    std::vector<TextContext> contexts_;
    axisly::bench::CssTextMetrics& metrics_;
    LayoutTree tree_;
    NodeId root_;
    std::vector<NodeId> panels_, leaves_;
    AvailableSize available_{AvailableSpace::definite(960),
        AvailableSpace::max_content()};
};

bool close(Unit first, Unit second) {
    return std::isfinite(first) && std::isfinite(second)
        && std::abs(first - second) <= Unit{0.0001};
}

bool same(Rect first, Rect second) {
    return close(first.x, second.x) && close(first.y, second.y)
        && close(first.width, second.width) && close(first.height, second.height);
}

std::string rect_string(Rect value) {
    return std::to_string(value.x) + "," + std::to_string(value.y)
        + "," + std::to_string(value.width) + "," + std::to_string(value.height);
}

void equal(const LayoutResult& first, const LayoutResult& second,
           std::string_view mode, std::size_t frame) {
    require(first.nodes.size() == second.nodes.size()
        && first.grid_tracks.size() == second.grid_tracks.size()
        && close(first.size.width, second.size.width)
        && close(first.size.height, second.size.height),
        "CSS targeted/full result shape differs");
    std::unordered_map<std::uint64_t, std::size_t> first_index, second_index;
    for (std::size_t index = 0; index < first.nodes.size(); ++index) {
        first_index.emplace(first.nodes[index].node.value, index);
        second_index.emplace(second.nodes[index].node.value, index);
    }
    for (std::size_t index = 0; index < first.nodes.size(); ++index) {
        const auto& a = first.nodes[index];
        const auto& b = second.nodes[index];
        const auto parent_index = [](const NodeLayout& node,
                                     const auto& indices) {
            return node.parent ? indices.at(node.parent->value)
                : std::numeric_limits<std::size_t>::max();
        };
        if (parent_index(a, first_index) != parent_index(b, second_index)
            || !same(a.border_box, b.border_box)
            || !same(a.content_box, b.content_box)
            || !same(a.scrollable_overflow_rect, b.scrollable_overflow_rect)) {
            throw std::runtime_error("CSS targeted/full geometry differs in "
                + std::string(mode) + " frame " + std::to_string(frame)
                + " node " + std::to_string(index)
                + " border " + rect_string(a.border_box) + "/" + rect_string(b.border_box)
                + " content " + rect_string(a.content_box) + "/" + rect_string(b.content_box)
                + " overflow " + rect_string(a.scrollable_overflow_rect)
                + "/" + rect_string(b.scrollable_overflow_rect));
        }
    }
    for (std::size_t index = 0; index < first.grid_tracks.size(); ++index) {
        const auto& a = first.grid_tracks[index];
        const auto& b = second.grid_tracks[index];
        require(first_index.at(a.node.value) == second_index.at(b.node.value)
            && a.row_origin == b.row_origin && a.column_origin == b.column_origin
            && a.explicit_rows == b.explicit_rows
            && a.explicit_columns == b.explicit_columns
            && a.columns == b.columns && a.rows == b.rows
            && a.row_positions == b.row_positions
            && a.column_positions == b.column_positions
            && a.items.size() == b.items.size(),
            "CSS targeted/full tracks differ");
        for (std::size_t item = 0; item < a.items.size(); ++item) {
            const auto& x = a.items[item];
            const auto& y = b.items[item];
            require(first_index.at(x.node.value) == second_index.at(y.node.value)
                && x.row_start == y.row_start && x.row_end == y.row_end
                && x.column_start == y.column_start
                && x.column_end == y.column_end,
                "CSS targeted/full grid placement differs");
        }
    }
}

void validate(std::size_t count, std::string_view mode,
              axisly::bench::CssTextMetrics& metrics) {
    Scene targeted(count, metrics), full(count, metrics);
    if (mode != "first") {
        targeted.step("first", 0, false);
        full.step("first", 0, false);
    }
    for (std::size_t frame = 0; frame < 4; ++frame) {
        if (mode == "first" && frame != 0) break;
        const auto& a = targeted.step(mode, frame, false);
        const auto& b = full.step(mode, frame, true);
        equal(a, b, mode, frame);
        if (mode == "same") {
            require(a.cache_hit && a.layout_count == 0
                && a.measurement_count == 0,
                "CSS unchanged tree missed its cache");
        }
        if (mode == "leaf_edit" || mode == "branch_edit") {
            require(a.measurement_count < b.measurement_count,
                "CSS local invalidation measured unrelated branches");
        }
    }
}

struct Sample {
    double us{}, measurements{}, layouts{}, allocations{}, allocated_bytes{},
        peak_live_bytes{}, checksum{};
};

Sample batch(std::size_t count, std::string_view mode,
             bool full_invalidation, std::size_t iterations,
             axisly::bench::CssTextMetrics& metrics) {
    std::unique_ptr<Scene> retained;
    if (mode != "first") {
        retained = std::make_unique<Scene>(count, metrics);
        retained->step("first", 0, false);
    }
    Sample sample;
    for (std::size_t frame = 0; frame < iterations; ++frame) {
        std::unique_ptr<Scene> fresh;
        Scene* scene = retained.get();
        if (mode == "first") {
            fresh = std::make_unique<Scene>(count, metrics);
            scene = fresh.get();
        }
#ifdef AXISLY_COUNT_ALLOCATIONS
        axisly::bench::begin_allocations();
#endif
        const auto start = std::chrono::steady_clock::now();
        const auto& result = scene->step(mode, frame, full_invalidation);
        const auto end = std::chrono::steady_clock::now();
#ifdef AXISLY_COUNT_ALLOCATIONS
        const auto allocation = axisly::bench::end_allocations();
        sample.allocations += static_cast<double>(allocation.allocations);
        sample.allocated_bytes += static_cast<double>(allocation.requested_bytes);
        sample.peak_live_bytes += static_cast<double>(allocation.peak_live_bytes);
#endif
        sample.us += std::chrono::duration<double, std::micro>(end - start).count();
        sample.measurements += static_cast<double>(result.measurement_count);
        sample.layouts += static_cast<double>(result.layout_count);
        for (const auto index : {std::size_t{0}, result.nodes.size() / 2,
                                 result.nodes.size() - 1}) {
            const auto& rect = result.nodes[index].border_box;
            sample.checksum += static_cast<double>(rect.x) + rect.y
                + rect.width + rect.height;
        }
    }
    const double divisor = static_cast<double>(iterations);
    sample.us /= divisor;
    sample.measurements /= divisor;
    sample.layouts /= divisor;
    sample.allocations /= divisor;
    sample.allocated_bytes /= divisor;
    sample.peak_live_bytes /= divisor;
    return sample;
}

struct Options {
    std::size_t iterations{12}, samples{5};
    bool smoke{};
    std::string backend{"synthetic"};
    std::string output, raw_output;
};

std::size_t positive(std::string_view text) {
    std::size_t result{};
    const auto [end, error] = std::from_chars(text.data(),
        text.data() + text.size(), result);
    require(error == std::errc{} && end == text.data() + text.size()
        && result > 0 && result <= 10'000, "Invalid CSS benchmark count");
    return result;
}

Options options(int argc, char** argv) {
    Options config;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "--smoke") { config.smoke = true; continue; }
        require(index + 1 < argc, "Missing CSS benchmark argument");
        if (argument == "--iterations") config.iterations = positive(argv[++index]);
        else if (argument == "--samples") config.samples = positive(argv[++index]);
        else if (argument == "--backend") config.backend = argv[++index];
        else if (argument == "--output") config.output = argv[++index];
        else if (argument == "--samples-output") config.raw_output = argv[++index];
        else throw std::runtime_error("Unknown CSS benchmark argument");
    }
    if (config.smoke) { config.iterations = 2; config.samples = 2; }
    require(config.backend == "synthetic" || config.backend == "directwrite",
        "Unknown CSS benchmark text backend");
    require(config.output.empty() || config.output != config.raw_output,
        "CSS benchmark output paths must differ");
    return config;
}

double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    const auto middle = values.size() / 2;
    return values.size() % 2 ? values[middle]
        : (values[middle - 1] + values[middle]) / 2;
}

double percentile95(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    return values[static_cast<std::size_t>(std::ceil(values.size() * 0.95)) - 1];
}

void report(std::ostream& summary, std::ostream* raw, std::size_t count,
            std::string_view mode, const Options& config,
            axisly::bench::CssTextMetrics& metrics) {
    validate(count, mode, metrics);
    std::array<std::vector<Sample>, 2> samples;
    for (std::size_t trial = 0; trial < config.samples; ++trial) {
        for (std::size_t order = 0; order < 2; ++order) {
            const std::size_t profile = (trial + order) % 2;
            const auto sample = batch(count, mode, profile == 1,
                config.iterations, metrics);
            samples[profile].push_back(sample);
            if (raw) {
                *raw << metrics.name() << ',' << mode << ',' << count << ','
                    << (profile ? "full_invalidation" : "targeted") << ','
                    << trial << ',' << config.iterations << ','
                    << sample.us << ',' << sample.measurements << ','
                    << sample.layouts << ',' << sample.allocations << ','
                    << sample.allocated_bytes << ',' << sample.peak_live_bytes << ','
                    << sample.checksum << '\n';
            }
        }
        require(close(static_cast<Unit>(samples[0].back().checksum),
            static_cast<Unit>(samples[1].back().checksum)),
            "CSS paired benchmark checksums differ");
    }
    for (std::size_t profile = 0; profile < 2; ++profile) {
        std::vector<double> times;
        const auto& first = samples[profile][0];
        for (const auto& sample : samples[profile]) {
            require(sample.checksum == first.checksum
                && sample.measurements == first.measurements
                && sample.layouts == first.layouts
                && sample.allocations == first.allocations
                && sample.allocated_bytes == first.allocated_bytes
                && sample.peak_live_bytes == first.peak_live_bytes,
                "CSS benchmark work is not repeatable");
            times.push_back(sample.us);
        }
        summary << metrics.name() << ',' << mode << ',' << count << ','
            << (profile ? "full_invalidation" : "targeted") << ','
            << AXISLY_BENCH_CONFIG << ',' << AXISLY_BENCH_VERSION << ','
            << config.iterations << ',' << config.samples << ','
            << median(times) << ',' << percentile95(times) << ','
            << first.measurements << ',' << first.layouts << ','
            << first.allocations << ',' << first.allocated_bytes << ','
            << first.peak_live_bytes << ',' << first.checksum << '\n';
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        const auto config = options(argc, argv);
        std::ofstream summary_file, raw_file;
        if (!config.output.empty()) {
            summary_file.open(config.output);
            require(static_cast<bool>(summary_file), "Cannot open CSS benchmark summary");
        }
        if (!config.raw_output.empty()) {
            raw_file.open(config.raw_output);
            require(static_cast<bool>(raw_file), "Cannot open CSS benchmark samples");
        }
        auto& summary = config.output.empty() ? std::cout : summary_file;
        axisly::bench::CssTextMetrics metrics(config.backend == "directwrite");
        metrics.check_wrapping();
        summary.imbue(std::locale::classic());
        raw_file.imbue(std::locale::classic());
        summary << std::fixed << std::setprecision(6);
        raw_file << std::fixed << std::setprecision(6);
        summary << "backend,mode,leaves,profile,build,version,iterations,samples,median_us,p95_us,measurements_per_frame,layouts_per_frame,allocations_per_frame,allocated_bytes_per_frame,peak_live_bytes_per_frame,checksum\n";
        if (raw_file.is_open()) {
            raw_file << "backend,mode,leaves,profile,sample,iterations,us,measurements_per_frame,layouts_per_frame,allocations_per_frame,allocated_bytes_per_frame,peak_live_bytes_per_frame,checksum\n";
        }
        for (const auto count : config.smoke
                ? std::vector<std::size_t>{96}
                : config.backend == "directwrite"
                    ? std::vector<std::size_t>{96}
                    : std::vector<std::size_t>{96, 384}) {
            for (const std::string_view mode : {"first", "same", "resize",
                                                "leaf_edit", "branch_edit", "mixed"}) {
                report(summary, raw_file.is_open() ? &raw_file : nullptr,
                    count, mode, config, metrics);
            }
        }
        summary.flush();
        require(static_cast<bool>(summary), "CSS benchmark summary write failed");
        if (raw_file.is_open()) {
            raw_file.flush();
            require(static_cast<bool>(raw_file), "CSS benchmark samples write failed");
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
