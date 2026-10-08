#include <axisly/css_box.hpp>
#include "css/box_resolution.hpp"
#include "css/layout_box.hpp"
#include "css/numeric_basis.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace axisly::css {
namespace {
void nonnegative_basis(std::optional<Unit> value) {
    if (value && (!std::isfinite(*value) || *value < 0)) {
        throw std::invalid_argument("Containing-block size must be finite and nonnegative");
    }
}

template<class Basis>
ResolvedEdge resolve_edge(const Length& value, const Basis& basis,
                          bool allow_auto, bool allow_negative,
                          IndefiniteEdgeBasis indefinite) {
    if (value.kind() == LengthKind::Auto && allow_auto) return {EdgeKind::Auto, 0};
    if (value.kind() != LengthKind::Length && value.kind() != LengthKind::Percent
        && value.kind() != LengthKind::Calc) {
        throw std::invalid_argument("Unsupported CSS edge value");
    }
    const auto numeric = basis.resolve(value);
    if (!numeric) return indefinite == IndefiniteEdgeBasis::Zero
        ? ResolvedEdge{EdgeKind::Numeric, 0}
        : ResolvedEdge{EdgeKind::UnresolvedPercentage, 0};
    if (!allow_negative && *numeric < 0) {
        throw std::invalid_argument("CSS padding and border must be nonnegative");
    }
    return {EdgeKind::Numeric, *numeric};
}

template<class Basis>
ResolvedEdges resolve_edges(const EdgeValues& edges, const Basis& basis,
                            bool allow_auto, bool allow_negative,
                            IndefiniteEdgeBasis indefinite) {
    return {
        resolve_edge(edges.left, basis, allow_auto, allow_negative, indefinite),
        resolve_edge(edges.top, basis, allow_auto, allow_negative, indefinite),
        resolve_edge(edges.right, basis, allow_auto, allow_negative, indefinite),
        resolve_edge(edges.bottom, basis, allow_auto, allow_negative, indefinite),
    };
}

template<class Basis>
std::optional<Unit> dimension(const Length& value, const Basis& basis,
                              Unit decoration, BoxSizing sizing) {
    const auto numeric = basis.resolve(value);
    if (!numeric) return std::nullopt;
    if (sizing == BoxSizing::BorderBox) return std::max(*numeric, decoration);
    return detail::add_box_extent(std::max(*numeric, Unit{0}), decoration);
}

template<class Basis>
std::optional<Unit> dimension(const Length& value, const Basis& basis,
                              std::optional<Unit> decoration, BoxSizing sizing) {
    const auto numeric = basis.resolve(value);
    if (!numeric || !decoration) return std::nullopt;
    if (sizing == BoxSizing::BorderBox) return std::max(*numeric, *decoration);
    return detail::add_box_extent(std::max(*numeric, Unit{0}), *decoration);
}

Unit safe_nonnegative(double value) {
    if (!std::isfinite(value) || value < 0 || value > std::numeric_limits<Unit>::max()) {
        throw std::overflow_error("CSS aspect-ratio transfer exceeds Unit range");
    }
    return static_cast<Unit>(value);
}

ResolvedDimensions transfer_ratio(ResolvedDimensions size, std::optional<Unit> horizontal,
    std::optional<Unit> vertical, double ratio, BoxSizing sizing) {
    if (!horizontal || !vertical) return size;
    if (size.width) {
        const double base = sizing == BoxSizing::ContentBox
            ? std::max(0.0, static_cast<double>(*size.width) - *horizontal)
            : *size.width;
        const double height = base / ratio
            + (sizing == BoxSizing::ContentBox ? *vertical : 0.0);
        size.height = std::max(safe_nonnegative(height), *vertical);
    } else {
        const double base = sizing == BoxSizing::ContentBox
            ? std::max(0.0, static_cast<double>(*size.height) - *vertical)
            : *size.height;
        const double width = base * ratio
            + (sizing == BoxSizing::ContentBox ? *horizontal : 0.0);
        size.width = std::max(safe_nonnegative(width), *horizontal);
    }
    return size;
}

template<class Box, class Basis>
void resolve_dimensions(Box& result, const BoxStyle& style,
    const Basis& width_basis, const Basis& height_basis) {
    const auto horizontal = result.horizontal_decoration();
    const auto vertical = result.vertical_decoration();
    const auto convert = [&](const Dimensions& input) {
        return ResolvedDimensions{
            dimension(input.width, width_basis, horizontal, style.box_sizing),
            dimension(input.height, height_basis, vertical, style.box_sizing),
        };
    };
    result.preferred = convert(style.size);
    result.minimum = convert(style.min_size);
    result.maximum = convert(style.max_size);
    result.specified_minimum = result.minimum;
    result.specified_maximum = result.maximum;
    result.preferred = transfer_aspect_ratio(result.preferred, result);
    result.minimum = transfer_aspect_ratio(result.minimum, result);
}

void validate_resolution(const BoxStyle& style, std::optional<Unit> width,
    std::optional<Unit> height) {
    nonnegative_basis(width);
    nonnegative_basis(height);
    if (style.aspect_ratio && (!std::isfinite(*style.aspect_ratio) || *style.aspect_ratio <= 0)) {
        throw std::invalid_argument("CSS aspect ratio must be finite and positive");
    }
}

template<class Basis>
detail::LayoutBox numeric_box(const BoxStyle& style, const Basis& width, const Basis& height) {
    const auto decoration = [&](const EdgeValues& edges) -> Insets {
        return {
            resolve_edge(edges.left, width, false, false, IndefiniteEdgeBasis::Zero).value,
            resolve_edge(edges.top, width, false, false, IndefiniteEdgeBasis::Zero).value,
            resolve_edge(edges.right, width, false, false, IndefiniteEdgeBasis::Zero).value,
            resolve_edge(edges.bottom, width, false, false, IndefiniteEdgeBasis::Zero).value,
        };
    };
    detail::LayoutBox result;
    result.margin = resolve_edges(style.margin, width, true, true, IndefiniteEdgeBasis::Zero);
    result.set_decoration(decoration(style.padding), decoration(style.border));
    result.aspect_ratio = style.aspect_ratio;
    result.box_sizing = style.box_sizing;
    result.direction = style.direction;
    result.replaced = style.replaced;
    resolve_dimensions(result, style, width, height);
    return result;
}

} // namespace

ResolvedDimensions detail::transfer_definite_aspect_ratio(ResolvedDimensions size,
    const ResolvedBox& box) {
    return transfer_ratio(size, box.horizontal_decoration(), box.vertical_decoration(),
        *box.aspect_ratio, box.box_sizing);
}

ResolvedDimensions detail::transfer_definite_aspect_ratio(ResolvedDimensions size,
    const LayoutBox& box) {
    return transfer_ratio(size, box.horizontal_decoration(), box.vertical_decoration(),
        *box.aspect_ratio, box.box_sizing);
}

ResolvedBox resolve_box(const BoxStyle& style,
                        std::optional<Unit> containing_width,
                        std::optional<Unit> containing_height,
                        IndefiniteEdgeBasis edge_basis) {
    validate_resolution(style, containing_width, containing_height);
    ResolvedBox result;
    const detail::NumericBasis basis(containing_width);
    result.margin = resolve_edges(style.margin, basis, true, true, edge_basis);
    result.padding = resolve_edges(style.padding, basis, false, false, edge_basis);
    result.border = resolve_edges(style.border, basis, false, false, edge_basis);
    result.aspect_ratio = style.aspect_ratio;
    result.box_sizing = style.box_sizing;
    result.direction = style.direction;
    result.replaced = style.replaced;
    detail::resolve_box_dimensions(result, style, containing_width, containing_height);
    return result;
}

ResolvedEdges detail::resolve_box_margins(const EdgeValues& values,
                                         std::optional<Unit> width,
                                         IndefiniteEdgeBasis edge_basis) {
    return resolve_edges(values, NumericBasis(width), true, true, edge_basis);
}

void detail::resolve_box_dimensions(ResolvedBox& result, const BoxStyle& style,
                                   std::optional<Unit> containing_width,
                                   std::optional<Unit> containing_height) {
    resolve_dimensions(result, style, NumericBasis(containing_width), NumericBasis(containing_height));
}

void detail::resolve_box_dimensions(LayoutBox& result, const BoxStyle& style,
    std::optional<Unit> width, std::optional<Unit> height) {
    resolve_dimensions(result, style, NumericBasis(width), NumericBasis(height));
}

detail::LayoutBox detail::resolve_numeric_box(const BoxStyle& style,
    std::optional<Unit> width, std::optional<Unit> height) {
    validate_resolution(style, width, height);
    // Nodes normalize unused containing sizes to an indefinite basis. Compile
    // that common resolver separately so fixed boxes do not carry optional
    // percentage/calc branches through every field. Both paths use the same
    // edge, dimension, ratio and overflow operations above.
    if (!width && !height) return numeric_box(style, IndefiniteBasis{}, IndefiniteBasis{});
    return numeric_box(style, NumericBasis(width), NumericBasis(height));
}

} // namespace axisly::css
