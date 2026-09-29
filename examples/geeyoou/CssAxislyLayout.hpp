#pragma once

#include <axisly/css_tree.hpp>
#include "geeyoou/widget/Layout.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace axisly_demo {

// A Showcase adapter for one CSS tree whose leaves are direct host widgets.
// Virtual containers permit Block/Flex/Grid nesting without creating dummy
// widgets. Like GeeyoouUI Layout, it retains child indices rather than pointers.
class CssAxislyLayout final : public geeyoou::Layout {
public:
    using Handle = std::size_t;
    explicit CssAxislyLayout(axisly::css::Style root_style);

    static constexpr Handle root() noexcept { return 0; }
    Handle addContainer(Handle parent, axisly::css::Style style);
    void addWidget(Handle parent, geeyoou::Widget* child,
                   axisly::css::Style style = {});
    void setPixelScale(float scale);

protected:
    geeyoou::SizeHint measure(const geeyoou::Widget&) const override;
    geeyoou::LayoutOverflow arrange(geeyoou::Widget&,
                                     const geeyoou::Rect&) override;
    void onInvalidated() override;
    void onChildAppended() override;
    void onChildRemoved(std::size_t index) override;

private:
    static constexpr std::size_t no_child = ~std::size_t{};
    struct NodeSpec {
        Handle parent{};
        axisly::css::Style style;
        std::size_t child{no_child};
        bool leaf{};
    };
    struct Snapshot {
        std::size_t child{};
        geeyoou::SizeHint hint;
    };
    std::vector<NodeSpec> specs_;
    std::uint64_t revision_{1};
    float pixel_scale_{1};
    mutable std::uint64_t gathered_revision_{}, gathered_style_{};
    mutable std::unique_ptr<axisly::css::LayoutTree> tree_;
    mutable std::vector<std::optional<axisly::css::NodeId>> nodes_;
    mutable std::vector<std::optional<Snapshot>> snapshots_;

    [[nodiscard]] std::size_t childIndex(const geeyoou::Widget*) const;
    [[nodiscard]] bool valid(std::uint64_t revision,
                             std::uint64_t style) const;
    void gather(const geeyoou::Widget&) const;
    [[nodiscard]] axisly::css::Measurement measureLeaf(Handle,
        const axisly::css::MeasureRequest&) const;
};

} // namespace axisly_demo
