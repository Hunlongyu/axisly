#include <axisly/css_tree.hpp>
#include <iostream>
#include <stdexcept>

namespace {
using namespace axisly;
using namespace axisly::css;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void check(LayoutTree& tree, const LayoutResult& result) {
    for (const auto& node : result.nodes) {
        require(tree.find_layout(node.node) == &node, "Index did not address the current published node");
    }
    require(!tree.find_layout({0}) && !tree.find_layout({~std::uint64_t{0}}), "Invalid identity accepted");
}
void lookup() {
    LayoutTree tree, foreign;
    const auto root = tree.create_node(), other = foreign.create_node();
    const auto first = tree.create_node(), second = tree.create_node();
    const auto nested = tree.create_node(), detached = tree.create_node();
    tree.append_child(root, first); tree.append_child(root, second);
    tree.append_child(first, nested);
    require(!tree.find_layout(root), "Uncomputed tree has a layout");
    check(tree, tree.compute(root, {}));
    require(!tree.find_layout(other) && !tree.find_layout(detached), "Foreign or uncomputed node accepted");
    const auto* old = tree.find_layout(nested);
    tree.remove_subtree(nested);
    require(tree.find_layout(nested) == old, "Mutation changed the last published snapshot");
    const auto recycled = tree.create_node();
    tree.append_child(second, recycled);
    require(!tree.find_layout(recycled), "Recycled slot aliased the old identity");
    check(tree, tree.compute(root, {}));
    require(!tree.find_layout(nested), "Removed node survived a new publication");
    tree.insert_child(root, 0, second);
    check(tree, tree.compute(root, {}));
    tree.append_child(first, recycled);
    check(tree, tree.compute(root, {}, PixelRounding{1.5F}));
    check(tree, tree.compute(root, {}));
    check(tree, tree.compute(first, {}));
    require(!tree.find_layout(root) && !tree.find_layout(second), "Partial root returned outside nodes");
    tree.set_measure(recycled, [](const MeasureRequest&) -> Measurement { throw std::runtime_error("host"); });
    old = tree.find_layout(recycled);
    bool thrown = false;
    try { (void)tree.compute(root, {}); } catch (const std::runtime_error&) { thrown = true; }
    require(thrown && tree.find_layout(recycled) == old, "Exception invalidated published lookup");
    tree.set_measure(recycled, [](const MeasureRequest&) { return Measurement{{-1, 1}}; });
    require(!tree.compute(root, {}).succeeded(), "Expected layout error");
    require(!tree.find_layout(first) && !tree.find_layout(recycled), "Failed publication exposed old geometry");
    tree.set_measure(recycled, [](const MeasureRequest&) { return Measurement{{3.25F, 2.75F}}; });
    check(tree, tree.compute(root, {}));
}
}
int main() {
    try { lookup(); return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
