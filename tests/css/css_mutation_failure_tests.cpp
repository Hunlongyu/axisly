#include <axisly/css_tree.hpp>

#include <array>
#include <cstdlib>
#include <cstdio>
#include <exception>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>
#if defined(_MSC_VER)
#include <malloc.h>
#include <crtdbg.h>
#endif

namespace injection {
constexpr auto disabled = std::numeric_limits<std::size_t>::max();
std::size_t remaining = disabled;
const char* operation = "initialization";
std::size_t position{};
void check() {
    if (remaining == disabled) return;
    if (remaining-- == 0) { remaining = disabled; throw std::bad_alloc(); }
}
}
void* operator new(std::size_t bytes) {
    injection::check();
    if (auto* result = std::malloc(bytes ? bytes : 1)) return result;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }
void* operator new(std::size_t bytes, std::align_val_t alignment) {
    injection::check();
    const auto align = static_cast<std::size_t>(alignment);
#if defined(_MSC_VER)
    auto* result = _aligned_malloc(bytes ? bytes : 1, align);
#else
    auto* result = std::aligned_alloc(align, ((bytes ? bytes : 1) + align - 1) / align * align);
#endif
    if (result) return result;
    throw std::bad_alloc();
}
void operator delete(void* p, std::align_val_t) noexcept {
#if defined(_MSC_VER)
    _aligned_free(p);
#else
    std::free(p);
#endif
}
void operator delete(void* p, std::size_t, std::align_val_t alignment) noexcept { ::operator delete(p, alignment); }
void* operator new[](std::size_t bytes, std::align_val_t alignment) { return ::operator new(bytes, alignment); }
void operator delete[](void* p, std::align_val_t alignment) noexcept { ::operator delete(p, alignment); }
void operator delete[](void* p, std::size_t, std::align_val_t alignment) noexcept { ::operator delete(p, alignment); }

namespace {
using namespace axisly;
using namespace axisly::css;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Fixture {
    LayoutTree tree;
    NodeId root, branch, leaf, sibling, outside;
    Fixture() {
        Style block;
        block.display = Display::Block;
        block.box.size.width = Length::length(200);
        root = tree.create_node(block); branch = tree.create_node(block);
        Style item;
        item.box.size = {Length::length(20), Length::length(10)};
        leaf = tree.create_node(item); sibling = tree.create_node(item); outside = tree.create_node(block);
        tree.append_child(branch, leaf);
        tree.set_children(root, std::array{branch, sibling});
    }
    auto topology() const {
        std::vector<std::uint64_t> result{tree.node_count()};
        for (auto id : {root, branch, leaf, sibling, outside}) {
            result.push_back(tree.contains(id));
            if (!tree.contains(id)) continue;
            result.push_back(tree.parent(id).value_or(NodeId{}).value);
            result.push_back(tree.child_count(id));
            for (auto child : tree.children(id)) result.push_back(child.value);
        }
        return result;
    }
};
template<class Operation> void exhaust(const char* name, Operation operation, bool allocates) {
    std::size_t failures = 0;
    for (std::size_t position = 0; position < 128; ++position) {
        Fixture f;
        const auto before = f.tree.compute(f.root, {});
        const auto topology = f.topology();
        bool failed = false;
        injection::operation = name;
        injection::position = position;
        injection::remaining = position;
        try { operation(f); } catch (const std::bad_alloc&) { failed = true; }
        injection::remaining = injection::disabled;
        if (failed) {
            ++failures;
            require(f.topology() == topology, "Allocation failure partially changed topology");
            const auto& after = f.tree.compute(f.root, {});
            require(after.succeeded() && after.nodes == before.nodes, "Allocation failure corrupted caches");
        } else {
            const auto result = f.tree.compute(f.root, {});
            f.tree.mark_subtree_dirty(f.root);
            const auto& fresh = f.tree.compute(f.root, {});
            require(result.succeeded() && result.nodes == fresh.nodes, "Mutation reused obsolete geometry");
            require(!allocates || failures > 0, "Allocating path was never fault-injected");
            std::cout << name << ": " << failures << " allocation failures checked\n";
            return;
        }
    }
    throw std::runtime_error("Allocation probe did not reach successful operation");
}
}
int main() {
#if defined(_MSC_VER)
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    std::set_terminate([] {
        injection::remaining = injection::disabled;
        std::fprintf(stderr, "terminate during %s at allocation %zu\n", injection::operation, injection::position);
        if (const auto exception = std::current_exception()) {
            try { std::rethrow_exception(exception); }
            catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); }
            catch (...) {}
        }
        std::_Exit(EXIT_FAILURE);
    });
    std::cout << std::unitbuf;
    try {
        exhaust("detach", [](Fixture& f) { f.tree.detach(f.branch); }, false);
        exhaust("invalidate", [](Fixture& f) { f.tree.mark_subtree_dirty(f.root); }, false);
        exhaust("remove subtree", [](Fixture& f) { f.tree.remove_subtree(f.branch); }, false);
        exhaust("remove node", [](Fixture& f) { f.tree.remove_node(f.branch); }, false);
        exhaust("insert", [](Fixture& f) { f.tree.insert_child(f.outside, 0, f.branch); }, true);
        exhaust("set children", [](Fixture& f) { f.tree.set_children(f.root, std::array{f.leaf, f.sibling}); }, true);
        exhaust("replace", [](Fixture& f) { (void)f.tree.replace_child(f.root, 0, f.leaf); }, true);
#if !defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0
        // MSVC iterator debugging allocates container proxies in noexcept
        // default/move constructors, including evaluation of the Style argument
        // before entering our API. Faulting those proxies terminates the CRT.
        // Release and sanitizer builds exercise every allocation in creation;
        // Debug still exercises the other seven operations above and API tests.
        exhaust("create parent", [](Fixture& f) {
            (void)f.tree.create_node_with_children({}, std::array{f.branch, f.sibling});
        }, true);
#else
        std::cout << "create parent: allocation sweep requires iterator debugging disabled\n";
#endif
        return 0;
    } catch (const std::exception& error) {
        injection::remaining = injection::disabled;
        std::cerr << error.what() << '\n'; return 1;
    }
}
