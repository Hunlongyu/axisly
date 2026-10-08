#pragma once

#include <axisly/geometry.hpp>

#include <cstddef>
#include <memory_resource>
#include <span>
#include <vector>

namespace axisly::css::detail {

struct BalancedLineWorkspace {
    explicit BalancedLineWorkspace(std::pmr::memory_resource* memory)
        : prefix(memory), previous(memory), current(memory), zero(memory),
          suffix_greedy(memory), fit_end(memory), choice(memory), result(memory) {}
    std::pmr::vector<double> prefix, previous, current;
    std::pmr::vector<unsigned char> zero;
    std::pmr::vector<std::size_t> suffix_greedy, fit_end, choice, result;
    void clear() noexcept {
        prefix.clear(); previous.clear(); current.clear(); zero.clear();
        suffix_greedy.clear(); fit_end.clear(); choice.clear(); result.clear();
    }
};

// Divide contiguous Flex items into balanced lines. A zero-size item stays
// with the preceding line whenever that produces a feasible division.
// Returned counts remain valid until this workspace is reused or cleared.
[[nodiscard]] std::span<const std::size_t> balanced_line_counts(
    std::span<const Unit> outer_sizes, Unit line_limit, Unit gap,
    std::size_t minimum_lines, std::size_t maximum_work, BalancedLineWorkspace& workspace);

} // namespace axisly::css::detail
