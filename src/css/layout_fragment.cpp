#include "layout_fragment.hpp"
#include "layout_engine.hpp"
#include <cassert>
#include <bit>
#include <type_traits>
#include <utility>

namespace axisly::css::detail {
static_assert(std::is_trivially_destructible_v<NodeLayout>);
static_assert(std::is_nothrow_copy_constructible_v<NodeLayout>);
static_assert(!std::is_copy_constructible_v<Fragment>);
static_assert(std::is_nothrow_move_constructible_v<Fragment>);

Fragment FragmentSnapshot::restore(FragmentRootPool& pool) const {
    Fragment fragment;
    static_cast<FragmentState&>(fragment) = *this;
    if (root) fragment.root = pool.create(*root);
    return fragment;
}

void Fragment::add_grid_report(GridTracks&& report) {
    if (!grid_reports) grid_reports = std::make_shared<FragmentGridReports>();
    else if (grid_reports.use_count() != 1) grid_reports = std::make_shared<FragmentGridReports>(*grid_reports);
    grid_reports->reports.push_back(std::make_shared<GridTracks>(std::move(report)));
}

void Fragment::append_grid_reports(Fragment&& child) {
    if (!child.grid_reports) return;
    if (!grid_reports) {
        grid_reports = std::move(child.grid_reports);
        return;
    }
    if (grid_reports.use_count() != 1) grid_reports = std::make_shared<FragmentGridReports>(*grid_reports);
    const auto& reports = child.grid_reports->reports;
    grid_reports->reports.insert(grid_reports->reports.end(), reports.begin(), reports.end());
}

std::vector<GridTracks> Fragment::take_grid_tracks() && {
    std::vector<GridTracks> result;
    if (!grid_reports) return result;
    result.reserve(grid_reports->reports.size());
    // Published results remain independent, mutable snapshots. Only uniquely
    // owned reports can transfer their vectors; cached reports are copied once
    // here rather than at every query-cache and parent-fragment boundary.
    for (auto& report : grid_reports->reports) {
        if (grid_reports.use_count() == 1 && report.use_count() == 1) result.push_back(std::move(*report));
        else result.push_back(*report);
    }
    grid_reports.reset();
    return result;
}

std::size_t Fragment::node_count() const noexcept {
    return (descendants ? descendants->node_count : 0) + (root ? 1 : 0);
}

void Fragment::reserve_children(std::size_t count) {
    if (!count) return;
    if (!descendants) descendants = std::make_shared<FragmentChildren>();
    else if (descendants.use_count() != 1) descendants = std::make_shared<FragmentChildren>(*descendants);
    descendants->reserve_hint = std::max(descendants->reserve_hint, count);
    descendants->publication.epoch = 0;
    descendants->publication.kind = FragmentChildren::Publication::Kind::None;
}

void Fragment::append_nodes(Fragment&& child) {
    if (!child.root) return;
    if (!descendants || descendants.use_count() != 1) reserve_children(1);
    const auto count = child.node_count();
    auto& children = *descendants;
    children.publication.epoch = 0;
    children.publication.kind = FragmentChildren::Publication::Kind::None;
    children.lookup_index.reset();
    if (!children.branched && child.descendants) {
        children.branches.reserve(std::max(children.reserve_hint, children.leaf_nodes().size() + 1));
        for (const auto& leaf : children.leaf_nodes()) children.branches.push_back({leaf, {}, {}});
        std::vector<NodeLayout>{}.swap(children.leaves);
        children.branched = true;
    }
    if (children.branched) {
        children.branches.push_back({child.root_layout(),
            std::move(child.descendants), std::move(child.translation)});
    } else {
        if (children.leaves.empty()) {
            // Leave room for the root when this buffer is uniquely owned.
            children.leaves.reserve(children.reserve_hint < children.leaves.max_size()
                ? children.reserve_hint + 1 : children.reserve_hint);
            children.leaves.emplace_back();
        }
        children.leaves.push_back(child.root_layout());
    }
    children.node_count += count;
}

std::optional<std::size_t> FragmentChildren::find_child(NodeId node) const {
    const auto count = branched ? branches.size() : leaf_nodes().size();
    const auto identity = [&](std::size_t index) {
        return branched ? branches[index].root.node.value : leaf_nodes()[index].node.value;
    };
    if (count < 16) {
        for (std::size_t i = 0; i < count; ++i) if (identity(i) == node.value) return i;
        return std::nullopt;
    }
    if (!lookup_index) {
        auto index = std::make_unique<LookupIndex>();
        bool sorted = true;
        for (std::size_t i = 1; i < count && sorted; ++i) sorted = identity(i - 1) < identity(i);
        if (!sorted) {
            index->order.resize(count);
            for (std::size_t i = 0; i < count; ++i) index->order[i] = i;
            std::sort(index->order.begin(), index->order.end(),
                [&](std::size_t a, std::size_t b) { return identity(a) < identity(b); });
        }
        lookup_index = std::move(index);
    }
    const auto& order = lookup_index->order;
    const auto offset = [&](std::size_t index) { return order.empty() ? index : order[index]; };
    std::size_t first = 0, last = count;
    while (first < last) {
        const auto middle = first + (last - first) / 2;
        if (identity(offset(middle)) < node.value) first = middle + 1;
        else last = middle;
    }
    if (first < count && identity(offset(first)) == node.value) return offset(first);
    return std::nullopt;
}

void Fragment::translate_descendants(Unit x, Unit y, NodeId node, FragmentTranslationPool& pool) {
    if (descendants) {
        // Each operation remains distinct: cancelling offsets would change
        // intermediate rounding and overflow. Reuse tree-owned records without
        // allocating a shared_ptr control block for every cached placement.
        translation = pool.create({x, y, node, translation});
    }
}

namespace {
struct TransformPath {
    const FragmentTranslation* translation;
    const TransformPath* outer;
};

template<class Visitor>
void visit_translation(const FragmentTranslation* translation, Visitor& visitor) {
    if (!translation) return;
    visit_translation(translation->previous.get(), visitor);
    visitor(std::pair{std::bit_cast<std::uint32_t>(translation->x),
        std::bit_cast<std::uint32_t>(translation->y)});
}
template<class Visitor>
void visit_path(const TransformPath* path, Visitor visitor) {
    for (; path; path = path->outer) visit_translation(path->translation, visitor);
}

bool matches_path(const FragmentChildren::Publication& published, const TransformPath* path) {
    std::size_t index = 0;
    bool matches = true;
    visit_path(path, [&](auto translation) {
        if (index >= published.translations.size() || published.translations[index] != translation) matches = false;
        ++index;
    });
    return matches && index == published.translations.size();
}

void apply_translation(NodeLayout& layout, const FragmentTranslation* translation) {
    if (!translation) return;
    apply_translation(layout, translation->previous.get());
    layout.border_box.x = checked(static_cast<double>(layout.border_box.x) + translation->x, translation->node);
    layout.border_box.y = checked(static_cast<double>(layout.border_box.y) + translation->y, translation->node);
    layout.content_box.x = checked(static_cast<double>(layout.content_box.x) + translation->x, translation->node);
    layout.content_box.y = checked(static_cast<double>(layout.content_box.y) + translation->y, translation->node);
}

bool find_path(const NodeLayout& node, const FragmentChildren* children,
               NodeId target, std::vector<NodeId>& path) {
    path.push_back(node.node);
    if (node.node == target) return true;
    if (children) {
        if (children->branched) {
            for (const auto& child : children->branches) {
                if (find_path(child.root, child.descendants.get(), target, path)) return true;
            }
        } else {
            for (const auto& child : children->leaf_nodes()) {
                if (child.node == target) { path.push_back(target); return true; }
            }
        }
    }
    path.pop_back();
    return false;
}

std::optional<NodeLayout> lookup_path(const NodeLayout& raw, const FragmentChildren* children,
        const FragmentTranslation* translation, const TransformPath* outer,
        std::span<const NodeId> path, LayoutPosition logical_parent,
        LayoutPosition pixel_parent, std::optional<PixelRounding> rounding, bool root,
        CoordinateSpace space) {
    if (path.empty() || raw.node != path.front()) return std::nullopt;
    auto result = raw;
    if (path.size() == 1 && space == CoordinateSpace::Parent && !root) {
        outer = nullptr;
        pixel_parent = {};
    }
    if (rounding) result = round_node_layout(raw, logical_parent, pixel_parent, *rounding, root);
    else for (auto* transform = outer; transform; transform = transform->outer) {
        apply_translation(result, transform->translation);
    }
    if (path.size() == 1) return result;
    if (!children) return std::nullopt;
    const TransformPath nested{translation, outer};
    const auto* transforms = translation ? &nested : outer;
    const LayoutPosition logical{
        checked(static_cast<double>(logical_parent.x) + raw.border_box.x, raw.node),
        checked(static_cast<double>(logical_parent.y) + raw.border_box.y, raw.node)};
    const LayoutPosition pixel{result.border_box.x, result.border_box.y};
    const auto index = children->find_child(path[1]);
    if (!index) return std::nullopt;
    if (children->branched) {
        const auto& child = children->branches[*index];
        return lookup_path(child.root, child.descendants.get(), child.translation.get(),
            transforms, path.subspan(1), logical, pixel, rounding, false, space);
    }
    return lookup_path(children->leaf_nodes()[*index], nullptr, nullptr, transforms, path.subspan(1),
        logical, pixel, rounding, false, space);
}

template<class Output>
void append_branches(Output& output, const FragmentChildren& children,
                     const TransformPath* path, PublicationContext& publication) {
    auto& published = children.publication;
    const auto start = output.size();
    const bool eligible = children.node_count >= 8 && path && publication.next_epoch;
    if (eligible && published.kind == FragmentChildren::Publication::Kind::Logical
        && published.epoch && published.epoch == publication.previous_epoch
        && matches_path(published, path)) {
        assert(published.offset <= publication.previous.size()
            && children.node_count <= publication.previous.size() - published.offset);
        const auto range = publication.previous.subspan(published.offset, children.node_count);
        output.insert(output.end(), range.begin(), range.end());
        publication.reused_nodes += children.node_count;
        published.epoch = publication.next_epoch;
        published.offset = start;
        return;
    }
    if (eligible) {
        // Updating this hint before a later failure is safe: a failed attempt
        // never publishes its epoch, so its offsets can never be reused.
        published.epoch = 0;
        published.kind = FragmentChildren::Publication::Kind::Logical;
        published.translations.clear();
        visit_path(path, [&](auto translation) { published.translations.push_back(translation); });
        published.offset = start;
        published.epoch = publication.next_epoch;
    }
    if (!children.branched) {
        if (!path) {
            const auto leaves = children.leaf_nodes();
            output.insert(output.end(), leaves.begin(), leaves.end());
        } else {
            for (auto layout : children.leaf_nodes()) {
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
            append_branches(output, *branch.descendants, branch.translation ? &nested : path, publication);
        }
    }
}
void validate_translations(const FragmentChildren& children, const TransformPath* path) {
    const auto validate = [&](NodeLayout layout) {
        for (auto* step = path; step; step = step->outer) apply_translation(layout, step->translation);
    };
    if (children.branched) {
        for (const auto& branch : children.branches) {
            if (path) validate(branch.root);
            if (branch.descendants) {
                const TransformPath nested{branch.translation.get(), path};
                validate_translations(*branch.descendants, branch.translation ? &nested : path);
            }
        }
    } else if (path) {
        for (const auto& leaf : children.leaf_nodes()) validate(leaf);
    }
}
} // namespace

void Fragment::validate_nodes() const {
    if (!descendants) return;
    const TransformPath path{translation.get(), nullptr};
    validate_translations(*descendants, translation ? &path : nullptr);
}

std::optional<NodeLayout> Fragment::lookup(NodeId node, std::span<const NodeId> path,
        std::optional<PixelRounding> rounding, CoordinateSpace space) const {
    if (!root) return std::nullopt;
    std::vector<NodeId> discovered;
    if (path.empty()) {
        if (!find_path(root_layout(), descendants.get(), node, discovered)) return std::nullopt;
        path = discovered;
    }
    return lookup_path(root_layout(), descendants.get(), translation.get(), nullptr,
        path, {}, {}, rounding, true, space);
}

std::vector<NodeLayout> Fragment::take_nodes(std::vector<NodeLayout> output,
                                           PublicationContext& publication) && {
    if (root && descendants && !descendants->leaves.empty()
        && !descendants->branched && !translation && descendants.use_count() == 1) {
        auto result = std::move(descendants->leaves);
        result.front() = root_layout();
        return result;
    }
    output.clear();
    output.reserve(node_count());
    if (root) output.push_back(root_layout());
    if (descendants) {
        const TransformPath path{translation.get(), nullptr};
        append_branches(output, *descendants, translation ? &path : nullptr, publication);
    }
    return output;
}
} // namespace axisly::css::detail
