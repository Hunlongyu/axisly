#pragma once

#include <axisly/css_tree.hpp>

#include <cmath>
#include <limits>
#include <stdexcept>

namespace axisly::css::detail {

class LayoutFailure final : public std::runtime_error {
public:
    LayoutFailure(LayoutErrorCode code, NodeId node, const char* message)
        : std::runtime_error(message), code(code), node(node) {}
    LayoutErrorCode code;
    NodeId node;
};

[[noreturn]] void layout_numeric_overflow(NodeId id);
[[noreturn]] void unresolved_layout_edge(NodeId id);

[[nodiscard]] inline Unit checked(double value, NodeId id) {
    if (!(std::abs(value) <= std::numeric_limits<Unit>::max())) {
        layout_numeric_overflow(id);
    }
    return static_cast<Unit>(value);
}

} // namespace axisly::css::detail
