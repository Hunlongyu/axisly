#pragma once
#include "css_fixture.hpp"
#include <axisly/css_batch.hpp>

namespace axisly::bench::comparison {
class Engine {
public:
    virtual ~Engine() = default;
    virtual void calculate(Size) = 0;
    virtual void edit(std::size_t) = 0;
    virtual Rect rect(std::size_t) const = 0;
    virtual void check_order() const {}
    std::size_t measurements{};
    double consume(std::size_t count) const {
        return fingerprint(rect(0)) + fingerprint(rect(count / 2))
            + fingerprint(rect(count - 1));
    }
};
inline css::Length dimension(float v) {
    return v == -1 ? css::Length::automatic()
        : v == -2 ? css::Length::percent(1) : css::Length::length(v);
}
inline css::Style style(const Node& n) {
    css::Style s;
    s.display = n.display == 'G' ? css::Display::Grid
        : n.display == 'B' ? css::Display::Block : css::Display::Flex;
    s.box.size = {dimension(n.width), dimension(n.height)};
    s.box.min_size = {css::Length::length(0), css::Length::length(0)};
    s.flex_direction = n.direction == 'R' ? css::FlexDirection::Row : css::FlexDirection::Column;
    s.flex_wrap = n.wrap ? css::FlexWrap::Wrap : css::FlexWrap::NoWrap;
    s.flex_basis = dimension(n.basis);
    s.flex_grow = n.grow;
    s.flex_shrink = n.shrink;
    const auto padding = css::Length::length(n.padding), margin = css::Length::length(n.margin);
    s.box.padding = {padding, padding, padding, padding};
    s.box.margin = {margin, margin, margin, margin};
    s.gap = {css::Length::length(n.gap), css::Length::length(n.gap)};
    s.align_items = {css::ItemPosition::Stretch};
    s.justify_items = {css::ItemPosition::Stretch};
    s.align_content = {css::ContentPosition::Start};
    s.justify_content = {css::ContentPosition::Start};
    auto tracks = [](const auto& source, auto& target) {
        for (const auto t : source) {
            if (t.kind == 'F') target.emplace_back(css::TrackSizing::fixed(t.value));
            else if (t.kind == 'A') target.emplace_back(css::TrackSizing::automatic());
            else target.emplace_back(css::TrackSizing::minmax(css::TrackBreadth::length(0),
                css::TrackBreadth::fraction(t.value)));
        }
    };
    tracks(n.columns, s.grid.template_columns);
    tracks(n.rows, s.grid.template_rows);
    if (n.column) s.grid.column.start = css::GridLine::index(static_cast<std::int16_t>(n.column));
    if (n.row) s.grid.row.start = css::GridLine::index(static_cast<std::int16_t>(n.row));
    s.grid.column.end = css::GridLine::span(static_cast<std::uint16_t>(n.column_span));
    s.grid.row.end = css::GridLine::span(static_cast<std::uint16_t>(n.row_span));
    return s;
}
class AxislyCss final : public Engine {
    const Fixture& fixture_;
    css::LayoutTree tree_;
    std::vector<css::NodeId> nodes_;
    std::vector<std::size_t> revisions_;
    const css::LayoutResult* output_{};
    css::LayoutOptions options_{};
public:
    AxislyCss(const Fixture& f, const std::vector<css::Style>& styles,
              std::shared_ptr<css::LayoutMemory> memory = {}, bool retained = false)
        : fixture_(f), tree_({}, std::move(memory)), revisions_(f.nodes.size()) {
        options_.publish_nodes = !retained;
        options_.collect_grid_tracks = !retained;
        nodes_.reserve(f.nodes.size());
        for (std::size_t i = 0; i < f.nodes.size(); ++i) {
            const auto& n = f.nodes[i];
            css::MeasureFunction callback;
            if (n.measure) callback = [this, i](const css::MeasureRequest& r) {
                ++measurements;
                const auto size = measure(fixture_.nodes[i], revisions_[i],
                    r.known_content_width, r.known_content_height,
                    r.available_width.definite_value(),
                    r.available_width.kind == css::AvailableKind::MinContent);
                return css::Measurement{.content_size = size};
            };
            nodes_.push_back(tree_.create_node(styles[i], std::move(callback)));
            if (n.parent >= 0) tree_.append_child(nodes_[static_cast<std::size_t>(n.parent)], nodes_.back());
        }
    }
    void calculate(Size size) override {
        measurements = 0;
        const auto request = layout_request(size);
        accept(tree_.compute(request.root, request.available, options_));
    }
    css::LayoutRequest layout_request(Size size) {
        measurements = 0;
        return {&tree_, nodes_[0], {css::AvailableSpace::definite(size.width),
            size.height < 0 ? css::AvailableSpace::max_content()
                : css::AvailableSpace::definite(size.height)}};
    }
    void accept(const css::LayoutResult& result) {
        output_ = &result;
        if (!output_->succeeded()) throw std::runtime_error(
            (output_->error.code == css::LayoutErrorCode::LimitExceeded ? "LAYOUT_LIMIT: " : "")
            + output_->error.message + "; measurements=" + std::to_string(output_->measurement_count)
            + "; layouts=" + std::to_string(output_->layout_count));
        require(!options_.publish_nodes || output_->nodes.size() == nodes_.size(), "Incomplete CSS output");
        require(output_->measurement_count == measurements, "CSS measurement count mismatch");
    }
    void edit(std::size_t index) override {
        ++revisions_[index];
        tree_.mark_dirty(nodes_[index]);
    }
    Rect rect(std::size_t i) const override {
        if (options_.publish_nodes) return output_->nodes[i].border_box;
        const auto result = tree_.layout(nodes_[i]);
        require(result.has_value(), "Missing retained CSS layout");
        return result->border_box;
    }
    void check_order() const override {
        if (!options_.publish_nodes) return;
        for (std::size_t i = 0; i < nodes_.size(); ++i)
            require(output_->nodes[i].node == nodes_[i], "CSS output order differs from fixture");
    }
};

} // namespace axisly::bench::comparison
