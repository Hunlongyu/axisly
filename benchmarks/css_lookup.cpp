#include <axisly/css_tree.hpp>

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace axisly;
using namespace axisly::css;
using Clock = std::chrono::steady_clock;
struct Fixture {
    LayoutTree tree;
    NodeId root;
    std::vector<NodeId> ids;
    const LayoutResult* output{};
    explicit Fixture(std::size_t count) {
        Style container;
        container.display = Display::Flex;
        container.box.size = {Length::length(static_cast<Unit>(count * 24)), Length::length(40)};
        root = tree.create_node(container); ids.push_back(root);
        Style leaf;
        leaf.box.size = {Length::length(20.25F), Length::length(12.5F)};
        leaf.flex_shrink = 0;
        for (std::size_t i = 1; i < count; ++i) {
            ids.push_back(tree.create_node(leaf)); tree.append_child(root, ids.back());
        }
        output = &tree.compute(root, {});
        if (!output->succeeded() || output->nodes.size() != count) throw std::runtime_error("Incomplete lookup fixture");
    }
};
double elapsed(Clock::time_point start) {
    return std::chrono::duration<double, std::micro>(Clock::now() - start).count();
}
void numbers(const std::vector<double>& values) {
    std::cout << '[';
    for (std::size_t i = 0; i < values.size(); ++i) { if (i) std::cout << ','; std::cout << values[i]; }
    std::cout << ']';
}
}

int main() {
    try {
        std::cout << std::setprecision(12) << "{\"unit\":\"us_per_query_batch\",\"cases\":[";
        bool first = true;
        for (const std::size_t count : {101U, 433U, 10001U}) {
            Fixture fixture(count);
            for (const auto& node : fixture.output->nodes) {
                if (fixture.tree.find_layout(node.node) != &node) throw std::runtime_error("Lookup geometry mismatch");
            }
            std::vector<double> first_lookup;
            for (int sample = 0; sample < 9; ++sample) {
                Fixture cold(count); // Construction/layout excluded from the first-lookup timer.
                const auto start = Clock::now();
                const auto* result = cold.tree.find_layout(cold.ids.back());
                first_lookup.push_back(elapsed(start));
                if (result != &cold.output->nodes.back()) throw std::runtime_error("First lookup mismatch");
            }
            for (const auto queries : {std::size_t{1}, std::size_t{3}, std::size_t{32}, count}) {
                const auto iterations = std::max(std::size_t{2}, std::size_t{20'000'000} / (count * queries));
                std::vector<double> linear, indexed;
                double checksums[2]{};
                for (int sample = 0; sample < 9; ++sample) {
                    // Alternate measurement order without changing either query sequence.
                    for (int order = 0; order < 2; ++order) {
                        const int method = (sample + order) % 2;
                        double checksum = 0;
                        const auto start = Clock::now();
                        for (std::size_t frame = 0; frame < iterations; ++frame) {
                            for (std::size_t i = 0; i < queries; ++i) {
                                const auto id = fixture.ids[(frame * 104729 + i * 7919) % count];
                                const auto* node = method ? fixture.tree.find_layout(id) : fixture.output->find(id);
                                if (!node) throw std::runtime_error("Missing queried node");
                                checksum += static_cast<double>(node->border_box.x) + node->border_box.width;
                            }
                        }
                        (method ? indexed : linear).push_back(elapsed(start) / static_cast<double>(iterations));
                        checksums[method] = checksum;
                    }
                    if (checksums[0] != checksums[1]) throw std::runtime_error("Lookup checksum mismatch");
                }
                if (!first) std::cout << ',';
                first = false;
                std::cout << "{\"nodes\":" << count << ",\"queries\":" << queries
                    << ",\"iterations\":" << iterations << ",\"index_payload_bytes\":" << count * sizeof(std::size_t)
                    << ",\"checksum\":" << checksums[0] << ",\"linear_samples_us\":";
                numbers(linear); std::cout << ",\"indexed_samples_us\":"; numbers(indexed);
                std::cout << ",\"first_lookup_samples_us\":"; numbers(first_lookup); std::cout << '}';
            }
        }
        std::cout << "]}\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
