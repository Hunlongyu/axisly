#include "layout_fragment.hpp"

#include <cmath>

namespace axisly::css::detail {
namespace {
// CSS coordinates round ties towards positive infinity, including negatives.
// Keep edge arithmetic at Unit precision before snapping, as in layout itself.
class AxisSnap {
public:
    AxisSnap(Unit position, Unit origin, Unit scale, NodeId node)
        : position_(checked(static_cast<double>(position) + origin, node)),
          scale_(scale), node_(node), start_(pixel(position_)) {}

    Unit offset(Unit offset) const { return distance(start_, pixel(add(position_, offset))); }
    Unit extent(Unit offset, Unit size) const {
        return distance(pixel(add(position_, offset)), pixel(add(position_, add(offset, size))));
    }
    Unit trailing(Unit size, Unit inset) const {
        const auto end = add(position_, size);
        return distance(pixel(add(end, -inset)), pixel(end));
    }
    Unit scalar(Unit value) const { return distance(0, pixel(value)); }

private:
    Unit position_, scale_;
    NodeId node_;
    Unit start_;

    Unit add(Unit a, Unit b) const { return checked(static_cast<double>(a) + b, node_); }
    Unit pixel(Unit value) const {
        const auto scaled = checked(static_cast<double>(value) * scale_, node_);
        return std::floor(add(scaled, Unit{0.5}));
    }
    Unit distance(Unit from, Unit to) const {
        return checked((static_cast<double>(to) - from) / scale_, node_);
    }
};

} // namespace

NodeLayout round_node_layout(const NodeLayout& raw, LayoutPosition logical_parent,
                            LayoutPosition pixel_parent, PixelRounding options, bool root) {
    const auto add = [&](Unit a, Unit b) { return checked(static_cast<double>(a) + b, raw.node); };
    const LayoutPosition logical{add(logical_parent.x, raw.border_box.x), add(logical_parent.y, raw.border_box.y)};
    const AxisSnap x(logical.x, options.origin_x, options.scale, raw.node);
    const AxisSnap y(logical.y, options.origin_y, options.scale, raw.node);
    const auto local_x = root
        ? AxisSnap(0, options.origin_x, options.scale, raw.node).offset(raw.border_box.x)
        : x.scalar(raw.border_box.x);
    const auto local_y = root
        ? AxisSnap(0, options.origin_y, options.scale, raw.node).offset(raw.border_box.y)
        : y.scalar(raw.border_box.y);
    auto rounded = raw;
    rounded.border_box = {add(pixel_parent.x, local_x), add(pixel_parent.y, local_y),
        x.offset(raw.border_box.width), y.offset(raw.border_box.height)};
    const auto insets = [&](Insets value) -> Insets {
        return {x.offset(value.left), y.offset(value.top),
            x.trailing(raw.border_box.width, value.right), y.trailing(raw.border_box.height, value.bottom)};
    };
    rounded.border = insets(raw.border);
    rounded.padding = insets(raw.padding);
    rounded.scrollbar = {x.scalar(raw.scrollbar.width), y.scalar(raw.scrollbar.height)};
    const Unit content_x = add(raw.content_box.x, -raw.border_box.x);
    const Unit content_y = add(raw.content_box.y, -raw.border_box.y);
    rounded.content_box = {add(rounded.border_box.x, x.offset(content_x)),
        add(rounded.border_box.y, y.offset(content_y)),
        x.extent(content_x, raw.content_box.width), y.extent(content_y, raw.content_box.height)};
    const auto& overflow = raw.scrollable_overflow_rect;
    rounded.scrollable_overflow_rect = {x.offset(overflow.x), y.offset(overflow.y),
        x.extent(overflow.x, overflow.width), y.extent(overflow.y, overflow.height)};
    if (raw.first_baseline) rounded.first_baseline = y.offset(*raw.first_baseline);
    if (raw.last_baseline) rounded.last_baseline = y.offset(*raw.last_baseline);
    return rounded;
}

namespace {
template<class Output>
void append(Output& output, const NodeLayout& raw,
            const FragmentChildren* children, LayoutPosition logical_parent,
            LayoutPosition pixel_parent, PixelRounding options, bool root) {
    const auto rounded = round_node_layout(raw, logical_parent, pixel_parent, options, root);
    output.push_back(rounded);

    // Each stored child root is already in its parent's local coordinates.
    // Summing those originals avoids subtracting large flattened coordinates,
    // and does not apply the deferred whole-subtree translations a second time.
    if (!children) return;
    const LayoutPosition logical{
        checked(static_cast<double>(logical_parent.x) + raw.border_box.x, raw.node),
        checked(static_cast<double>(logical_parent.y) + raw.border_box.y, raw.node)};
    const LayoutPosition published{rounded.border_box.x, rounded.border_box.y};
    if (children->branched) {
        for (const auto& child : children->branches) {
            append(output, child.root, child.descendants.get(), logical, published, options, false);
        }
    } else {
        for (const auto& child : children->leaves) {
            append(output, child, nullptr, logical, published, options, false);
        }
    }
}
} // namespace

void Fragment::validate_rounding(PixelRounding rounding) const {
    struct Sink { void push_back(const NodeLayout&) {} } output;
    if (root) append(output, root_layout(), descendants.get(), {}, {}, rounding, true);
}

std::vector<NodeLayout> Fragment::rounded_nodes(std::vector<NodeLayout> output,
                                               PixelRounding rounding) const {
    output.clear();
    output.reserve(node_count());
    if (root) append(output, root_layout(), descendants.get(), {}, {}, rounding, true);
    return output;
}
} // namespace axisly::css::detail
