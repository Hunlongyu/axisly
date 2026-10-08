// Isolates style submission; this does not measure layout or text rendering.
#include <axisly/css_tree.hpp>

#ifdef AXISLY_COUNT_ALLOCATIONS
#include "allocation_probe.hpp"
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace axisly::css;

Style fixture(bool grid) {
    Style style;
    style.box.size = {Length::length(320), Length::length(160)};
    if (!grid) return style;
    style.display = Display::Grid;
    style.grid.template_columns.assign(8, TrackSizing::fraction(1));
    style.grid.template_rows.assign(8, TrackSizing::fixed(20));
    style.grid.column_line_names.resize(9);
    for (std::size_t i = 0; i < 9; ++i)
        style.grid.column_line_names[i] = {"long-grid-column-boundary-" + std::to_string(i)};
    std::vector<std::vector<std::string>> areas;
    for (std::size_t i = 0; i < 8; ++i)
        areas.emplace_back(8, "long-grid-template-area-" + std::to_string(i));
    style.grid.template_areas = std::move(areas);
    return style;
}

void run(bool grid, std::string_view mode, std::size_t iterations) {
    LayoutTree tree;
    const auto first = fixture(grid);
    auto second = first;
    second.box.size.width = Length::length(321);
    const auto root = tree.create_node(first);
    const auto before = tree.compute(root, {});
    if (!before.succeeded()) throw std::runtime_error("Benchmark layout failed");
    const bool changes = mode == "changed_lvalue";
    const auto update = [&](std::size_t i) {
        if (mode == "same_alias") tree.set_style(root, tree.style(root));
        else tree.set_style(root, changes && i % 2 == 0 ? second : first);
    };
    // Every batch ends on 'first', making both changed and unchanged runs repeatable.
    for (std::size_t i = 0; i < 20; ++i) update(i);
#ifdef AXISLY_COUNT_ALLOCATIONS
    axisly::bench::begin_allocations();
    for (std::size_t i = 0; i < iterations; ++i) update(i);
    const auto stats = axisly::bench::end_allocations();
    std::cout << (grid ? "named_grid" : "flex") << ',' << mode << ',' << iterations
        << ',' << double(stats.allocations) / double(iterations)
        << ',' << double(stats.requested_bytes) / double(iterations) << '\n';
#else
    std::array<double, 7> samples;
    for (auto& sample : samples) {
        const auto start = std::chrono::steady_clock::now();
        for (std::size_t i = 0; i < iterations; ++i) update(i);
        sample = std::chrono::duration<double, std::nano>(
            std::chrono::steady_clock::now() - start).count() / double(iterations);
    }
    std::sort(samples.begin(), samples.end());
    std::cout << (grid ? "named_grid" : "flex") << ',' << mode << ',' << iterations
        << ',' << samples[3] << ',' << samples.front() << ',' << samples.back() << '\n';
#endif
    const auto& after = tree.compute(root, {});
    if (!after.succeeded() || after.nodes != before.nodes || after.cache_hit == changes)
        throw std::runtime_error("Style submission changed geometry or cache semantics");
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc > 2 || (argc == 2 && std::string_view(argv[1]) != "--smoke"))
            throw std::invalid_argument("Usage: css_style_updates [--smoke]");
#ifdef AXISLY_COUNT_ALLOCATIONS
        const std::size_t iterations = argc == 2 ? 20 : 200;
        std::cout << "style,mode,iterations,allocations_per_update,bytes_per_update\n";
#else
        const std::size_t iterations = argc == 2 ? 20 : 20000;
        std::cout << "style,mode,iterations,median_ns,min_ns,max_ns\n";
#endif
        std::cout << std::setprecision(9);
        for (bool grid : {false, true})
            for (auto mode : {"same_lvalue", "same_alias", "changed_lvalue"})
                run(grid, mode, iterations);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
