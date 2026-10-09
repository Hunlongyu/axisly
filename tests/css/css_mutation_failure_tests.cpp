#include <axisly/css_tree.hpp>
#include <axisly/css_source.hpp>

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

void source_index_failure() {
    struct Source final : LayoutSource {
        std::array<Style, 4> styles;
        std::vector<NodeId> children{{2}, {3}};
        Source() {
            for (auto& style : styles) {
                style.display = Display::Block;
                style.box.size = {Length::length(20), Length::length(10)};
            }
        }
        std::optional<LayoutNodeView> node(NodeId id) const override {
            if (id.value < 1 || id.value > styles.size()) return std::nullopt;
            return LayoutNodeView{styles[id.value - 1], id.value == 1 ? std::span<const NodeId>{children}
                : std::span<const NodeId>{}, id.value == 1 ? std::nullopt : std::optional<NodeId>{{1}}};
        }
    } source;
    LayoutSession session(source);
    for (const auto queried : {NodeId{3}, NodeId{4}}) {
        if (queried.value == 4) { source.children.push_back(queried); session.invalidate(); }
        const auto& publication = session.compute({1}, {});
        const auto snapshot = publication.nodes;
        injection::operation = "source index";
        injection::remaining = 0;
        bool failed = false;
        try { (void)session.layout(queried); } catch (const std::bad_alloc&) { failed = true; }
        injection::remaining = injection::disabled;
        require(failed && publication.nodes == snapshot, "Index allocation failure damaged the snapshot");
        for (const auto& node : snapshot) require(session.layout(node.node) == node, "Source index did not recover");
    }
}

Style named_style() {
    Style style;
    style.display = Display::Grid;
    style.box.size = {Length::length(100), Length::length(40)};
    style.grid.template_columns.assign(2, TrackSizing::fraction(1));
    style.grid.template_rows.assign(2, TrackSizing::fixed(20));
    style.grid.column_line_names = {{"a-long-first-column-boundary"}, {}, {"a-long-final-column-boundary"}};
    style.grid.template_areas = std::vector<std::vector<std::string>>{
        {"a-long-top-area-name", "a-long-top-area-name"},
        {"a-long-bottom-area-name", "a-long-bottom-area-name"}};
    return style;
}

void equal_style_does_not_allocate() {
    LayoutTree tree;
    auto equal = named_style();
    const auto root = tree.create_node(equal);
    const auto before = tree.compute(root, {});
    injection::operation = "equal style";
    injection::position = 0;
    injection::remaining = 0;
    tree.set_style(root, tree.style(root));
    tree.set_style(root, equal);
    tree.set_style(root, std::move(equal));
    tree.set_children(root, tree.children(root));
    injection::remaining = injection::disabled;
    require(equal == tree.style(root), "Equal rvalue style was unnecessarily consumed");
    const auto& after = tree.compute(root, {});
    require(after.cache_hit && after.nodes == before.nodes,
        "Equal style invalidated the published layout");
    std::cout << "equal style: alias, lvalue and rvalue perform no allocation\n";
}

#if !defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0
void style_allocation_failures() {
    const auto original = named_style();
    auto changed = original;
    changed.box.size.width = Length::length(120);
    changed.grid.template_columns.assign(3, TrackSizing::fraction(1));
    changed.grid.column_line_names = {{"a-new-first-column-boundary"}, {}, {}, {"a-new-final-column-boundary"}};
    changed.grid.template_areas = std::vector<std::vector<std::string>>{
        {"new-long-area-name", "new-long-area-name", "new-long-area-name"}};
    std::size_t failures = 0;
    for (std::size_t position = 0; position < 128; ++position) {
        LayoutTree tree;
        const auto root = tree.create_node(original);
        tree.append_child(root, tree.create_node());
        const auto before = tree.compute(root, {});
        injection::operation = "set style";
        injection::position = position;
        injection::remaining = position;
        bool failed = false;
        try { tree.set_style(root, changed); }
        catch (const std::bad_alloc&) { failed = true; }
        injection::remaining = injection::disabled;
        if (failed) {
            ++failures;
            require(tree.style(root) == original && !tree.dirty(root),
                "Style copy failure changed the stored style or invalidated caches");
            const auto& after = tree.compute(root, {});
            require(after.cache_hit && after.nodes == before.nodes,
                "Style copy failure lost the last successful layout");
        } else {
            require(failures > 0 && tree.style(root) == changed && tree.dirty(root),
                "Style update did not exercise allocations or publish the new style");
            const auto updated = tree.compute(root, {});
            tree.mark_subtree_dirty(root);
            const auto& fresh = tree.compute(root, {});
            require(updated.succeeded() && updated.size.width == 120 && updated.nodes == fresh.nodes,
                "Changed style reused stale geometry");
            std::cout << "set style: " << failures << " allocation failures checked\n";
            return;
        }
    }
    throw std::runtime_error("Style allocation probe did not complete");
}
#endif
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
        equal_style_does_not_allocate();
        source_index_failure();
        exhaust("detach", [](Fixture& f) { f.tree.detach(f.branch); }, false);
        exhaust("invalidate", [](Fixture& f) { f.tree.mark_subtree_dirty(f.root); }, false);
        exhaust("remove subtree", [](Fixture& f) { f.tree.remove_subtree(f.branch); }, false);
        exhaust("remove node", [](Fixture& f) { f.tree.remove_node(f.branch); }, false);
        exhaust("insert", [](Fixture& f) { f.tree.insert_child(f.outside, 0, f.branch); }, true);
        exhaust("set children", [](Fixture& f) { f.tree.set_children(f.root, std::array{f.leaf, f.sibling}); }, true);
        exhaust("batch reparent", [](Fixture& f) { f.tree.set_children(f.outside, std::array{f.leaf, f.branch, f.sibling}); }, true);
        exhaust("replace", [](Fixture& f) { (void)f.tree.replace_child(f.root, 0, f.leaf); }, true);
#if !defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0
        style_allocation_failures();
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
