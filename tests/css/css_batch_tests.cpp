#include <axisly/css_batch.hpp>

#include <atomic>
#include <iostream>
#include <latch>
#include <stdexcept>
#include <thread>

namespace {
using namespace axisly::css;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void batches() {
    LayoutBatch batch(4);
    std::vector<std::unique_ptr<LayoutTree>> trees;
    std::vector<LayoutRequest> requests;
    const auto owner_thread = std::this_thread::get_id();
    std::atomic<bool> wrong_thread{};
    bool check_thread = true;
    std::atomic<std::size_t> calls{};
    auto memory = std::make_shared<LayoutMemory>();
    for (int i = 0; i < 16; ++i) {
        auto tree = std::make_unique<LayoutTree>(TreeLimits{}, memory);
        Style style; style.display = Display::Flex;
        const auto root = tree->create_node(style);
        for (int j = 0; j < 32; ++j) {
            const auto child = tree->create_node({}, [&](const MeasureRequest&) {
                ++calls;
                if (check_thread && std::this_thread::get_id() != owner_thread) wrong_thread = true;
                return Measurement{.content_size = {12, 10}};
            });
            tree->append_child(root, child);
        }
        requests.push_back({tree.get(), root, {AvailableSpace::definite(500), AvailableSpace::max_content()}});
        trees.push_back(std::move(tree));
    }
    std::vector<LayoutResult> expected;
    for (const auto& result : batch.compute(requests)) {
        require(result.succeeded(), "Caller-thread batch failed");
        expected.push_back(*result.layout);
    }
    require(!wrong_thread && calls > 0, "Default execution moved callbacks off the caller thread");
    check_thread = false;
    for (int frame = 0; frame < 40; ++frame) {
        for (const auto& request : requests) request.tree->mark_subtree_dirty(request.root);
        const auto results = batch.compute(requests, BatchExecution::Parallel);
        require(results.size() == expected.size(), "Wrong parallel result count");
        for (std::size_t i = 0; i < results.size(); ++i) {
            require(results[i].succeeded(), "Parallel batch failed");
            const auto& actual = results[i].layout->nodes;
            require(actual.size() == expected[i].nodes.size(), "Parallel output is incomplete");
            for (std::size_t n = 0; n < actual.size(); ++n) {
                require(actual[n].node == expected[i].nodes[n].node
                    && actual[n].border_box == expected[i].nodes[n].border_box,
                    "Parallel layout differs from the caller-thread result");
            }
        }
    }
    const auto call_count = calls.load();
    auto invalid = requests;
    invalid.back() = invalid.front();
    bool rejected = false;
    try { (void)batch.compute(invalid, BatchExecution::Parallel); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected && calls == call_count, "Duplicate trees were not rejected before dispatch");
    invalid.back().tree = nullptr;
    rejected = false;
    try { (void)batch.compute(invalid); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected && calls == call_count, "Null tree was not rejected before dispatch");
    const auto bad_leaf = trees[0]->children(requests[0].root).front();
    trees[0]->set_measure(bad_leaf, [](const MeasureRequest&) -> Measurement { throw std::runtime_error("host"); });
    requests[1].root = {};
    const auto failed = batch.compute(requests, BatchExecution::Parallel);
    require(failed[0].exception && !failed[0].layout, "Host exception was not captured");
    require(failed[1].layout && !failed[1].succeeded() && !failed[1].exception,
        "A layout error was confused with a callback exception");
    for (std::size_t i = 2; i < failed.size(); ++i) require(failed[i].succeeded(), "Failure prevented an independent request");
    require(batch.compute({}).empty(), "Empty batch returned stale results");
}
void concurrent_and_reentrant() {
    LayoutBatch batch(2);
    LayoutTree tree;
    std::latch entered(1), release(1);
    bool nested_rejected = false;
    const auto root = tree.create_node({}, [&](const MeasureRequest&) {
        try { (void)batch.compute({}); } catch (const std::logic_error&) { nested_rejected = true; }
        entered.count_down();
        release.wait();
        return Measurement{.content_size = {10, 10}};
    });
    const LayoutRequest request{&tree, root, {}};
    bool completed = false;
    std::thread running([&] { completed = batch.compute({&request, 1}).front().succeeded(); });
    entered.wait();
    bool rejected = false;
    try { (void)batch.compute({}); } catch (const std::logic_error&) { rejected = true; }
    catch (...) { release.count_down(); running.join(); throw; }
    release.count_down();
    running.join();
    require(rejected && nested_rejected && completed, "Concurrent/reentrant batch guard failed");
}
}
int main() {
    try { batches(); concurrent_and_reentrant(); return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
