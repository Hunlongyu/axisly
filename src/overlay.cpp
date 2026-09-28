#include <axisly/overlay.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

#include "detail/measurement_validation.hpp"

namespace axisly {
namespace {

bool finite_nonnegative(Unit value) noexcept { return std::isfinite(value) && value >= 0; }
bool finite_insets(const Insets& insets) noexcept {
    return finite_nonnegative(insets.left) && finite_nonnegative(insets.top)
        && finite_nonnegative(insets.right) && finite_nonnegative(insets.bottom);
}
bool valid_anchor(const std::optional<AnchorLength>& anchor) noexcept {
    if (!anchor) return true;
    switch (anchor->kind) {
    case AnchorLength::Kind::Fixed:
    case AnchorLength::Kind::Percent:
        return std::isfinite(anchor->value);
    }
    return false;
}
bool valid_alignment(OverlayAlign alignment) noexcept {
    switch (alignment) {
    case OverlayAlign::Auto:
    case OverlayAlign::Start:
    case OverlayAlign::Center:
    case OverlayAlign::End:
    case OverlayAlign::Stretch:
        return true;
    }
    return false;
}
bool fits_unit(double value) noexcept {
    return std::isfinite(value) && value >= -std::numeric_limits<Unit>::max()
        && value <= std::numeric_limits<Unit>::max();
}
bool fits_size(double value) noexcept { return fits_unit(value) && value >= 0; }

double resolve(const std::optional<AnchorLength>& anchor, double basis) noexcept {
    if (!anchor) return 0;
    return anchor->kind == AnchorLength::Kind::Percent
        ? static_cast<double>(anchor->value) * basis : anchor->value;
}

struct AxisInput {
    double content_start{}, content_size{};
    std::optional<AnchorLength> start, end;
    double margin_start{}, margin_end{};
    OverlayAlign alignment{OverlayAlign::Auto};
    double preferred{}, minimum{};
    std::optional<Unit> maximum, fixed;
};

struct AxisOutput {
    double position{}, size{};
    double anchor_position{}, anchor_size{};
};

AxisOutput layout_axis(const AxisInput& input) noexcept {
    const double start_distance = resolve(input.start, input.content_size);
    const double end_distance = resolve(input.end, input.content_size);
    const double raw_start = input.content_start + (input.start ? start_distance : 0);
    const double raw_end = input.content_start + input.content_size - (input.end ? end_distance : 0);
    const double span = std::max(0.0, raw_end - raw_start);
    const double inner = std::max(0.0, span - input.margin_start - input.margin_end);

    OverlayAlign alignment = input.alignment;
    if (alignment == OverlayAlign::Auto) {
        alignment = input.start && input.end && !input.fixed ? OverlayAlign::Stretch
            : input.end && !input.start ? OverlayAlign::End : OverlayAlign::Start;
    }
    double size = input.fixed ? *input.fixed
        : alignment == OverlayAlign::Stretch ? inner : input.preferred;
    size = std::max(size, input.minimum);
    if (input.maximum) size = std::min(size, static_cast<double>(*input.maximum));

    double position = raw_start + input.margin_start;
    if (alignment == OverlayAlign::Center) {
        position += (inner - size) / 2;
    } else if (alignment == OverlayAlign::End) {
        position = raw_end - input.margin_end - size;
    }
    // When opposing anchors and a minimum size cannot fit, Start intentionally
    // wins unless the caller explicitly requested Center or End.
    return {position, size, raw_start, span};
}

OverlayResult failure(OverlayErrorCode code, const char* message,
                      std::optional<ItemId> id = {}, std::size_t measurements = 0) {
    OverlayResult result;
    result.error = {code, message, id};
    result.measurement_count = measurements;
    return result;
}

} // namespace

OverlayResult compute_overlay(const OverlaySpec& spec, std::span<const OverlayItem> items, Size available) {
    return compute_overlay(spec, items, available, nullptr, nullptr);
}

OverlayResult compute_overlay(const OverlaySpec& spec, std::span<const OverlayItem> items,
                              Size available, MeasureCallback measure, void* user_data) {
    if (!finite_nonnegative(available.width) || !finite_nonnegative(available.height))
        return failure(OverlayErrorCode::InvalidContainerSize, "Container size must be finite and non-negative");
    if (!finite_insets(spec.padding))
        return failure(OverlayErrorCode::InvalidPadding, "Padding must be finite and non-negative");
    if (!finite_insets(spec.border))
        return failure(OverlayErrorCode::InvalidBorder, "Border must be finite and non-negative");

    std::unordered_set<ItemId> ids;
    ids.reserve(items.size());
    for (const auto& item : items) {
        if (!ids.insert(item.id).second)
            return failure(OverlayErrorCode::DuplicateItemId, "Item IDs must be unique", item.id);
        if (!valid_anchor(item.anchors.left) || !valid_anchor(item.anchors.top)
            || !valid_anchor(item.anchors.right) || !valid_anchor(item.anchors.bottom))
            return failure(OverlayErrorCode::InvalidAnchor, "Anchors must be finite and valid", item.id);
        if (!finite_insets(item.margin))
            return failure(OverlayErrorCode::InvalidMargin, "Margins must be finite and non-negative", item.id);
        if (!valid_alignment(item.horizontal_alignment) || !valid_alignment(item.vertical_alignment))
            return failure(OverlayErrorCode::InvalidAlignment, "Invalid overlay alignment", item.id);
        if (!finite_nonnegative(item.minimum_size.width) || !finite_nonnegative(item.minimum_size.height)
            || !finite_nonnegative(item.preferred_size.width) || !finite_nonnegative(item.preferred_size.height)
            || (item.maximum_width && !finite_nonnegative(*item.maximum_width))
            || (item.maximum_height && !finite_nonnegative(*item.maximum_height))
            || (item.width && !finite_nonnegative(*item.width))
            || (item.height && !finite_nonnegative(*item.height))
            || (item.maximum_width && *item.maximum_width < item.minimum_size.width)
            || (item.maximum_height && *item.maximum_height < item.minimum_size.height))
            return failure(OverlayErrorCode::InvalidSize, "Invalid item size or limits", item.id);
    }

    const double content_x = static_cast<double>(spec.border.left) + spec.padding.left;
    const double content_y = static_cast<double>(spec.border.top) + spec.padding.top;
    const double content_width = std::max(0.0, static_cast<double>(available.width) - content_x
        - spec.border.right - spec.padding.right);
    const double content_height = std::max(0.0, static_cast<double>(available.height) - content_y
        - spec.border.bottom - spec.padding.bottom);
    OverlayResult result;
    result.size = available;
    result.items.reserve(items.size());
    double min_x = 0, min_y = 0, max_x = available.width, max_y = available.height;

    for (const auto& item : items) {
        MeasureResult measured{item.minimum_size, item.preferred_size, {}};
        if (measure) {
            MeasureInput input;
            input.available = {
                static_cast<Unit>(std::max(0.0, content_width - item.margin.left - item.margin.right)),
                static_cast<Unit>(std::max(0.0, content_height - item.margin.top - item.margin.bottom))
            };
            measured = measure(item.id, input, user_data);
            ++result.measurement_count;
            if (!detail::valid_measurement(measured))
                return failure(OverlayErrorCode::InvalidMeasurement, "Invalid intrinsic measurement", item.id, result.measurement_count);
        }

        const double min_width = std::max(static_cast<double>(item.minimum_size.width),
                                          static_cast<double>(measured.minimum.width));
        const double min_height = std::max(static_cast<double>(item.minimum_size.height),
                                           static_cast<double>(measured.minimum.height));
        if (item.maximum_width && min_width > *item.maximum_width)
            return failure(OverlayErrorCode::InvalidMeasurement, "Measured minimum width exceeds maximum", item.id, result.measurement_count);
        AxisInput x_input{content_x, content_width, item.anchors.left, item.anchors.right,
            item.margin.left, item.margin.right, item.horizontal_alignment,
            measured.preferred.width, min_width, item.maximum_width, item.width};
        auto x = layout_axis(x_input);
        if (!fits_size(x.size) || !fits_unit(x.position) || !fits_unit(x.anchor_position)
            || !fits_size(x.anchor_size))
            return failure(OverlayErrorCode::NumericOverflow, "Horizontal geometry overflow", item.id, result.measurement_count);

        if (measure) {
            MeasureInput input;
            input.known_width = static_cast<Unit>(x.size);
            input.available = {static_cast<Unit>(x.size),
                static_cast<Unit>(std::max(0.0, content_height - item.margin.top - item.margin.bottom))};
            measured = measure(item.id, input, user_data);
            ++result.measurement_count;
            if (!detail::valid_measurement(measured))
                return failure(OverlayErrorCode::InvalidMeasurement, "Invalid constrained measurement", item.id, result.measurement_count);
        }

        if (item.maximum_height && std::max(min_height, static_cast<double>(measured.minimum.height)) > *item.maximum_height)
            return failure(OverlayErrorCode::InvalidMeasurement, "Measured minimum height exceeds maximum", item.id, result.measurement_count);
        AxisInput y_input{content_y, content_height, item.anchors.top, item.anchors.bottom,
            item.margin.top, item.margin.bottom, item.vertical_alignment,
            measured.preferred.height, std::max(min_height, static_cast<double>(measured.minimum.height)),
            item.maximum_height, item.height};
        auto y = layout_axis(y_input);
        if (!fits_size(y.size) || !fits_unit(y.position) || !fits_unit(y.anchor_position)
            || !fits_size(y.anchor_size))
            return failure(OverlayErrorCode::NumericOverflow, "Vertical geometry overflow", item.id, result.measurement_count);
        const Rect rect{static_cast<Unit>(x.position), static_cast<Unit>(y.position),
            static_cast<Unit>(x.size), static_cast<Unit>(y.size)};
        const Rect anchors{static_cast<Unit>(x.anchor_position), static_cast<Unit>(y.anchor_position),
            static_cast<Unit>(x.anchor_size), static_cast<Unit>(y.anchor_size)};
        result.items.push_back({item.id, rect, anchors});
        min_x = std::min(min_x, x.position);
        min_y = std::min(min_y, y.position);
        max_x = std::max(max_x, x.position + x.size);
        max_y = std::max(max_y, y.position + y.size);
    }
    if (!fits_unit(min_x) || !fits_unit(min_y)
        || !fits_size(max_x - min_x) || !fits_size(max_y - min_y))
        return failure(OverlayErrorCode::NumericOverflow, "Occupied bounds overflow", {}, result.measurement_count);
    result.occupied_bounds = {static_cast<Unit>(min_x), static_cast<Unit>(min_y),
        static_cast<Unit>(max_x - min_x), static_cast<Unit>(max_y - min_y)};
    return result;
}

} // namespace axisly
