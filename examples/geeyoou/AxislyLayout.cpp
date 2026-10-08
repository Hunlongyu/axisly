#include "AxislyLayout.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <utility>
#include "geeyoou/render/StyleSheet.hpp"
#include "geeyoou/widget/GroupBox.hpp"
#include "geeyoou/widget/Label.hpp"
#include "geeyoou/widget/Window.hpp"

namespace axisly_demo {
namespace {
Counters totals;
struct Interrupted {};
struct WidthRequest { const geeyoou::Widget* widget{}; float width{}; };
thread_local WidthRequest widthRequest;
float nonnegative(float v) {
    if (!std::isfinite(v)) throw Interrupted{};
    return std::max(0.0f, v);
}
geeyoou::SizeHint normalize(geeyoou::SizeHint h) {
    auto axis = [](float& mn, float& pf, float& mx) {
        mn = nonnegative(mn);
        mx = std::max(mn, nonnegative(mx));
        pf = std::clamp(nonnegative(pf), mn, mx);
    };
    axis(h.min.width, h.preferred.width, h.max.width);
    axis(h.min.height, h.preferred.height, h.max.height);
    return h;
}
std::uint32_t index(int v) {
    if (v < 0 || v > 4096) throw std::out_of_range("Showcase grid index must be in [0,4096]");
    return static_cast<std::uint32_t>(v);
}
axisly::css::GridPlacement placement(std::uint32_t start, std::uint16_t span = 1) {
    return {axisly::css::GridLine::index(static_cast<std::int16_t>(start + 1)), axisly::css::GridLine::span(span)};
}
std::size_t extent(const axisly::css::GridPlacement& p) {
    if (p.start.kind() != axisly::css::GridLineKind::Index || p.start.index_value() <= 0) return 0;
    return static_cast<std::size_t>(p.start.index_value() - 1)
        + (p.end.kind() == axisly::css::GridLineKind::Span ? p.end.span_count() : 1);
}

}

Counters counters() { return totals; }
void resetCounters() { totals = {}; }

geeyoou::SizeHint measureAtWidth(const geeyoou::Widget& widget, float width) {
    struct Restore {
        WidthRequest old{widthRequest};
        ~Restore() { widthRequest = old; }
    } restore;
    widthRequest = {&widget, nonnegative(width)};
    // Only the scoped request lives outside the widget. No member is accessed
    // after the virtual call, which is allowed to destroy the entire host.
    return widget.sizeHint();
}

AxislyLayout::AxislyLayout(Kind kind) : kind_(kind) {}
void AxislyLayout::onInvalidated() { ++revision_; naturalValid_ = false; }
void AxislyLayout::onChildAppended() { onInvalidated(); }
void AxislyLayout::onChildRemoved(std::size_t child) {
    std::erase_if(entries_, [child](const Entry& e) { return e.child == child; });
    for (auto& e : entries_) if (e.child != noChild && e.child > child) --e.child;
    onInvalidated();
}
void AxislyLayout::changed() {
    // invalidate() can synchronously arrange and destroy us. It must be last.
    invalidate();
}
void AxislyLayout::setColumns(std::vector<axisly::css::TrackSizing> tracks) {
    if (columns_ == tracks) return;
    columns_ = std::move(tracks); changed();
}
void AxislyLayout::setRows(std::vector<axisly::css::TrackSizing> tracks) {
    if (rows_ == tracks) return;
    rows_ = std::move(tracks); changed();
}
void AxislyLayout::setAutoFlow(axisly::css::GridAutoFlow flow) {
    if (flow_ == flow) return;
    flow_ = flow; changed();
}
void AxislyLayout::setDesignMinimum(geeyoou::Size size) { designMinimum_ = size; changed(); }
void AxislyLayout::setPreferredWidth(float width) { preferredWidth_ = nonnegative(width); changed(); }
void AxislyLayout::setPixelScale(float scale) {
    if (!std::isfinite(scale) || scale <= 0) throw std::invalid_argument("Invalid pixel scale");
    if (scale_ == scale) return;
    scale_ = scale; changed();
}
void AxislyLayout::setColumnStretch(int column, std::uint16_t weight) {
    const auto i = index(column);
    if (columnWeights_.size() <= i) columnWeights_.resize(i + 1);
    if (columnWeights_[i] == weight) return;
    columnWeights_[i] = weight; changed();
}
void AxislyLayout::setRowStretch(int row, std::uint16_t weight) {
    const auto i = index(row);
    if (rowWeights_.size() <= i) rowWeights_.resize(i + 1);
    if (rowWeights_[i] == weight) return;
    rowWeights_[i] = weight; changed();
}
std::size_t AxislyLayout::indexOf(const geeyoou::Widget* child) const {
    if (!host()) throw std::logic_error("Adopt layout before adding children");
    const auto& children = host()->children();
    for (std::size_t i = 0; i < children.size(); ++i) if (children[i].get() == child) return i;
    throw std::invalid_argument("AxislyLayout requires a direct child of its host");
}
void AxislyLayout::addCell(geeyoou::Widget* child, axisly::css::Style style, std::optional<float> baseline) {
    const auto childIndex = indexOf(child);
    if (std::any_of(entries_.begin(), entries_.end(), [childIndex](const Entry& e) { return e.child == childIndex; }))
        throw std::invalid_argument("Widget already belongs to the layout");
    axisly::css::validate_style(style);
    entries_.push_back({childIndex, std::move(style), baseline}); changed();
}
void AxislyLayout::addBoxWidget(geeyoou::Widget* child, std::uint16_t weight) {
    const auto childIndex = indexOf(child);
    if (std::any_of(entries_.begin(), entries_.end(), [childIndex](const Entry& e) { return e.child == childIndex; }))
        throw std::invalid_argument("Widget already belongs to the layout");
    entries_.push_back({childIndex, {}, {}, weight}); changed();
}
void AxislyLayout::addSpace(float pixels, std::uint16_t weight) {
    entries_.push_back({noChild, {}, {}, weight, nonnegative(pixels)}); changed();
}
void AxislyLayout::addFormRow(geeyoou::Widget* label, geeyoou::Widget* field) {
    const auto labelIndex = indexOf(label), fieldIndex = indexOf(field);
    if (labelIndex == fieldIndex || std::any_of(entries_.begin(), entries_.end(), [&](const Entry& e) {
            return e.child == labelIndex || e.child == fieldIndex;
        })) throw std::invalid_argument("Duplicate form widgets");
    const auto row = static_cast<std::uint32_t>(rowCount());
    if (row > 4096) throw std::out_of_range("Too many form rows");
    entries_.reserve(entries_.size() + 2);
    if (columnWeights_.size() < 2) columnWeights_.resize(2);
    if (!columnWeights_[1]) columnWeights_[1] = 1;
    axisly::css::Style labelItem, fieldItem;
    labelItem.grid.row = fieldItem.grid.row = placement(row);
    labelItem.grid.column = placement(0); fieldItem.grid.column = placement(1);
    entries_.push_back({labelIndex, labelItem, {}}); entries_.push_back({fieldIndex, fieldItem, {}});
    changed();
}
std::size_t AxislyLayout::columnCount() const {
    auto count = std::max(columns_.size(), columnWeights_.size());
    for (const auto& e : entries_) count = std::max(count, extent(e.style.grid.column));
    return count;
}
std::size_t AxislyLayout::rowCount() const {
    auto count = std::max(rows_.size(), rowWeights_.size());
    for (const auto& e : entries_) count = std::max(count, extent(e.style.grid.row));
    return count;
}
bool AxislyLayout::valid(std::uint64_t epoch, std::uint64_t style) const {
    return hostAlive() && revision_ == epoch && geeyoou::styleGeneration() == style;
}
void AxislyLayout::gather(const geeyoou::Widget& hostWidget) const {
    const auto epoch = revision_, style = geeyoou::styleGeneration();
    if (gatheredRevision_ == epoch && gatheredStyle_ == style) return;
    naturalValid_ = false;
    std::vector<Snapshot> fresh;
    fresh.reserve(entries_.size());
    // Copy each entry before crossing into arbitrary user code.
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const Entry e = entries_[i];
        if (e.child == noChild) { fresh.push_back({e, {}}); continue; }
        if (e.child >= hostWidget.children().size()) throw Interrupted{};
        const auto* child = hostWidget.children()[e.child].get();
        if (!child->isVisible()) continue;
        const geeyoou::detail::DeathWatch watch(child);
        ++totals.widgetMeasures;
        const auto hint = child->sizeHint();
        if (!valid(epoch, style) || !watch.alive()) throw Interrupted{};
        fresh.push_back({e, normalize(hint)});
    }
    snapshots_ = std::move(fresh);
    for (auto& state : states_) state = {};
    gatheredRevision_ = epoch; gatheredStyle_ = style;
}

AxislyLayout::Request AxislyLayout::request(Mode mode) const {
    namespace css = axisly::css;
    Request r;
    const auto flexTrack = [](std::uint16_t weight) {
        return weight ? css::TrackSizing::minmax(css::TrackBreadth::min_content(), css::TrackBreadth::fraction(float(weight)))
                      : css::TrackSizing::automatic();
    };
    r.root.display = kind_ == Kind::Grid ? css::Display::Grid : css::Display::Flex;
    r.root.align_content = css::ContentAlignment{css::ContentPosition::Start};
    r.root.justify_content = css::ContentAlignment{css::ContentPosition::Start};
    if (kind_ == Kind::Grid) {
        auto& grid = r.root.grid;
        for (const auto& track : columns_) grid.template_columns.push_back(track);
        for (const auto& track : rows_) grid.template_rows.push_back(track);
        for (auto i = columns_.size(); i < columnCount(); ++i)
            grid.template_columns.push_back(flexTrack(i < columnWeights_.size() ? columnWeights_[i] : 0));
        for (auto i = rows_.size(); i < rowCount(); ++i)
            grid.template_rows.push_back(flexTrack(i < rowWeights_.size() ? rowWeights_[i] : 0));
        grid.auto_flow = flow_;
        r.root.gap = {css::Length::length(std::max(0.0f, spacing())), css::Length::length(std::max(0.0f, spacing()))};
    } else r.root.flex_direction = kind_ == Kind::Vertical ? css::FlexDirection::Column : css::FlexDirection::Row;
    bool previousWidget = false;
    for (const auto& snapshot : snapshots_) {
        auto style = snapshot.entry.style;
        const auto& hint = snapshot.hint;
        const bool widget = snapshot.entry.child != noChild;
        if (widget) {
            style.box.min_size = {css::Length::length(hint.min.width), css::Length::length(hint.min.height)};
            style.box.max_size = {css::Length::length(hint.max.width), css::Length::length(hint.max.height)};
        }
        if (kind_ != Kind::Grid) {
            const bool vertical = kind_ == Kind::Vertical;
            style.flex_shrink = 0;
            style.flex_grow = float(snapshot.entry.weight);
            if (style.flex_grow > 0 && mode == Mode::Arrange) style.flex_basis = css::Length::length(0);
            if (!widget) {
                (vertical ? style.box.size.height : style.box.size.width) = css::Length::length(snapshot.entry.space);
            }
            if (previousWidget && widget) {
                (vertical ? style.box.margin.top : style.box.margin.left) = css::Length::length(std::max(0.0f, spacing()));
            }
        }
        r.items.push_back(std::move(style));
        previousWidget = widget;
    }
    return r;
}
axisly::css::Measurement AxislyLayout::measureItem(std::size_t index, const axisly::css::MeasureRequest& input) const {
    namespace css = axisly::css;
    if (!valid(passRevision_, passStyle_)) throw Interrupted{};
    const Snapshot snapshot = snapshots_.at(index);
    if (snapshot.entry.child == noChild) return {};
    auto h = snapshot.hint;
    const auto knownWidth = input.known_content_width ? input.known_content_width : input.available_width.definite_value();
    if ((mode_ == Mode::Width || mode_ == Mode::Arrange) && knownWidth) {
        const auto* child = host()->children().at(snapshot.entry.child).get();
        const geeyoou::detail::DeathWatch watch(child);
        const float width = std::clamp(*knownWidth, h.min.width, h.max.width);
        if (const auto* label = dynamic_cast<const geeyoou::Label*>(child); label && label->wordWrap()) {
            ++totals.wrappedMeasures;
            h.preferred.height = std::max(h.min.height, label->heightForWidth(width));
        } else if (dynamic_cast<const AxislyLayout*>(child->layout())) {
            const auto sized = measureAtWidth(*child, width);
            if (!valid(passRevision_, passStyle_) || !watch.alive()) throw Interrupted{};
            h.preferred.height = std::max(h.min.height, sized.preferred.height);
        }
        if (!valid(passRevision_, passStyle_) || !watch.alive()) throw Interrupted{};
    }
    const float width = input.known_content_width.value_or(
        input.available_width.kind == css::AvailableKind::MinContent ? h.min.width : h.preferred.width);
    const float height = input.known_content_height.value_or(
        input.available_height.kind == css::AvailableKind::MinContent ? h.min.height : h.preferred.height);
    auto baseline = snapshot.entry.baseline;
    if (baseline) baseline = std::min(*baseline, height);
    return {{width, height}, baseline, baseline};
}
const axisly::css::LayoutResult& AxislyLayout::compute(Mode mode, std::optional<float> width, std::optional<float> height) const {
    namespace css = axisly::css;
    mode_ = mode; passRevision_ = revision_; passStyle_ = geeyoou::styleGeneration();
    auto& state = states_[static_cast<std::size_t>(mode)];
    if (!state.tree) {
        auto req = request(mode);
        auto tree = std::make_unique<css::LayoutTree>();
        state.root = tree->create_node(req.root);
        state.nodes.clear();
        for (std::size_t i = 0; i < req.items.size(); ++i) {
            const auto node = tree->create_node(req.items[i], [this, i](const css::MeasureRequest& input) { return measureItem(i, input); });
            tree->append_child(state.root, node);
            state.nodes.push_back(node);
        }
        state.tree = std::move(tree);
    }
    auto style = state.tree->style(state.root);
    style.box.size = {width ? css::Length::length(*width) : css::Length::automatic(),
                      height ? css::Length::length(*height) : css::Length::automatic()};
    state.tree->set_style(state.root, std::move(style));
    const auto query = mode == Mode::Minimum ? css::AvailableSpace::min_content() : css::AvailableSpace::max_content();
    const auto& result = state.tree->compute(state.root,
        {width ? css::AvailableSpace::definite(*width) : query, height ? css::AvailableSpace::definite(*height) : query});
    if (!result.succeeded()) { ++totals.errors; throw Interrupted{}; }
    if (result.cache_hit) ++totals.cacheHits; else ++totals.solves;
    return result;
}
axisly::Size AxislyLayout::intrinsic(Mode mode, std::optional<float> width) const {
    return compute(mode, width).size;
}
geeyoou::SizeHint AxislyLayout::measure(const geeyoou::Widget& hostWidget) const {
    try {
        gather(hostWidget);
        const auto m = margins();
        if (!naturalValid_) {
            const auto mn = intrinsic(Mode::Minimum), pf = intrinsic(Mode::Preferred);
            natural_.min = {std::max(designMinimum_.width, mn.width + m.left + m.right),
                            std::max(designMinimum_.height, mn.height + m.top + m.bottom)};
            natural_.preferred = {std::max(natural_.min.width, pf.width + m.left + m.right),
                                  std::max(natural_.min.height, pf.height + m.top + m.bottom)};
            if (preferredWidth_) natural_.preferred.width = std::max(natural_.min.width, *preferredWidth_);
            naturalValid_ = true;
            if (preferredWidth_) {
                const auto sized = intrinsic(Mode::Width, std::max(0.0f, *preferredWidth_ - m.left - m.right));
                natural_.preferred.height = std::max(natural_.min.height, sized.height + m.top + m.bottom);
            }
        }
        auto hint = natural_;
        if (widthRequest.widget == &hostWidget) {
            // The pinned GroupBox adds exactly 12px on either horizontal edge
            // outside Layout::measure, and subtracts these before arrange.
            const float frame = dynamic_cast<const geeyoou::GroupBox*>(&hostWidget) ? 24.0f : 0.0f;
            const auto sized = intrinsic(Mode::Width, std::max(0.0f, widthRequest.width - frame - m.left - m.right));
            hint.preferred.height = std::max(hint.min.height, sized.height + m.top + m.bottom);
            hint.preferred.width = std::max(hint.min.width, std::min(hint.preferred.width, widthRequest.width - frame));
        }
        return hint;
    } catch (const Interrupted&) {
        ++totals.aborted; geeyoou::detail::frameDegraded(); return {};
    }
}
geeyoou::LayoutOverflow AxislyLayout::arrange(geeyoou::Widget& hostWidget, const geeyoou::Rect& content) {
    try {
        gather(hostWidget);
        const auto& result = compute(Mode::Arrange, std::max(0.0f, content.width()), std::max(0.0f, content.height()));
        float scale = scale_;
        // window() can be overridden. Guard it just like measurement.
        auto* window = hostWidget.window();
        if (!valid(passRevision_, passStyle_)) throw Interrupted{};
        if (window) scale = window->scaleFactor();
        const auto snap = [scale](float v) { return std::round(v * scale) / scale; };
        geeyoou::LayoutOverflow overflow;
        overflow.widthShort = std::max(0.0f, result.nodes.front().scrollable_overflow_rect.width - content.width());
        overflow.heightShort = std::max(0.0f, result.nodes.front().scrollable_overflow_rect.height - content.height());
        // No geometry is published until ALL measurements and solving succeed.
        for (std::size_t i = 0; i < snapshots_.size(); ++i) {
            if (!valid(passRevision_, passStyle_)) throw Interrupted{};
            const auto entry = snapshots_[i];
            if (entry.entry.child == noChild) continue;
            auto* child = hostWidget.children().at(entry.entry.child).get();
            const geeyoou::detail::DeathWatch watch(child);
            const auto* item = result.find(states_[static_cast<std::size_t>(Mode::Arrange)].nodes[i]);
            if (!item) throw Interrupted{};
            const auto& r = item->border_box;
            // Max constrains this widget, never another item sharing its column.
            // Min wins over an undersized cell; overflow is explicitly reported.
            const float w = std::clamp(r.width, entry.hint.min.width, entry.hint.max.width);
            const float h = std::clamp(r.height, entry.hint.min.height, entry.hint.max.height);
            const float x = snap(content.x() + r.x), y = snap(content.y() + r.y);
            const geeyoou::Rect rect{x, y, snap(content.x() + r.x + w) - x, snap(content.y() + r.y + h) - y};
            if (rect.right() > content.right() + 0.5f / scale || rect.bottom() > content.bottom() + 0.5f / scale) ++overflow.clippedCount;
            child->setGeometry(rect);
            if (!valid(passRevision_, passStyle_) || !watch.alive()) throw Interrupted{};
        }
        return overflow;
    } catch (const Interrupted&) {
        ++totals.aborted; geeyoou::detail::frameDegraded(); return {};
    }
}

void AxislyGridLayout::addWidget(geeyoou::Widget* w, int row, int column, int rowSpan, int columnSpan) {
    axisly::css::Style item;
    index(row); index(column);
    if (rowSpan < 1 || columnSpan < 1 || rowSpan > 4097 - row || columnSpan > 4097 - column)
        throw std::out_of_range("Invalid Showcase span");
    item.grid.row = placement(index(row), static_cast<std::uint16_t>(rowSpan));
    item.grid.column = placement(index(column), static_cast<std::uint16_t>(columnSpan));
    addCell(w, item);
}
void AxislyGridLayout::addRow(geeyoou::Widget* label, geeyoou::Widget* field) {
    addFormRow(label, field);
}

void AxislyScrollArea::onGeometryChanged() {
    auto* page = content();
    auto* layout = page ? dynamic_cast<AxislyLayout*>(page->layout()) : nullptr;
    revision_ = layout ? layout->revision() : 0;
    style_ = geeyoou::styleGeneration();
    struct Restore {
        WidthRequest old{widthRequest};
        ~Restore() { widthRequest = old; }
    } restore;
    // Reserve the pinned host's 12px scroll strip before measuring wrapped
    // text. This avoids alternating between two widths as the bar appears.
    widthRequest = {page, std::max(0.0f, geometry().width() - 12)};
    geeyoou::ScrollArea::onGeometryChanged();
    // The base call can delete us or the content. Touch no members afterward.
}
void AxislyScrollArea::onAnimationTick() {
    auto* page = content();
    auto* layout = page ? dynamic_cast<AxislyLayout*>(page->layout()) : nullptr;
    if (layout && (revision_ != layout->revision() || style_ != geeyoou::styleGeneration()))
        onGeometryChanged();
}

geeyoou::Size adoptDesignedPage(geeyoou::Widget& page, geeyoou::Size design) {
    if (page.layout()) return page.sizeHint().preferred;
    // Extract explicit design boundaries once, before a layout owns geometry.
    // A column grows proportionally, while rows keep their authored height.
    // This preserves designed sub-scenes and still uses axisly for every panel.
    std::vector<float> xs{0, design.width}, ys{0, design.height};
    std::vector<std::pair<std::size_t, geeyoou::Rect>> panels;
    for (std::size_t i = 0; i < page.children().size(); ++i) {
        const auto r = page.children()[i]->geometry();
        if (r.isEmpty()) continue;
        panels.emplace_back(i, r);
        xs.push_back(r.x()); xs.push_back(r.right());
        ys.push_back(r.y()); ys.push_back(r.bottom());
    }
    auto unique = [](std::vector<float>& values) {
        std::sort(values.begin(), values.end());
        values.erase(std::unique(values.begin(), values.end()), values.end());
    };
    unique(xs); unique(ys);
    auto* layout = page.setLayout<AxislyGridLayout>();
    layout->setSpacing(0);
    std::vector<axisly::css::TrackSizing> columns, rows;
    for (std::size_t i = 1; i < xs.size(); ++i) columns.push_back(axisly::css::TrackSizing::minmax(axisly::css::TrackBreadth::length(xs[i] - xs[i-1]), axisly::css::TrackBreadth::fraction(xs[i] - xs[i-1])));
    for (std::size_t i = 1; i < ys.size(); ++i) rows.push_back(axisly::css::TrackSizing::fixed(ys[i] - ys[i-1]));
    layout->setColumns(std::move(columns)); layout->setRows(std::move(rows));
    layout->setDesignMinimum(design);
    for (const auto& [child, r] : panels) {
        const int col = int(std::lower_bound(xs.begin(), xs.end(), r.x()) - xs.begin());
        const int row = int(std::lower_bound(ys.begin(), ys.end(), r.y()) - ys.begin());
        const int cols = int(std::lower_bound(xs.begin(), xs.end(), r.right()) - xs.begin()) - col;
        const int rowsCount = int(std::lower_bound(ys.begin(), ys.end(), r.bottom()) - ys.begin()) - row;
        layout->addWidget(page.children()[child].get(), row, col, rowsCount, cols);
    }
    return page.sizeHint().preferred;
}
} // namespace axisly_demo
