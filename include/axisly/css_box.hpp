#pragma once

#include <axisly/css_values.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>

namespace axisly::css {

namespace detail {
[[nodiscard]] inline Unit add_box_extent(Unit first, Unit second) {
    const double sum = static_cast<double>(first) + second;
    if (!std::isfinite(sum) || sum > std::numeric_limits<Unit>::max()
        || sum < -std::numeric_limits<Unit>::max()) {
        numeric_value_overflow("CSS box dimension exceeds Unit range");
    }
    return static_cast<Unit>(sum);
}
} // namespace detail

enum class BoxSizing { BorderBox, ContentBox };
enum class Direction { Ltr, Rtl };

struct Dimensions {
    Length width{Length::automatic()};
    Length height{Length::automatic()};
    bool operator==(const Dimensions&) const = default;
};

struct EdgeValues {
    Length left{Length::length(0)};
    Length top{Length::length(0)};
    Length right{Length::length(0)};
    Length bottom{Length::length(0)};
    bool operator==(const EdgeValues&) const = default;
};

struct BoxStyle {
    BoxSizing box_sizing{BoxSizing::BorderBox};
    Direction direction{Direction::Ltr};
    bool replaced{};
    Dimensions size;
    Dimensions min_size;
    Dimensions max_size;
    std::optional<Unit> aspect_ratio;
    EdgeValues margin;
    EdgeValues padding;
    EdgeValues border;
    bool operator==(const BoxStyle&) const = default;
};

enum class EdgeKind { Numeric, Auto, UnresolvedPercentage };
enum class IndefiniteEdgeBasis { Unresolved, Zero };

struct ResolvedEdge {
    EdgeKind kind{EdgeKind::Numeric};
    Unit value{};
    [[nodiscard]] std::optional<Unit> numeric() const noexcept {
        return kind == EdgeKind::Numeric ? std::optional<Unit>(value) : std::nullopt;
    }
    bool operator==(const ResolvedEdge&) const = default;
};

struct ResolvedEdges {
    ResolvedEdge left, top, right, bottom;
    [[nodiscard]] std::optional<Unit> horizontal() const {
        return left.kind == EdgeKind::Numeric && right.kind == EdgeKind::Numeric
            ? std::optional<Unit>(detail::add_box_extent(left.value, right.value)) : std::nullopt;
    }
    [[nodiscard]] std::optional<Unit> vertical() const {
        return top.kind == EdgeKind::Numeric && bottom.kind == EdgeKind::Numeric
            ? std::optional<Unit>(detail::add_box_extent(top.value, bottom.value)) : std::nullopt;
    }
    bool operator==(const ResolvedEdges&) const = default;
};

struct ResolvedDimensions {
    std::optional<Unit> width, height;
    bool operator==(const ResolvedDimensions&) const = default;
};

struct ResolvedBox {
    ResolvedEdges margin, padding, border;
    Size scrollbar{};
    // These are border-box sizes. An absent value remains under algorithm control:
    // auto, intrinsic keywords and percentages with an indefinite basis differ.
    ResolvedDimensions preferred, minimum, maximum;
    // Constraints before aspect-ratio transfer. Flex uses these to resolve
    // flexible lengths; transferred constraints only bound hypothetical sizes.
    ResolvedDimensions specified_minimum, specified_maximum;
    std::optional<Unit> aspect_ratio;
    BoxSizing box_sizing{BoxSizing::BorderBox};
    Direction direction{Direction::Ltr};
    bool replaced{};
    // Padding and border set the minimum border-box size. A scrollbar reserves
    // content space but does not enlarge that minimum.
    [[nodiscard]] std::optional<Unit> horizontal_padding_border() const {
        const auto p = padding.horizontal(), b = border.horizontal();
        return p && b ? std::optional<Unit>(detail::add_box_extent(*p, *b)) : std::nullopt;
    }
    [[nodiscard]] std::optional<Unit> vertical_padding_border() const {
        const auto p = padding.vertical(), b = border.vertical();
        return p && b ? std::optional<Unit>(detail::add_box_extent(*p, *b)) : std::nullopt;
    }
    [[nodiscard]] std::optional<Unit> horizontal_decoration() const {
        const auto value = horizontal_padding_border();
        return value ? std::optional<Unit>(detail::add_box_extent(*value, scrollbar.width)) : std::nullopt;
    }
    [[nodiscard]] std::optional<Unit> vertical_decoration() const {
        const auto value = vertical_padding_border();
        return value ? std::optional<Unit>(detail::add_box_extent(*value, scrollbar.height)) : std::nullopt;
    }
};

namespace detail {
[[nodiscard]] ResolvedDimensions transfer_definite_aspect_ratio(
    ResolvedDimensions size, const ResolvedBox& box);
} // namespace detail

// Transfer a single definite axis through aspect-ratio. Inputs and output are
// border-box sizes; the box-sizing mode determines the ratio's reference box.
[[nodiscard]] inline ResolvedDimensions transfer_aspect_ratio(
    ResolvedDimensions size, const ResolvedBox& box) {
    if (!box.aspect_ratio || size.width.has_value() == size.height.has_value()) return size;
    return detail::transfer_definite_aspect_ratio(size, box);
}

// Percentage padding, border and margin use the containing block's inline size.
// Width and height percentages use the corresponding containing-block axis.
// By default, unknown bases stay explicit. Layout algorithms may instead
// resolve percentage edges against zero during an intrinsic-size pass.
[[nodiscard]] ResolvedBox resolve_box(const BoxStyle& style,
    std::optional<Unit> containing_width, std::optional<Unit> containing_height,
    IndefiniteEdgeBasis edge_basis = IndefiniteEdgeBasis::Unresolved);

// Resolve min/max in CSS order: max size first, then min size, so min wins if
// min exceeds max. The caller supplies the content-derived border-box size.
[[nodiscard]] inline Size constrain_size(Size candidate, const ResolvedBox& box) {
    if (!std::isfinite(candidate.width) || !std::isfinite(candidate.height)
        || candidate.width < 0 || candidate.height < 0) {
        detail::invalid_numeric_value("Candidate border-box size must be finite and nonnegative");
    }
    if (box.maximum.width) candidate.width = std::min(candidate.width, *box.maximum.width);
    if (box.maximum.height) candidate.height = std::min(candidate.height, *box.maximum.height);
    if (box.minimum.width) candidate.width = std::max(candidate.width, *box.minimum.width);
    if (box.minimum.height) candidate.height = std::max(candidate.height, *box.minimum.height);
    return candidate;
}

} // namespace axisly::css
