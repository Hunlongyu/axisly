#pragma once

#include <axisly/css_box.hpp>

namespace axisly::css::detail {

// Layout stages may select different bases for item dimensions and margins.
// Resolve only the affected component instead of rebuilding a BoxStyle and
// resolving all twelve edges and six dimensions again.
void resolve_box_dimensions(ResolvedBox& box, const BoxStyle& style,
    std::optional<Unit> width, std::optional<Unit> height);
[[nodiscard]] ResolvedEdges resolve_box_margins(const EdgeValues& values,
    std::optional<Unit> width,
    IndefiniteEdgeBasis edge_basis = IndefiniteEdgeBasis::Unresolved);

} // namespace axisly::css::detail
