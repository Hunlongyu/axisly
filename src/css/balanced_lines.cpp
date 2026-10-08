#include "balanced_lines.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace axisly::css::detail {

std::span<const std::size_t> balanced_line_counts(std::span<const Unit> outer_sizes,
    Unit line_limit, Unit gap, std::size_t minimum_lines, std::size_t maximum_work,
    BalancedLineWorkspace& workspace) {
    workspace.clear();
    const auto count = outer_sizes.size();
    if (!count) return {};
    if ((!(std::isfinite(line_limit) || std::isinf(line_limit))) || line_limit < 0
        || !std::isfinite(gap) || gap < 0 || !minimum_lines) {
        throw std::invalid_argument("Invalid balanced Flex line parameters");
    }
    const double limit = line_limit;
    auto& prefix = workspace.prefix;
    auto& zero = workspace.zero;
    prefix.resize(count + 1);
    zero.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        if (!std::isfinite(outer_sizes[i])) throw std::invalid_argument("Invalid Flex item size");
        const double size = std::max(Unit{0}, outer_sizes[i]);
        zero[i] = size == 0;
        prefix[i + 1] = prefix[i] + size + gap;
    }
    const auto line_size = [&](std::size_t start, std::size_t end) {
        return prefix[end + 1] - prefix[start] - gap;
    };
    auto& suffix_greedy = workspace.suffix_greedy;
    auto& fit_end = workspace.fit_end;
    suffix_greedy.resize(count + 1);
    fit_end.resize(count);
    std::size_t fit = 0;
    for (std::size_t start = 0; start < count; ++start) {
        fit = std::max(fit, start);
        while (fit + 1 < count && line_size(start, fit + 1) <= limit) ++fit;
        fit_end[start] = fit;
    }
    for (std::size_t start = count; start-- > 0;) {
        suffix_greedy[start] = 1 + suffix_greedy[fit_end[start] + 1];
    }
    const auto line_count = std::max(suffix_greedy[0], std::min(minimum_lines, count));
    auto& result = workspace.result;
    if (line_count == count) {
        result.assign(count, 1);
        return result;
    }

    const double infinity = std::numeric_limits<double>::infinity();
    auto& previous = workspace.previous;
    auto& current = workspace.current;
    auto& choice = workspace.choice;
    previous.assign(count + 1, infinity);
    current.assign(count + 1, infinity);
    if (line_count + 1 > choice.max_size() / count) {
        throw std::length_error("Balanced Flex choice table limit exceeded");
    }
    choice.resize((line_count + 1) * count);
    for (std::size_t start = 0; start < count; ++start) {
        const double size = line_size(start, count - 1);
        if (start == count - 1 || size <= limit) previous[start] = size * size;
    }
    std::size_t work = 0;
    for (std::size_t lines = 2; lines <= line_count; ++lines) {
        std::fill(current.begin(), current.end(), infinity);
        for (std::size_t start = 0; start + lines <= count; ++start) {
            if (suffix_greedy[start] > lines) continue;
            const auto max_end = count - lines;
            const auto permitted_zero_end = std::min(fit_end[start], max_end);
            double best = infinity;
            std::size_t best_end = start;
            for (std::size_t end = start; end <= max_end; ++end) {
                if (++work > maximum_work) throw std::length_error("Balanced Flex work limit exceeded");
                const double size = line_size(start, end);
                if (end > start && size > limit) break;
                if (zero[end + 1] && end != permitted_zero_end) continue;
                if (!std::isfinite(previous[end + 1])) continue;
                const double score = size * size + previous[end + 1];
                if (score <= best) { best = score; best_end = end; }
            }
            current[start] = best;
            choice[lines * count + start] = best_end;
        }
        previous.swap(current);
    }
    if (!std::isfinite(previous[0])) throw std::logic_error("Balanced Flex division is infeasible");
    result.reserve(line_count);
    std::size_t start = 0;
    for (std::size_t lines = line_count; lines > 1; --lines) {
        const auto end = choice[lines * count + start];
        result.push_back(end - start + 1);
        start = end + 1;
    }
    result.push_back(count - start);
    return result;
}

} // namespace axisly::css::detail
