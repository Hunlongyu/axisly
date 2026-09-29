#pragma once

#include <axisly/css_measurement.hpp>
#include <axisly/css_style.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace axisly::css {

struct NodeId {
    std::uint64_t value{};
    bool operator==(const NodeId&) const = default;
};

struct AvailableSize {
    AvailableSpace width{AvailableSpace::indefinite()};
    AvailableSpace height{AvailableSpace::indefinite()};
    bool operator==(const AvailableSize&) const = default;
};

struct TreeLimits {
    std::size_t maximum_depth{64};
    std::size_t maximum_nodes{100'000};
    std::size_t maximum_measurements{100'000};
    std::size_t maximum_cached_layouts{100'000};
};

enum class LayoutErrorCode {
    None, InvalidRoot, InvalidStyle, Unsupported, UnresolvedPercentage,
    InvalidMeasurement, NumericOverflow, LimitExceeded,
};

struct LayoutError {
    LayoutErrorCode code{LayoutErrorCode::None};
    std::optional<NodeId> node;
    std::string message;
};

struct NodeLayout {
    NodeId node;
    std::optional<NodeId> parent;
    // Root-relative border and content rectangles; margin is outside border.
    Rect border_box{}, content_box{};
    Insets margin{}, padding{}, border{};
    // Origin used when child overflow propagates. A content-sized Block may
    // resolve cyclic percentage borders after its reported edges are frozen.
    Insets overflow_origin_border{};
    Size scrollbar{};
    std::optional<Unit> first_baseline, last_baseline;
    // Node-local scrollable region; unlike border_box, it is not root-relative.
    Rect scrollable_overflow_rect{};
};

struct GridTracks {
    NodeId node;
    std::int32_t row_origin{}, column_origin{};
    std::vector<Unit> rows, columns;
};

struct LayoutResult {
    Size size{};
    std::vector<NodeLayout> nodes; // Root first, then depth-first order.
    std::vector<GridTracks> grid_tracks;
    LayoutError error;
    std::size_t measurement_count{};
    std::size_t layout_count{};
    std::size_t cache_hits{};
    bool cache_hit{};
    [[nodiscard]] bool succeeded() const noexcept { return error.code == LayoutErrorCode::None; }
    [[nodiscard]] const NodeLayout* find(NodeId node) const noexcept;
};

// Retained CSS layout tree. Host measurement callbacks are stored by value.
// Mutations and reentrant compute from a callback are rejected.
class LayoutTree {
public:
    explicit LayoutTree(TreeLimits limits = {});
    ~LayoutTree();
    LayoutTree(const LayoutTree&) = delete;
    LayoutTree& operator=(const LayoutTree&) = delete;
    LayoutTree(LayoutTree&&) = delete;
    LayoutTree& operator=(LayoutTree&&) = delete;

    [[nodiscard]] NodeId create_node(Style style = {}, MeasureFunction measure = {});
    [[nodiscard]] bool contains(NodeId node) const noexcept;
    [[nodiscard]] const Style& style(NodeId node) const;
    [[nodiscard]] std::optional<NodeId> parent(NodeId node) const;
    [[nodiscard]] std::span<const NodeId> children(NodeId node) const;
    void set_style(NodeId node, Style style);
    void set_measure(NodeId node, MeasureFunction measure);
    void insert_child(NodeId parent, std::size_t index, NodeId child);
    void append_child(NodeId parent, NodeId child);
    void detach(NodeId child);
    void remove(NodeId node);
    void mark_dirty(NodeId node);

    // A failed computation publishes an empty result with an error. Host
    // callback exceptions propagate without publishing partial geometry.
    [[nodiscard]] const LayoutResult& compute(NodeId root, AvailableSize available) &;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace axisly::css
