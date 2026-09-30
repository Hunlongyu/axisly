#include "layout_fragment.hpp"
#include <iterator>
#include <utility>

namespace axisly::css::detail {
std::size_t Fragment::node_count() const noexcept {
    return descendants.size() + (root ? 1 : 0);
}
void Fragment::append_nodes(Fragment&& child) {
    if (child.root) descendants.push_back(std::move(*child.root));
    descendants.insert(descendants.end(),
        std::make_move_iterator(child.descendants.begin()),
        std::make_move_iterator(child.descendants.end()));
}
std::vector<NodeLayout> Fragment::take_nodes() && {
    if (root) descendants.insert(descendants.begin(), std::move(*root));
    return std::move(descendants);
}
} // namespace axisly::css::detail
