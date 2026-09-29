#pragma once

#include <axisly/geometry.hpp>

#include <cstddef>
#include <span>
#include <vector>

namespace axisly::css::detail {

// Divide contiguous Flex items into balanced lines. A zero-size item stays
// with the preceding line whenever that produces a feasible division.
[[nodiscard]] std::vector<std::size_t> balanced_line_counts(
    std::span<const Unit> outer_sizes, Unit line_limit, Unit gap,
    std::size_t minimum_lines, std::size_t maximum_work);

} // namespace axisly::css::detail
