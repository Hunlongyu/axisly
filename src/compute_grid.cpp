#include <gridly/layout.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <limits>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "detail/track_sizing.hpp"
#include "detail/item_alignment.hpp"
#include "detail/span_resolution.hpp"
#include "detail/layout_workspace.hpp"
#include "detail/measurement_validation.hpp"

namespace gridly {
namespace {

bool is_nonnegative_finite(Unit value) noexcept {
    return std::isfinite(value) && value >= 0.0f;
}

bool valid_insets(const Insets& insets) noexcept {
    return is_nonnegative_finite(insets.left) && is_nonnegative_finite(insets.top)
        && is_nonnegative_finite(insets.right) && is_nonnegative_finite(insets.bottom);
}

bool valid_alignment(HorizontalAlignment alignment) noexcept {
    switch (alignment) {
    case HorizontalAlignment::Start:
    case HorizontalAlignment::Center:
    case HorizontalAlignment::End:
    case HorizontalAlignment::Stretch:
        return true;
    }
    return false;
}

bool valid_alignment(VerticalAlignment alignment) noexcept {
    switch (alignment) {
    case VerticalAlignment::Start:
    case VerticalAlignment::Center:
    case VerticalAlignment::End:
    case VerticalAlignment::Stretch:
    case VerticalAlignment::Baseline:
        return true;
    }
    return false;
}

bool valid_range(const RangeTrack& range) noexcept {
    const auto* lower = std::get_if<FixedTrack>(&range.minimum);
    const auto* percent_lower = std::get_if<PercentageTrack>(&range.minimum);
    if (lower ? !is_nonnegative_finite(lower->size)
        : percent_lower ? !is_nonnegative_finite(percent_lower->fraction)
        : !std::holds_alternative<ContentTrack>(range.minimum)) {
        return false;
    }
    if (const auto* upper = std::get_if<FixedTrack>(&range.maximum)) {
        return is_nonnegative_finite(upper->size) && (!lower || upper->size >= lower->size);
    }
    if (const auto* upper = std::get_if<WeightedTrack>(&range.maximum)) {
        return is_nonnegative_finite(upper->weight) && upper->weight > 0.0f
            && upper->minimum == 0.0f && !upper->maximum;
    }
    if (const auto* upper = std::get_if<PercentageTrack>(&range.maximum)) {
        return is_nonnegative_finite(upper->fraction)
            && (!percent_lower || percent_lower->fraction <= upper->fraction);
    }
    return std::holds_alternative<ContentTrack>(range.maximum);
}

std::optional<GridError> validate_tracks(std::span<const Track> tracks, const char* axis) {
    for (std::size_t index = 0; index < tracks.size(); ++index) {
        const auto& definition = tracks[index].definition();
        if (const auto* fixed = std::get_if<FixedTrack>(&definition)) {
            if (!is_nonnegative_finite(fixed->size)) {
                return GridError{GridErrorCode::InvalidTrack,
                                 std::string{axis} + " track " + std::to_string(index) + ": size must be finite and non-negative", {}};
            }
        } else if (const auto* weighted = std::get_if<WeightedTrack>(&definition)) {
            if (!is_nonnegative_finite(weighted->weight) || weighted->weight == 0.0f
                || !is_nonnegative_finite(weighted->minimum)
                || (weighted->maximum && (!is_nonnegative_finite(*weighted->maximum) || *weighted->maximum < weighted->minimum))) {
                return GridError{GridErrorCode::InvalidTrack,
                                 std::string{axis} + " track " + std::to_string(index) + ": invalid weight or numeric limits", {}};
            }
        } else if (const auto* percent = std::get_if<PercentageTrack>(&definition)) {
            if (!is_nonnegative_finite(percent->fraction))
                return GridError{GridErrorCode::InvalidTrack, "Percentage must be finite and non-negative", {}};
        } else if (const auto* range = std::get_if<RangeTrack>(&definition)) {
            if (!valid_range(*range)) {
                return GridError{GridErrorCode::InvalidTrack,
                                 std::string{axis} + " track " + std::to_string(index) + ": invalid range bounds", {}};
            }
        }
    }
    return std::nullopt;
}

std::size_t first_duplicate_index(std::span<const GridItem> items,
                                  std::vector<std::pair<ItemId, std::size_t>>& ids) {
    ids.clear();
    if (items.size() < 2) {
        return items.size();
    }
    // One contiguous allocation replaces a hash node for every item. Keep the
    // original index so duplicate errors retain input order, not sorted ID order.
    ids.reserve(items.size());
    for (std::size_t index = 0; index < items.size(); ++index) {
        ids.emplace_back(items[index].id, index);
    }
    std::sort(ids.begin(), ids.end());
    auto duplicate = items.size();
    for (std::size_t index = 1; index < ids.size(); ++index) {
        if (ids[index - 1].first == ids[index].first) {
            duplicate = std::min(duplicate, ids[index].second);
        }
    }
    return duplicate;
}

std::optional<GridError> validate_input(const GridSpec& spec, std::span<const GridItem> items,
                                      std::vector<std::pair<ItemId, std::size_t>>& ids) {
    if (!is_nonnegative_finite(spec.column_gap) || !is_nonnegative_finite(spec.row_gap)) {
        return GridError{GridErrorCode::InvalidGap, "Gaps must be finite and non-negative", {}};
    }
    if (!valid_insets(spec.padding)) {
        return GridError{GridErrorCode::InvalidPadding, "Padding must be finite and non-negative", {}};
    }
    if (auto error = validate_tracks(spec.columns, "Column")) {
        return error;
    }
    if (auto error = validate_tracks(spec.rows, "Row")) {
        return error;
    }

    const auto duplicate = first_duplicate_index(items, ids);
    for (std::size_t index = 0; index < items.size(); ++index) {
        const auto& item = items[index];
        if (item.column >= spec.columns.size() || item.row >= spec.rows.size()) {
            return GridError{GridErrorCode::InvalidPlacement, "Item row or column is outside the explicit grid", item.id};
        }
        // Subtract after validating the index, avoiding unsigned index + span overflow.
        if (item.column_span == 0 || item.row_span == 0
            || item.column_span > spec.columns.size() - item.column
            || item.row_span > spec.rows.size() - item.row) {
            return GridError{GridErrorCode::InvalidSpan, "Item spans must be positive and stay within the explicit grid", item.id};
        }
        if (!valid_insets(item.margin)) {
            return GridError{GridErrorCode::InvalidMargin, "Item margins must be finite and non-negative", item.id};
        }
        if (!valid_alignment(item.horizontal_alignment) || !valid_alignment(item.vertical_alignment)) {
            return GridError{GridErrorCode::InvalidAlignment, "Item alignment is not a defined alignment value", item.id};
        }
        if (!is_nonnegative_finite(item.preferred_size.width) || !is_nonnegative_finite(item.preferred_size.height)) {
            return GridError{GridErrorCode::InvalidPreferredSize, "Preferred dimensions must be finite and non-negative", item.id};
        }
        if (item.baseline && (!is_nonnegative_finite(*item.baseline) || *item.baseline > item.preferred_size.height)) {
            return GridError{GridErrorCode::InvalidMeasurement, "Static baseline must lie inside preferred height", item.id};
        }
        if (index == duplicate) {
            return GridError{GridErrorCode::DuplicateItemId, "Item IDs must be unique within one layout call", item.id};
        }
    }
    return std::nullopt;
}

Size measurement_available(Size available, const Insets& padding, const Insets& margin) noexcept {
    return {static_cast<Unit>(std::max(0.0, static_cast<double>(available.width)
                                              - padding.left - padding.right - margin.left - margin.right)),
            static_cast<Unit>(std::max(0.0, static_cast<double>(available.height)
                                              - padding.top - padding.bottom - margin.top - margin.bottom))};
}

void release_axis_scratch(detail::SpanWorkspace& contributions, detail::TrackSizingWorkspace& sizing) {
    // Stateless calls have no next frame to amortize these buffers over. Keep
    // resolved tracks until output is built, but release completed work before
    // starting the other axis or allocating the item rectangles.
    contributions = {};
    sizing.allocations = std::vector<detail::WeightedAllocation>{};
    sizing.sizes = std::vector<double>{};
}

} // namespace

GridResult compute_grid(const GridSpec& spec, std::span<const GridItem> items, Size available) {
    return compute_grid(spec, items, available, nullptr, nullptr);
}

GridResult compute_grid(const GridSpec& spec, std::span<const GridItem> items, Size available,
                        MeasureCallback measure, void* user_data) {
    detail::LayoutWorkspace workspace;
    GridResult result;
    detail::compute_grid_into(spec, items, available, measure, user_data, workspace, result, false);
    return result;
}

namespace detail {

void compute_grid_into(const GridSpec& input_spec, std::span<const GridItem> input_items, Size available,
                       MeasureCallback measure, void* user_data, LayoutWorkspace& workspace, GridResult& result,
                       bool retain_scratch) {
    result.items.clear();
    result.size = {};
    // Reuse the message's storage, including the MSVC Debug string iterator
    // proxy. Assigning a temporary GridError allocates that proxy on every miss.
    result.error.code = GridErrorCode::NotComputed;
    result.error.message.clear();
    result.error.item_id.reset();
    result.measurement_count = 0;
    // Container errors keep priority, including after a prepared layout or an
    // invalid input update. Never treat a previous successful size as validation.
    if (!is_nonnegative_finite(available.width) || !is_nonnegative_finite(available.height)) {
        result.error = {GridErrorCode::InvalidContainerSize, "Container dimensions must be finite and non-negative", {}};
        return;
    }
    const bool placement_enabled = input_spec.auto_flow != AutoFlow::Explicit || input_spec.allow_implicit_tracks;
    if (placement_enabled && !workspace.placement.prepared) {
        if (!prepare_placement(input_spec, input_items, workspace.placement, result.error)) return;
    }
    const GridSpec& spec = placement_enabled ? workspace.placement.spec : input_spec;
    const std::span<const GridItem> items = placement_enabled ? std::span<const GridItem>{workspace.placement.items} : input_items;
    if (!workspace.validated) {
        if (auto error = validate_input(spec, items, workspace.ids)) {
            result.error = std::move(*error);
            return;
        }
        workspace.validated = true;
    }
    if (!retain_scratch) {
        workspace.ids = std::vector<std::pair<ItemId, std::size_t>>{};
        workspace.validated = false;
    }

    auto& measurement_count = result.measurement_count;
    auto& measurements = workspace.measurements;
    measurements.clear();
    measurements.reserve(items.size());
    for (const auto& item : items) {
        MeasureResult measured{{}, item.preferred_size, item.baseline};
        if (measure) {
            const MeasureInput input{{}, {}, measurement_available(available, spec.padding, item.margin)};
            ++measurement_count;
            measured = measure(item.id, input, user_data);
            if (!valid_measurement(measured)) {
                result.error = {GridErrorCode::InvalidMeasurement,
                                "Intrinsic measurement must be finite, non-negative, and minimum <= preferred", item.id};
                return;
            }
        }
        measurements.push_back(measured);
    }

    auto& column_contributions = workspace.column_contributions;
    collect_track_contributions(spec.columns, items, measurements, spec.column_gap, true, column_contributions,
        std::max(0.0, static_cast<double>(available.width) - spec.padding.left - spec.padding.right));
    if (!resolve_tracks(spec.columns, available.width, spec.column_gap, spec.padding.left, spec.padding.right,
                        column_contributions.contributions, column_contributions.all_fixed, workspace.columns)) {
        result.error = {GridErrorCode::NumericOverflow, "Column geometry exceeds the finite Unit range", {}};
        return;
    }
    const auto& columns = workspace.columns.result;
    if (!retain_scratch) release_axis_scratch(column_contributions, workspace.columns);

    auto& item_columns = workspace.item_columns;
    item_columns.clear();
    item_columns.reserve(items.size());
    for (const auto& item : items) {
        item_columns.push_back(spanned_track(columns.tracks, item.column, item.column_span, spec.column_gap));
    }

    if (measure) {
        for (std::size_t index = 0; index < items.size(); ++index) {
            const auto& item = items[index];
            auto& measured = measurements[index];
            const Unit width = resolved_item_width(item, item_columns[index], measured.preferred.width);
            auto space = measurement_available(available, spec.padding, item.margin);
            space.width = width;
            const MeasureInput input{width, {}, space};
            ++measurement_count;
            const auto constrained = measure(item.id, input, user_data);
            if (!valid_measurement(constrained)) {
                result.error = {GridErrorCode::InvalidMeasurement,
                                "Width-constrained measurement must be finite, non-negative, and minimum <= preferred", item.id};
                return;
            }
            // Width is already final. Only height can change in this pass;
            // accepting a new preferred width would invalidate the measured height.
            measured.minimum.height = constrained.minimum.height;
            measured.preferred.height = constrained.preferred.height;
            measured.baseline = constrained.baseline;
        }
    }

    auto& row_contributions = workspace.row_contributions;
    const bool has_baseline = std::any_of(items.begin(), items.end(), [](const GridItem& item) {
        return item.vertical_alignment == VerticalAlignment::Baseline;
    });
    if (has_baseline) {
        workspace.baseline_offsets.assign(spec.rows.size(), 0.0);
        workspace.baseline_shims.assign(items.size(), 0.0);
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (items[i].vertical_alignment != VerticalAlignment::Baseline) continue;
            const auto baseline = measurements[i].baseline.value_or(measurements[i].preferred.height);
            auto& target = workspace.baseline_offsets[items[i].row];
            target = std::max(target, static_cast<double>(items[i].margin.top) + baseline);
        }
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (items[i].vertical_alignment != VerticalAlignment::Baseline) continue;
            const auto baseline = measurements[i].baseline.value_or(measurements[i].preferred.height);
            workspace.baseline_shims[i] = std::max(0.0,
                workspace.baseline_offsets[items[i].row] - items[i].margin.top - baseline);
        }
    }
    collect_track_contributions(spec.rows, items, measurements,
        spec.row_gap, false, row_contributions,
        std::max(0.0, static_cast<double>(available.height) - spec.padding.top - spec.padding.bottom),
        has_baseline ? std::span<const double>{workspace.baseline_shims} : std::span<const double>{});
    if (!resolve_tracks(spec.rows, available.height, spec.row_gap, spec.padding.top, spec.padding.bottom,
                        row_contributions.contributions, row_contributions.all_fixed, workspace.rows)) {
        result.error = {GridErrorCode::NumericOverflow, "Row geometry exceeds the finite Unit range", {}};
        return;
    }
    const auto& rows = workspace.rows.result;
    if (!retain_scratch) release_axis_scratch(row_contributions, workspace.rows);

    result.items.reserve(items.size());
    for (std::size_t index = 0; index < items.size(); ++index) {
        const auto& item = items[index];
        const auto& column = item_columns[index];
        const auto row = spanned_track(rows.tracks, item.row, item.row_span, spec.row_gap);
        const Rect cell{static_cast<Unit>(column.offset), static_cast<Unit>(row.offset),
                        static_cast<Unit>(column.size), static_cast<Unit>(row.size)};
        auto rect = align_item(item, column, row, measurements[index].preferred);
        if (item.vertical_alignment == VerticalAlignment::Baseline) {
            const double baseline = std::min(rect.height, measurements[index].baseline.value_or(measurements[index].preferred.height));
            const double leading = std::min(row.size, static_cast<double>(item.margin.top));
            const double start = row.offset + leading;
            const double end = start + std::max(0.0, row.size - leading - item.margin.bottom - rect.height);
            rect.y = static_cast<Unit>(std::min(static_cast<double>(std::numeric_limits<Unit>::max()),
                std::clamp(row.offset + workspace.baseline_offsets[item.row] - baseline, start, end)));
        }
        result.items.push_back({item.id, rect, cell, item.column, item.row, item.column_span, item.row_span});
    }
    result.size = {columns.extent, rows.extent};
    result.error.code = GridErrorCode::None;
}

} // namespace detail
} // namespace gridly
