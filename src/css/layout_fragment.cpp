#include "layout_fragment.hpp"
#include "layout_engine.hpp"
#include <utility>

namespace axisly::css::detail {
std::size_t Fragment::node_count() const noexcept {
    return (descendants ? descendants->node_count : 0) + (root ? 1 : 0);
}

void Fragment::reserve_children(std::size_t count) {
    if (!count) return;
    if (!descendants) descendants = std::make_shared<FragmentChildren>();
    else if (descendants.use_count() != 1) descendants = std::make_shared<FragmentChildren>(*descendants);
    descendants->reserve_hint = std::max(descendants->reserve_hint, count);
}

void Fragment::append_nodes(Fragment&& child) {
    if (!child.root) return;
    if (!descendants || descendants.use_count() != 1) reserve_children(1);
    const auto count = child.node_count();
    auto& children = *descendants;
    if (!children.branched && child.descendants) {
        children.branches.reserve(std::max(children.reserve_hint, children.leaves.size() + 1));
        for (auto& leaf : children.leaves) children.branches.push_back({std::move(leaf), {}, {}});
        std::vector<NodeLayout>{}.swap(children.leaves);
        children.branched = true;
    }
    if (children.branched) {
        children.branches.push_back({std::move(*child.root),
            std::move(child.descendants), std::move(child.translation)});
    } else {
        if (children.leaves.empty()) {
            // Leave room for the root when this buffer is uniquely owned.
            children.leaves.reserve(children.reserve_hint < children.leaves.max_size()
                ? children.reserve_hint + 1 : children.reserve_hint);
        }
        children.leaves.push_back(std::move(*child.root));
    }
    children.node_count += count;
}

void Fragment::translate_descendants(Unit x, Unit y, NodeId node) {
    if (descendants) {
        translation = std::make_shared<FragmentTranslation>(FragmentTranslation{x, y, node, translation});
    }
}

namespace {
struct TransformPath {
    const FragmentTranslation* translation;
    const TransformPath* outer;
};

void apply_translation(NodeLayout& layout, const FragmentTranslation* translation) {
    if (!translation) return;
    apply_translation(layout, translation->previous.get());
    layout.border_box.x = checked(static_cast<double>(layout.border_box.x) + translation->x, translation->node);
    layout.border_box.y = checked(static_cast<double>(layout.border_box.y) + translation->y, translation->node);
    layout.content_box.x = checked(static_cast<double>(layout.content_box.x) + translation->x, translation->node);
    layout.content_box.y = checked(static_cast<double>(layout.content_box.y) + translation->y, translation->node);
}

void append_branches(std::vector<NodeLayout>& output, const FragmentChildren& children,
                     const TransformPath* path) {
    if (!children.branched) {
        if (!path) {
            output.insert(output.end(), children.leaves.begin(), children.leaves.end());
        } else {
            for (auto layout : children.leaves) {
                for (auto* transform = path; transform; transform = transform->outer) {
                    apply_translation(layout, transform->translation);
                }
                output.push_back(layout);
            }
        }
        return;
    }
    for (const auto& branch : children.branches) {
        auto layout = branch.root;
        for (auto* transform = path; transform; transform = transform->outer) {
            apply_translation(layout, transform->translation);
        }
        output.push_back(layout);
        if (branch.descendants) {
            const TransformPath nested{branch.translation.get(), path};
            append_branches(output, *branch.descendants, branch.translation ? &nested : path);
        }
    }
}
} // namespace

std::vector<NodeLayout> Fragment::take_nodes() && {
    if (descendants && !descendants->branched && !translation && descendants.use_count() == 1) {
        auto result = std::move(descendants->leaves);
        if (root) result.insert(result.begin(), std::move(*root));
        return result;
    }
    std::vector<NodeLayout> result;
    result.reserve(node_count());
    if (root) result.push_back(std::move(*root));
    if (descendants) {
        const TransformPath path{translation.get(), nullptr};
        append_branches(result, *descendants, translation ? &path : nullptr);
    }
    return result;
}
} // namespace axisly::css::detail
