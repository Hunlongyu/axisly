#pragma once

#include <functional>
#include <memory>
#include <span>
#include <type_traits>

#include <axisly/grid.hpp>
#include <axisly/measurement.hpp>
#include <axisly/result.hpp>

namespace axisly {

// Fixed/weighted/content/range/percentage tracks, spans, margins and alignment.
// Optional automatic placement expands permitted implicit tracks first.
// Fixed tracks retain their sizes even when available is smaller or zero.
// Weighted tracks share the remaining space subject to their numeric limits.
// Without a callback, minimum is zero and preferred is GridItem::preferred_size.
// Content tracks shrink from preferred to minimum as space requires.
// Range chooses a fixed/content minimum and a fixed/content/weighted growth rule.
// Fixed upper bounds cap content requirements; a fixed lower bound overrides
// a smaller content preference. Weighted upper bounds distribute flexible space.
// Spanning minima grow Content first; otherwise they raise weighted floors
// within numeric maxima. Fixed tracks never grow to satisfy item contributions.
// size is the occupied grid extent including padding, not the available size.
// Failure returns zero size and no items; inputs are never modified.
[[nodiscard]] GridResult compute_grid(
    const GridSpec& spec,
    std::span<const GridItem> items,
    Size available);

// On success, calls measure twice per item: intrinsic sizing, then height at its
// final aligned width over all spanned columns and their internal gaps.
// Column sizing/alignment use the first result; row sizing/alignment use the second.
// known_height remains unknown. Callback exceptions propagate to the caller.
[[nodiscard]] GridResult compute_grid(
    const GridSpec& spec,
    std::span<const GridItem> items,
    Size available,
    MeasureCallback measure,
    void* user_data = nullptr);

// Synchronous convenience adapter: supports capturing/move-only callables without
// copying or retaining them. The raw callback + context overload remains available.
template<class Measure>
    requires std::is_invocable_r_v<MeasureResult, Measure&, ItemId, const MeasureInput&>
[[nodiscard]] GridResult compute_grid(
    const GridSpec& spec,
    std::span<const GridItem> items,
    Size available,
    Measure&& measure) {
    auto invoke = [&measure](ItemId id, const MeasureInput& input) -> MeasureResult {
        return std::invoke(measure, id, input);
    };
    return compute_grid(spec, items, available,
        +[](ItemId id, const MeasureInput& input, void* context) -> MeasureResult {
            return (*static_cast<decltype(invoke)*>(context))(id, input);
        }, std::addressof(invoke));
}

} // namespace axisly
