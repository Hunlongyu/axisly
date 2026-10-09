#include <axisly/css_tree.hpp>
#include "../../src/css/node_store.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <thread>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void budget_and_alignment() {
    axisly::css::LayoutMemory memory(16 * 1024);
    for (const std::size_t alignment : {std::size_t{1}, std::size_t{64}, std::size_t{4096}}) {
        auto* first = memory.allocate(8192, alignment);
        require(reinterpret_cast<std::uintptr_t>(first) % alignment == 0, "Overalignment lost");
        memory.deallocate(first, 8192, alignment);
        const auto hits = memory.stats().reuse_count;
        auto* second = memory.allocate(8192, alignment);
        require(second == first && memory.stats().reuse_count == hits + 1, "Freed block not reused");
        memory.deallocate(second, 8192, alignment);
        require(memory.stats().cached_bytes <= memory.cache_limit(), "Cache exceeded budget");
        memory.release_unused();
    }
    auto* live = memory.allocate(8192);
    auto* spare = memory.allocate(8192);
    memory.deallocate(spare, 8192);
    memory.set_cache_limit(4096);
    require(memory.stats().cached_bytes == 0 && memory.stats().live_bytes == 8192,
        "Trimming changed a live allocation");
    memory.deallocate(live, 8192);
    require(memory.stats().cached_bytes == 0 && memory.stats().live_bytes == 0,
        "Oversized allocation retained");
    auto* small = memory.allocate(64);
    memory.deallocate(small, 64);
    require(memory.stats().cached_bytes == 0, "Small allocation retained");
}
void tree_reuse() {
    using namespace axisly::css;
    auto memory = std::make_shared<LayoutMemory>();
    NodeId stale;
    for (int round = 0; round < 9; ++round) {
        LayoutTree tree({}, memory);
        Style root_style;
        root_style.display = std::array{Display::Flex, Display::Grid, Display::Block}[round % 3];
        if (root_style.display == Display::Grid) {
            root_style.grid.template_columns = {GridRepeat{RepeatKind::AutoFit, 1, {TrackSizing::fixed(40)}, {}}};
        }
        const auto root = tree.create_node(root_style);
        require(!tree.contains(stale), "Resource reuse revived a stale node identity");
        Style leaf; leaf.box.size = {Length::length(20), Length::length(10)};
        if (root_style.display == Display::Block) leaf.float_side = FloatSide::Left;
        for (int i = 0; i < 300; ++i) tree.append_child(root, tree.create_node(leaf));
        // Exercise both repeat-cache slots and retained Grid/float scratch space.
        require(tree.compute(root, {AvailableSpace::definite(800), AvailableSpace::max_content()}).succeeded(),
            "Workspace warmup failed");
        const auto& result = tree.compute(root, {AvailableSpace::definite(1000), AvailableSpace::max_content()});
        require(result.succeeded() && result.nodes.size() == 301, "Reused tree geometry incomplete");
        stale = root;
        memory->release_unused();
        require(result.nodes.front().node == root, "Trim invalidated live geometry");
    }
    require(memory->stats().live_bytes == 0 && memory->stats().reuse_count > 0,
        "Tree storage did not return to its resource");
    std::weak_ptr<LayoutMemory> weak = memory;
    {
        LayoutTree survivor({}, memory);
        memory.reset();
        require(!weak.expired(), "Tree did not retain resource ownership");
        const auto root = survivor.create_node();
        require(survivor.compute(root, {}).succeeded(), "Tree failed after host released resource");
    }
    require(weak.expired(), "Resource leaked after final tree destruction");
}
void concurrent_resource() {
    auto memory = std::make_shared<axisly::css::LayoutMemory>(32768);
    std::array<std::jthread, 4> workers;
    for (auto& worker : workers) worker = std::jthread([memory] {
        for (int i = 0; i < 500; ++i) {
            auto* pointer = static_cast<std::byte*>(memory->allocate(8192, 64));
            pointer[0] = std::byte{1}; pointer[8191] = std::byte{2};
            memory->deallocate(pointer, 8192, 64);
            if (i % 17 == 0) memory->release_unused();
        }
    });
    for (auto& worker : workers) worker.join();
    require(memory->stats().live_bytes == 0 && memory->stats().cached_bytes <= 32768,
        "Concurrent resource accounting failed");
}

#ifndef __has_feature
#define __has_feature(feature) 0
#endif
#if !defined(__SANITIZE_ADDRESS__) && !__has_feature(address_sanitizer)
// ASan intentionally terminates on impossible allocation sizes before calling
// new_handler. Ordinary builds exercise the real system-allocation failure.
axisly::css::LayoutMemory* failed_resource;
bool handled_failure;
void allocation_failure() {
    axisly::css::LayoutMemory memory;
    auto* spare = memory.allocate(8192);
    memory.deallocate(spare, 8192);
    failed_resource = &memory;
    struct HandlerGuard {
        std::new_handler previous;
        ~HandlerGuard() { std::set_new_handler(previous); }
    } guard{std::set_new_handler([] {
        failed_resource->release_unused();
        handled_failure = true;
        throw std::bad_alloc();
    })};
    bool failed = false;
    try {
        const auto impossible = std::numeric_limits<std::size_t>::max() / 2;
        auto* pointer = memory.allocate(impossible);
        memory.deallocate(pointer, impossible);
    } catch (const std::bad_alloc&) { failed = true; }
    require(failed && handled_failure && memory.stats().live_bytes == 0
        && memory.stats().cached_bytes == 0, "Failed allocation did not allow reentrant cache trimming");
    failed_resource = nullptr;
}
#endif
}
int main() {
    try {
        budget_and_alignment(); tree_reuse(); concurrent_resource();
#if !defined(__SANITIZE_ADDRESS__) && !__has_feature(address_sanitizer)
        allocation_failure();
#endif
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
