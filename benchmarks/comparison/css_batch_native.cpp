#include "css_axisly_adapter.hpp"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <string_view>

namespace {
using namespace axisly;
using namespace axisly::bench::comparison;
std::size_t number(const char* text, bool allow_zero = false) {
    std::size_t consumed{};
    const auto value = std::stoull(text, &consumed);
    require(text[consumed] == '\0' && (allow_zero || value > 0), "Invalid number");
    return static_cast<std::size_t>(value);
}
}
int main(int argc, char** argv) {
    try {
        require(argc == 7, "Usage: css_batch_native fixture threads(0=direct) trees [verify_]resize|leaf_edit|same iterations samples");
        const auto fixture = read_fixture(argv[1]);
        const auto threads = number(argv[2], true), count = number(argv[3]);
        std::string mode(argv[4]);
        const bool verify = mode.starts_with("verify_");
        if (verify) mode.erase(0, 7);
        require(mode == "resize" || mode == "leaf_edit" || mode == "same", "Unknown batch mode");
        require(mode != "leaf_edit" || !fixture.measured.empty(), "No measured leaves");
        const auto iterations = number(argv[5]), samples = number(argv[6]);
        std::vector<css::Style> styles;
        for (const auto& node : fixture.nodes) styles.push_back(style(node));
        std::vector<std::unique_ptr<AxislyCss>> trees;
        for (std::size_t i = 0; i < count; ++i) trees.push_back(std::make_unique<AxislyCss>(fixture, styles));
        css::LayoutBatch batch(std::max(std::size_t{1}, threads));
        std::vector<css::LayoutRequest> requests(count);
        const auto step = [&](std::size_t frame) {
            const auto size = mode == "resize" ? fixture.sizes[frame % fixture.sizes.size()] : fixture.sizes.back();
            for (std::size_t i = 0; i < count; ++i) {
                if (mode == "leaf_edit") trees[i]->edit(fixture.measured[(frame * 17 + i) % fixture.measured.size()]);
                requests[i] = trees[i]->layout_request(size);
            }
            if (!threads) {
                for (auto& tree : trees) tree->calculate(size);
            } else {
                const auto results = batch.compute(requests, css::BatchExecution::Parallel);
                for (std::size_t i = 0; i < count; ++i) {
                    if (results[i].exception) std::rethrow_exception(results[i].exception);
                    require(results[i].layout != nullptr, "Missing batch result");
                    trees[i]->accept(*results[i].layout);
                }
            }
            double checksum{};
            for (const auto& tree : trees) checksum += tree->consume(fixture.nodes.size());
            return checksum;
        };
        for (auto& tree : trees) tree->calculate(fixture.sizes.back());
        std::cout << std::setprecision(12);
        if (verify) {
            std::cout << "{\"frames\":[";
            for (std::size_t frame = 0; frame < iterations; ++frame) {
                (void)step(frame);
                if (frame) std::cout << ',';
                std::cout << '[';
                for (std::size_t tree = 0; tree < count; ++tree) {
                    trees[tree]->check_order();
                    if (tree) std::cout << ',';
                    std::cout << '[';
                    for (std::size_t n = 0; n < fixture.nodes.size(); ++n) {
                        if (n) std::cout << ',';
                        const auto rect = trees[tree]->rect(n);
                        std::cout << '[' << rect.x << ',' << rect.y << ',' << rect.width << ',' << rect.height << ']';
                    }
                    std::cout << ']';
                }
                std::cout << ']';
            }
            std::cout << "]}\n";
            return 0;
        }
        for (std::size_t frame = 0; frame < 17; ++frame) (void)step(frame);
        std::vector<double> times, checksums;
        for (std::size_t sample = 0; sample < samples; ++sample) {
            double checksum{};
            const auto start = std::chrono::steady_clock::now();
            for (std::size_t frame = 0; frame < iterations; ++frame) checksum += step(frame);
            const auto elapsed = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
            times.push_back(elapsed / static_cast<double>(iterations * count));
            checksums.push_back(checksum);
        }
        std::cout << "{\"threads\":" << threads << ",\"trees\":" << count << ",\"samples_us_per_layout\":[";
        for (std::size_t i = 0; i < times.size(); ++i) { if (i) std::cout << ','; std::cout << times[i]; }
        std::cout << "],\"checksums\":[";
        for (std::size_t i = 0; i < checksums.size(); ++i) { if (i) std::cout << ','; std::cout << checksums[i]; }
        std::cout << "]}\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
