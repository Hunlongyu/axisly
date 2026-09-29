#pragma once

#include <axisly/css_tree.hpp>

#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace axisly::css::detail {

struct LayoutNode {
    NodeId id;
    Style style;
    MeasureFunction measure;
    std::optional<NodeId> parent;
    std::vector<NodeId> children;
};

struct Constraints {
    std::optional<Unit> known_width, known_height; // border-box
    AvailableSize available;
    std::optional<Unit> containing_width, containing_height;
    bool ignore_preferred_width{}, ignore_preferred_height{};
    bool operator==(const Constraints&) const = default;
};

struct MarginStrut {
    Unit positive{}, negative{};
    [[nodiscard]] static MarginStrut from(Unit value);
    [[nodiscard]] MarginStrut merged(MarginStrut other) const;
    [[nodiscard]] Unit resolve(NodeId node) const;
};

struct BlockMargins {
    MarginStrut top, bottom;
    bool through{};
};

struct Fragment {
    Size size{};
    std::vector<NodeLayout> nodes;
    std::vector<GridTracks> grid_tracks;
    std::optional<BlockMargins> block_margins;
};

struct CacheEntry {
    Constraints constraints;
    Fragment fragment;
};
using CacheMap = std::unordered_map<std::uint64_t, CacheEntry>;

[[nodiscard]] Unit checked(double value, NodeId id);
[[nodiscard]] Unit edge(const ResolvedEdge& value, NodeId id);
[[nodiscard]] Insets edges(const ResolvedEdges& values, NodeId id);
[[nodiscard]] Unit content_axis(Unit border_size, Unit first_padding, Unit last_padding,
                                Unit first_border, Unit last_border);
[[nodiscard]] Unit constrain_axis(Unit value, std::optional<Unit> minimum,
                                  std::optional<Unit> maximum);
[[nodiscard]] AvailableSpace subtract_decoration(AvailableSpace space, Unit amount);
[[nodiscard]] ResolvedBox resolve_layout_box(const LayoutNode& node,
    std::optional<Unit> containing_width, std::optional<Unit> containing_height);
[[nodiscard]] NodeLayout make_layout(const LayoutNode& node, Size size,
    const ResolvedBox& resolved, std::optional<Unit> first_baseline = std::nullopt,
    std::optional<Unit> last_baseline = std::nullopt);
void translate(Fragment& fragment, Unit x, Unit y, NodeId node);
[[nodiscard]] std::pair<Unit, Unit> relative_offset(const LayoutNode& node,
    Size containing, NodeId parent);

class LayoutFailure final : public std::runtime_error {
public:
    LayoutFailure(LayoutErrorCode code, NodeId node, const char* message)
        : std::runtime_error(message), code(code), node(node) {}
    LayoutErrorCode code;
    NodeId node;
};

class Engine {
public:
    Engine(const std::unordered_map<std::uint64_t, LayoutNode>& nodes,
           const CacheMap& cache, TreeLimits limits);
    [[nodiscard]] LayoutResult compute(NodeId root, AvailableSize available);
    [[nodiscard]] CacheMap take_updates() && { return std::move(updates_); }

private:
    const std::unordered_map<std::uint64_t, LayoutNode>& nodes_;
    const CacheMap& cache_;
    CacheMap updates_;
    TreeLimits limits_;
    std::size_t measurements_{};
    std::size_t layouts_{};
    std::size_t hits_{};
    std::size_t pending_layouts_{};
    bool root_hit_{};
    void stage(NodeId id, const Constraints& constraints, const Fragment& fragment);
    [[nodiscard]] const LayoutNode& get(NodeId id) const;
    [[nodiscard]] Fragment layout(NodeId id, Constraints constraints, std::size_t depth);
    [[nodiscard]] Fragment leaf(const LayoutNode& node, const Constraints& constraints,
                                const ResolvedBox& box);
    [[nodiscard]] Fragment block(const LayoutNode& node, const Constraints& constraints,
                                 const ResolvedBox& box, std::size_t depth);
    [[nodiscard]] Fragment flex(const LayoutNode& node, const Constraints& constraints,
                                const ResolvedBox& box, std::size_t depth);
    [[nodiscard]] Fragment grid(const LayoutNode& node, const Constraints& constraints,
                                const ResolvedBox& box, std::size_t depth);
    [[nodiscard]] Fragment hidden(const LayoutNode& node, std::size_t depth);
    [[nodiscard]] Fragment absolute(const LayoutNode& parent, NodeId child,
        Size parent_size, const ResolvedBox& parent_box, std::size_t depth,
        std::optional<Rect> containing_override = std::nullopt,
        std::optional<Unit> static_block_y = std::nullopt);
    [[nodiscard]] Measurement measure(const LayoutNode& node, const MeasureRequest& request);
};

} // namespace axisly::css::detail
