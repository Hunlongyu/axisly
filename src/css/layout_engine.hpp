#pragma once

#include <axisly/css_tree.hpp>

#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace axisly::css::detail {

struct GridContributionInput;
struct GridAxisContribution;
struct GridBaselineItem;
struct Placement;

struct LayoutNode {
    NodeId id;
    Style style;
    MeasureFunction measure;
    std::optional<NodeId> parent;
    std::vector<NodeId> children;
};

enum class FragmentDetail { Size, Baseline, Full };
enum class SizingMode { Inherent, Content };

struct Constraints {
    std::optional<Unit> known_width, known_height; // border-box
    // A parent can supply a provisional item size for the child's own
    // min/max resolution. Root Block sizing already resolves its bounds.
    bool known_width_is_final{}, known_height_is_final{};
    // A known size can come from a parent's content calculation. Flex must
    // retain that distinction when collecting its own lines.
    bool known_width_is_definite{true}, known_height_is_definite{true};
    AvailableSize available;
    std::optional<Unit> containing_width, containing_height;
    bool ignore_preferred_width{}, ignore_preferred_height{};
    bool ignore_min_max_width{}, ignore_min_max_height{};
    bool content_based_width{};
    bool preserve_known_grid_size{};
    SizingMode sizing_mode{SizingMode::Inherent};
    // Cache entries for intrinsic probes cannot stand in for published geometry.
    FragmentDetail detail{FragmentDetail::Full};
    bool operator==(const Constraints&) const = default;
};

[[nodiscard]] ResolvedDimensions requested_size(const Constraints& constraints,
    const ResolvedBox& box);

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
using CacheMap = std::unordered_map<std::uint64_t, std::vector<CacheEntry>>;

struct MeasurementCacheEntry {
    MeasureRequest request;
    Measurement result;
};
using MeasurementCacheMap =
    std::unordered_map<std::uint64_t, std::vector<MeasurementCacheEntry>>;

struct BlockContext;
struct PositionedItemInput;
struct FlexItem;
struct FlexItemSizingInput;

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
[[nodiscard]] Unit content_origin_x(const ResolvedBox& box,
    Insets border, Insets padding, NodeId node);
void set_fragment_border_size(Fragment& fragment, Size size, const ResolvedBox& box);
[[nodiscard]] NodeLayout make_layout(const LayoutNode& node, Size size,
    const ResolvedBox& resolved, std::optional<Unit> first_baseline = std::nullopt,
    std::optional<Unit> last_baseline = std::nullopt);
void translate(Fragment& fragment, Unit x, Unit y, NodeId node);
[[nodiscard]] std::optional<Unit> child_baseline(const Fragment& fragment,
    const Style& style);
void compute_scrollable_overflow(std::vector<NodeLayout>& layouts,
    const std::unordered_map<std::uint64_t, LayoutNode>& nodes);
[[nodiscard]] std::pair<Unit, Unit> relative_offset(const LayoutNode& node,
    Size containing, Direction containing_direction, NodeId parent);

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
           const CacheMap& cache, const MeasurementCacheMap& measurement_cache,
           TreeLimits limits);
    [[nodiscard]] LayoutResult compute(NodeId root, AvailableSize available);
    [[nodiscard]] CacheMap take_updates() && { return std::move(updates_); }
    [[nodiscard]] MeasurementCacheMap take_measurement_updates() && {
        return std::move(measurement_updates_);
    }

private:
    const std::unordered_map<std::uint64_t, LayoutNode>& nodes_;
    const CacheMap& cache_;
    CacheMap updates_;
    const MeasurementCacheMap& measurement_cache_;
    MeasurementCacheMap measurement_updates_;
    TreeLimits limits_;
    std::size_t measurements_{};
    std::size_t layouts_{};
    std::size_t hits_{};
    std::size_t pending_layouts_{};
    std::size_t pending_measurements_{};
    bool root_hit_{};
    FragmentDetail detail_{FragmentDetail::Full};
    std::unordered_map<std::uint64_t, bool> float_subtree_;
    void stage(NodeId id, const Constraints& constraints, const Fragment& fragment);
    [[nodiscard]] const LayoutNode& get(NodeId id) const;
    [[nodiscard]] bool has_float_subtree(NodeId id, std::size_t depth);
    [[nodiscard]] Fragment layout(NodeId id, Constraints constraints, std::size_t depth,
                                  BlockContext* block_context = nullptr);
    [[nodiscard]] Fragment layout_sizing(NodeId id, Constraints constraints,
                                         std::size_t depth);
    [[nodiscard]] Fragment layout_size(NodeId id, Constraints constraints,
                                       std::size_t depth);
    [[nodiscard]] Fragment leaf(const LayoutNode& node, const Constraints& constraints,
                                const ResolvedBox& box);
    [[nodiscard]] std::optional<Unit> intrinsic_container_size(const LayoutNode& node,
        const Constraints& constraints, const ResolvedBox& box,
        std::size_t depth, bool horizontal);
    [[nodiscard]] Fragment block(const LayoutNode& node, const Constraints& constraints,
                                 const ResolvedBox& box, std::size_t depth,
                                 BlockContext* block_context);
    [[nodiscard]] Unit block_content_based_width(const LayoutNode& node,
        AvailableSpace available, std::size_t depth);
    [[nodiscard]] Fragment flex(const LayoutNode& node, const Constraints& constraints,
                                const ResolvedBox& box, std::size_t depth);
    [[nodiscard]] FlexItem prepare_flex_item(const LayoutNode& node,
        const LayoutNode& child, const Constraints& constraints,
        const FlexItemSizingInput& input, std::size_t depth);
    [[nodiscard]] Fragment grid(const LayoutNode& node, const Constraints& constraints,
                                const ResolvedBox& box, std::size_t depth);
    [[nodiscard]] GridAxisContribution grid_axis_contribution(NodeId id,
        const GridContributionInput& input, std::size_t depth);
    [[nodiscard]] std::vector<GridBaselineItem> measure_grid_baselines(const LayoutNode& parent,
        const std::vector<Placement>& placements, ResolvedDimensions container_inner,
        std::size_t depth);
    [[nodiscard]] Fragment hidden(const LayoutNode& node, std::size_t depth);
    [[nodiscard]] Fragment absolute(const LayoutNode& parent, NodeId child,
        Size parent_size, const ResolvedBox& parent_box, std::size_t depth,
        std::optional<Rect> containing_override = std::nullopt,
        std::optional<Unit> static_block_y = std::nullopt);
    [[nodiscard]] Fragment positioned_item(const LayoutNode& child,
        const ResolvedBox& box, const PositionedItemInput& input, std::size_t depth);
    [[nodiscard]] Measurement measure(const LayoutNode& node, const MeasureRequest& request);
};

} // namespace axisly::css::detail
