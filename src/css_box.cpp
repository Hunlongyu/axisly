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

Unit safe_add(Unit first, Unit second) {
    const double sum = static_cast<double>(first) + second;
    if (!std::isfinite(sum) || sum > std::numeric_limits<Unit>::max()
        || sum < -std::numeric_limits<Unit>::max()) {
        throw std::overflow_error("CSS box dimension exceeds Unit range");
    }
    return static_cast<Unit>(sum);
}

ResolvedEdge resolve_edge(const Length& value, std::optional<Unit> basis,
                          bool allow_auto, bool allow_negative) {
    if (value.kind() == LengthKind::Auto && allow_auto) return {EdgeKind::Auto, 0};
    if (value.kind() != LengthKind::Length && value.kind() != LengthKind::Percent
        && value.kind() != LengthKind::Calc) {
        throw std::invalid_argument("Unsupported CSS edge value");
    }
    const auto numeric = value.resolve_numeric(basis);
    if (!numeric) return {EdgeKind::UnresolvedPercentage, 0};
    if (!allow_negative && *numeric < 0) {
        throw std::invalid_argument("CSS padding and border must be nonnegative");
    }
    return {EdgeKind::Numeric, *numeric};
}

ResolvedEdges resolve_edges(const EdgeValues& edges, std::optional<Unit> basis,
                            bool allow_auto, bool allow_negative) {
    return {
        resolve_edge(edges.left, basis, allow_auto, allow_negative),
        resolve_edge(edges.top, basis, allow_auto, allow_negative),
        resolve_edge(edges.right, basis, allow_auto, allow_negative),
        resolve_edge(edges.bottom, basis, allow_auto, allow_negative),
    };
}

std::optional<Unit> dimension(const Length& value, std::optional<Unit> basis,
                              std::optional<Unit> decoration, BoxSizing sizing) {
    const auto numeric = value.resolve_numeric(basis);
    if (!numeric || !decoration) return std::nullopt;
    if (sizing == BoxSizing::BorderBox) return std::max(*numeric, *decoration);
    return safe_add(std::max(*numeric, Unit{0}), *decoration);
}

std::optional<Unit> decoration(const ResolvedEdges& padding, const ResolvedEdges& border,
                               bool horizontal) {
    const auto first = horizontal ? padding.horizontal() : padding.vertical();
    const auto second = horizontal ? border.horizontal() : border.vertical();
    return first && second ? std::optional<Unit>(safe_add(*first, *second)) : std::nullopt;
}

Unit safe_nonnegative(double value) {
    if (!std::isfinite(value) || value < 0 || value > std::numeric_limits<Unit>::max()) {
        throw std::overflow_error("CSS aspect-ratio transfer exceeds Unit range");
    }
    return static_cast<Unit>(value);
}

} // namespace

std::optional<Unit> ResolvedEdge::numeric() const noexcept {
    return kind == EdgeKind::Numeric ? std::optional<Unit>(value) : std::nullopt;
}

std::optional<Unit> ResolvedEdges::horizontal() const {
    const auto l = left.numeric();
    const auto r = right.numeric();
    return l && r ? std::optional<Unit>(safe_add(*l, *r)) : std::nullopt;
}

std::optional<Unit> ResolvedEdges::vertical() const {
    const auto t = top.numeric();
    const auto b = bottom.numeric();
    return t && b ? std::optional<Unit>(safe_add(*t, *b)) : std::nullopt;
}

std::optional<Unit> ResolvedBox::horizontal_decoration() const {
    const auto value = decoration(padding, border, true);
    return value ? std::optional<Unit>(safe_add(*value, scrollbar.width)) : std::nullopt;
}

std::optional<Unit> ResolvedBox::vertical_decoration() const {
    const auto value = decoration(padding, border, false);
    return value ? std::optional<Unit>(safe_add(*value, scrollbar.height)) : std::nullopt;
}

ResolvedDimensions transfer_aspect_ratio(ResolvedDimensions size,
                                         const ResolvedBox& box) {
    if (!box.aspect_ratio || size.width.has_value() == size.height.has_value()) {
        return size;
    }
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
                        std::optional<Unit> containing_height) {
    nonnegative_basis(containing_width);
    nonnegative_basis(containing_height);
    if (style.aspect_ratio && (!std::isfinite(*style.aspect_ratio) || *style.aspect_ratio <= 0)) {
        throw std::invalid_argument("CSS aspect ratio must be finite and positive");
    }
    ResolvedBox result;
    result.margin = resolve_edges(style.margin, containing_width, true, true);
    result.padding = resolve_edges(style.padding, containing_width, false, false);
    result.border = resolve_edges(style.border, containing_width, false, false);
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
    result.preferred = transfer_aspect_ratio(result.preferred, result);
    result.minimum = transfer_aspect_ratio(result.minimum, result);
    result.maximum = transfer_aspect_ratio(result.maximum, result);
    return result;
}

Size constrain_size(Size candidate, const ResolvedBox& box) {
    if (!std::isfinite(candidate.width) || !std::isfinite(candidate.height)
        || candidate.width < 0 || candidate.height < 0) {
        throw std::invalid_argument("Candidate border-box size must be finite and nonnegative");
    }
    if (box.maximum.width) candidate.width = std::min(candidate.width, *box.maximum.width);
    if (box.maximum.height) candidate.height = std::min(candidate.height, *box.maximum.height);
    if (box.minimum.width) candidate.width = std::max(candidate.width, *box.minimum.width);
    if (box.minimum.height) candidate.height = std::max(candidate.height, *box.minimum.height);
    return candidate;
}
} // namespace axisly::css
