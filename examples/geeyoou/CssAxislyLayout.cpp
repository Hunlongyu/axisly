#include "CssAxislyLayout.hpp"

#include "geeyoou/render/StyleSheet.hpp"
#include "geeyoou/widget/Label.hpp"
#include "geeyoou/widget/Widget.hpp"
#include "geeyoou/widget/Window.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <utility>

namespace axisly_demo {
namespace {
struct Interrupted {};

float finite_nonnegative(float value) {
    if (!std::isfinite(value)) throw Interrupted{};
    return std::max(0.0f, value);
}

geeyoou::SizeHint normalize(geeyoou::SizeHint hint) {
    const auto axis = [](float& minimum, float& preferred, float& maximum) {
        minimum = finite_nonnegative(minimum);
        maximum = std::max(minimum, finite_nonnegative(maximum));
        preferred = std::clamp(finite_nonnegative(preferred), minimum, maximum);
    };
    axis(hint.min.width, hint.preferred.width, hint.max.width);
    axis(hint.min.height, hint.preferred.height, hint.max.height);
    return hint;
}
} // namespace

CssAxislyLayout::CssAxislyLayout(axisly::css::Style root_style) {
    axisly::css::validate_style(root_style);
    specs_.push_back({0, std::move(root_style), no_child, false});
}

std::size_t CssAxislyLayout::childIndex(const geeyoou::Widget* child) const {
    if (!host()) throw std::logic_error("Adopt CSS layout before adding children");
    const auto& children = host()->children();
    for (std::size_t index = 0; index < children.size(); ++index) {
        if (children[index].get() == child) return index;
    }
    throw std::invalid_argument("CSS layout requires a direct host child");
}

CssAxislyLayout::Handle CssAxislyLayout::addContainer(
    Handle parent, axisly::css::Style style) {
    if (parent >= specs_.size() || specs_[parent].leaf)
        throw std::invalid_argument("Invalid CSS container parent");
    axisly::css::validate_style(style);
    const auto result = specs_.size();
    specs_.push_back({parent, std::move(style), no_child, false});
    invalidate();
    return result;
}

void CssAxislyLayout::addWidget(Handle parent, geeyoou::Widget* child,
                                 axisly::css::Style style) {
    if (parent >= specs_.size() || specs_[parent].leaf)
        throw std::invalid_argument("Invalid CSS widget parent");
    axisly::css::validate_style(style);
    const auto index = childIndex(child);
    if (std::any_of(specs_.begin(), specs_.end(), [index](const NodeSpec& spec) {
        return spec.leaf && spec.child == index;
    })) throw std::invalid_argument("Widget already belongs to CSS layout");
    specs_.push_back({parent, std::move(style), index, true});
    invalidate();
}

void CssAxislyLayout::setPixelScale(float scale) {
    if (!std::isfinite(scale) || scale <= 0)
        throw std::invalid_argument("Invalid CSS host pixel scale");
    if (scale == pixel_scale_) return;
    pixel_scale_ = scale;
    invalidate();
}

void CssAxislyLayout::onInvalidated() { ++revision_; }
void CssAxislyLayout::onChildAppended() { ++revision_; }
void CssAxislyLayout::onChildRemoved(std::size_t child) {
    for (auto& spec : specs_) {
        if (!spec.leaf || spec.child == no_child) continue;
        if (spec.child == child) spec.child = no_child;
        else if (spec.child > child) --spec.child;
    }
    ++revision_;
}

bool CssAxislyLayout::valid(std::uint64_t revision, std::uint64_t style) const {
    return hostAlive() && revision_ == revision
        && geeyoou::styleGeneration() == style;
}

void CssAxislyLayout::gather(const geeyoou::Widget& host_widget) const {
    const auto revision = revision_;
    const auto style = geeyoou::styleGeneration();
    if (tree_ && gathered_revision_ == revision && gathered_style_ == style) return;

    std::vector<std::optional<Snapshot>> snapshots(specs_.size());
    for (std::size_t index = 1; index < specs_.size(); ++index) {
        const auto child_index = specs_[index].child;
        if (!specs_[index].leaf || child_index == no_child) continue;
        if (child_index >= host_widget.children().size()) throw Interrupted{};
        const auto* child = host_widget.children()[child_index].get();
        if (!child->isVisible()) continue;
        const geeyoou::detail::DeathWatch watch(child);
        const auto hint = child->sizeHint();
        if (!valid(revision, style) || !watch.alive()) throw Interrupted{};
        snapshots[index] = Snapshot{child_index, normalize(hint)};
    }

    auto tree = std::make_unique<axisly::css::LayoutTree>();
    std::vector<std::optional<axisly::css::NodeId>> nodes(specs_.size());
    for (std::size_t index = 0; index < specs_.size(); ++index) {
        if (specs_[index].leaf && !snapshots[index]) continue;
        const auto id = specs_[index].leaf
            ? tree->create_node(specs_[index].style,
                [this, index](const axisly::css::MeasureRequest& request) {
                    return measureLeaf(index, request);
                })
            : tree->create_node(specs_[index].style);
        nodes[index] = id;
        if (index) tree->append_child(*nodes[specs_[index].parent], id);
    }
    if (!valid(revision, style)) throw Interrupted{};
    snapshots_ = std::move(snapshots);
    nodes_ = std::move(nodes);
    tree_ = std::move(tree);
    gathered_revision_ = revision;
    gathered_style_ = style;
}

axisly::css::Measurement CssAxislyLayout::measureLeaf(
    Handle handle, const axisly::css::MeasureRequest& request) const {
    const auto revision = gathered_revision_;
    const auto style = gathered_style_;
    if (!valid(revision, style)) throw Interrupted{};
    const auto snapshot = *snapshots_.at(handle);
    auto width = snapshot.hint.preferred.width;
    if (request.available_width.kind == axisly::css::AvailableKind::MinContent)
        width = snapshot.hint.min.width;
    else if (const auto definite = request.available_width.definite_value())
        width = std::min(width, *definite);
    width = request.known_content_width.value_or(width);
    auto height = request.available_height.kind == axisly::css::AvailableKind::MinContent
        ? snapshot.hint.min.height : snapshot.hint.preferred.height;
    const auto* child = host()->children().at(snapshot.child).get();
    if (const auto* label = dynamic_cast<const geeyoou::Label*>(child);
        label && label->wordWrap()
            && (request.known_content_width
                || request.available_width.kind
                    == axisly::css::AvailableKind::Definite)) {
        const geeyoou::detail::DeathWatch watch(child);
        height = std::max(snapshot.hint.min.height,
            label->heightForWidth(std::max(1.0f, width)));
        if (!valid(revision, style) || !watch.alive()) throw Interrupted{};
    }
    height = request.known_content_height.value_or(height);
    return {{finite_nonnegative(width), finite_nonnegative(height)}, {}, {}};
}

geeyoou::SizeHint CssAxislyLayout::measure(const geeyoou::Widget& host_widget) const {
    try {
        gather(host_widget);
        const auto root = *nodes_[0];
        tree_->set_style(root, specs_[0].style);
        const auto& minimum = tree_->compute(root,
            {axisly::css::AvailableSpace::min_content(),
             axisly::css::AvailableSpace::min_content()});
        if (!minimum.succeeded()) throw Interrupted{};
        const auto minimum_size = minimum.size;
        const auto& preferred = tree_->compute(root,
            {axisly::css::AvailableSpace::max_content(),
             axisly::css::AvailableSpace::max_content()});
        if (!preferred.succeeded()) throw Interrupted{};
        const auto m = margins();
        geeyoou::SizeHint hint;
        hint.min = {minimum_size.width + m.left + m.right,
            minimum_size.height + m.top + m.bottom};
        hint.preferred = {std::max(hint.min.width,
                preferred.size.width + m.left + m.right),
            std::max(hint.min.height,
                preferred.size.height + m.top + m.bottom)};
        return hint;
    } catch (const Interrupted&) {
        geeyoou::detail::frameDegraded();
        return {};
    }
}

geeyoou::LayoutOverflow CssAxislyLayout::arrange(
    geeyoou::Widget& host_widget, const geeyoou::Rect& content) {
    try {
        gather(host_widget);
        const auto revision = revision_, style = geeyoou::styleGeneration();
        const auto root = *nodes_[0];
        auto root_style = specs_[0].style;
        root_style.box.size = {axisly::css::Length::length(
            finite_nonnegative(content.width())),
            axisly::css::Length::length(finite_nonnegative(content.height()))};
        tree_->set_style(root, std::move(root_style));
        const auto& result = tree_->compute(root,
            {axisly::css::AvailableSpace::definite(finite_nonnegative(content.width())),
             axisly::css::AvailableSpace::definite(finite_nonnegative(content.height()))});
        if (!result.succeeded()) {
            std::fprintf(stderr, "axisly CSS host arrange: %s\n",
                result.error.message.c_str());
            return {};
        }
        float scale = pixel_scale_;
        auto* window = host_widget.window();
        if (!valid(revision, style)) throw Interrupted{};
        if (window) scale = window->scaleFactor();
        const auto snap = [scale](float value) {
            return std::round(value * scale) / scale;
        };
        geeyoou::LayoutOverflow overflow;
        for (std::size_t index = 1; index < specs_.size(); ++index) {
            if (!snapshots_[index] || !nodes_[index]) continue;
            if (!valid(revision, style)) throw Interrupted{};
            const auto snapshot = *snapshots_[index];
            auto* child = host_widget.children().at(snapshot.child).get();
            const geeyoou::detail::DeathWatch watch(child);
            const auto* geometry = result.find(*nodes_[index]);
            if (!geometry) throw Interrupted{};
            const auto& box = geometry->border_box;
            const float x = snap(content.x() + box.x);
            const float y = snap(content.y() + box.y);
            const float right = snap(content.x() + box.x + box.width);
            const float bottom = snap(content.y() + box.y + box.height);
            const geeyoou::Rect rect{x, y, right - x, bottom - y};
            if (rect.x() < content.x() || rect.y() < content.y()
                || rect.right() > content.right() || rect.bottom() > content.bottom())
                ++overflow.clippedCount;
            child->setGeometry(rect);
            if (!valid(revision, style) || !watch.alive()) throw Interrupted{};
        }
        return overflow;
    } catch (const Interrupted&) {
        geeyoou::detail::frameDegraded();
        return {};
    }
}

} // namespace axisly_demo
