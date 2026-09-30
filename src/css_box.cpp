#include <axisly/css_box.hpp>

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

ResolvedEdge resolve_edge(const Length& value, std::optional<Unit> basis,
                          bool allow_auto, bool allow_negative,
                          IndefiniteEdgeBasis indefinite) {
    if (value.kind() == LengthKind::Auto && allow_auto) return {EdgeKind::Auto, 0};
    if (value.kind() != LengthKind::Length && value.kind() != LengthKind::Percent
        && value.kind() != LengthKind::Calc) {
        throw std::invalid_argument("Unsupported CSS edge value");
    }
    const auto numeric = value.resolve_numeric(basis);
    if (!numeric) return indefinite == IndefiniteEdgeBasis::Zero
        ? ResolvedEdge{EdgeKind::Numeric, 0}
        : ResolvedEdge{EdgeKind::UnresolvedPercentage, 0};
    if (!allow_negative && *numeric < 0) {
        throw std::invalid_argument("CSS padding and border must be nonnegative");
    }
    return {EdgeKind::Numeric, *numeric};
}

ResolvedEdges resolve_edges(const EdgeValues& edges, std::optional<Unit> basis,
                            bool allow_auto, bool allow_negative,
                            IndefiniteEdgeBasis indefinite) {
    return {
        resolve_edge(edges.left, basis, allow_auto, allow_negative, indefinite),
        resolve_edge(edges.top, basis, allow_auto, allow_negative, indefinite),
        resolve_edge(edges.right, basis, allow_auto, allow_negative, indefinite),
        resolve_edge(edges.bottom, basis, allow_auto, allow_negative, indefinite),
    };
}

std::optional<Unit> dimension(const Length& value, std::optional<Unit> basis,
                              std::optional<Unit> decoration, BoxSizing sizing) {
    const auto numeric = value.resolve_numeric(basis);
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

} // namespace

ResolvedDimensions detail::transfer_definite_aspect_ratio(ResolvedDimensions size,
                                                         const ResolvedBox& box) {
    const auto horizontal = box.horizontal_decoration();
    const auto vertical = box.vertical_decoration();
    if (!horizontal || !vertical) return size;
    const double ratio = *box.aspect_ratio;
    if (size.width) {
        const double base = box.box_sizing == BoxSizing::ContentBox
            ? std::max(0.0, static_cast<double>(*size.width) - *horizontal)
            : *size.width;
        const double height = base / ratio
            + (box.box_sizing == BoxSizing::ContentBox ? *vertical : 0.0);
        size.height = std::max(safe_nonnegative(height), *vertical);
    } else {
        const double base = box.box_sizing == BoxSizing::ContentBox
            ? std::max(0.0, static_cast<double>(*size.height) - *vertical)
            : *size.height;
        const double width = base * ratio
            + (box.box_sizing == BoxSizing::ContentBox ? *horizontal : 0.0);
        size.width = std::max(safe_nonnegative(width), *horizontal);
    }
    return size;
}

ResolvedBox resolve_box(const BoxStyle& style,
                        std::optional<Unit> containing_width,
                        std::optional<Unit> containing_height,
                        IndefiniteEdgeBasis edge_basis) {
    nonnegative_basis(containing_width);
    nonnegative_basis(containing_height);
    if (style.aspect_ratio && (!std::isfinite(*style.aspect_ratio) || *style.aspect_ratio <= 0)) {
        throw std::invalid_argument("CSS aspect ratio must be finite and positive");
    }
    ResolvedBox result;
    result.margin = resolve_edges(style.margin, containing_width, true, true, edge_basis);
    result.padding = resolve_edges(style.padding, containing_width, false, false, edge_basis);
    result.border = resolve_edges(style.border, containing_width, false, false, edge_basis);
    result.aspect_ratio = style.aspect_ratio;
    result.box_sizing = style.box_sizing;
    result.direction = style.direction;
    result.replaced = style.replaced;
    const auto horizontal = result.horizontal_decoration();
    const auto vertical = result.vertical_decoration();
    const auto convert = [&](const Dimensions& input) {
        return ResolvedDimensions{
            dimension(input.width, containing_width, horizontal, style.box_sizing),
            dimension(input.height, containing_height, vertical, style.box_sizing),
        };
    };
    result.preferred = convert(style.size);
    result.minimum = convert(style.min_size);
    result.maximum = convert(style.max_size);
    result.specified_minimum = result.minimum;
    result.specified_maximum = result.maximum;
    result.preferred = transfer_aspect_ratio(result.preferred, result);
    result.minimum = transfer_aspect_ratio(result.minimum, result);
    return result;
}

} // namespace axisly::css
