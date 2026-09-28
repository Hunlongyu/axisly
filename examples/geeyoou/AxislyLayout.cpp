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
axisly::Track intrinsicTrack(const axisly::Track& track) {
    if (const auto* weighted = std::get_if<axisly::WeightedTrack>(&track.definition()))
        return axisly::Track::range(axisly::Track::fixed(weighted->minimum),
            weighted->maximum ? axisly::Track::fixed(*weighted->maximum) : axisly::Track::content());
    // An unconstrained size hint has no percentage basis. Measure its content;
    // the real percentage is resolved later against a finite container width.
    if (track.kind() == axisly::TrackKind::Percentage) return axisly::Track::content();
    if (const auto* range = std::get_if<axisly::RangeTrack>(&track.definition())) {
        const auto bound = [](const axisly::TrackBound& value) {
            if (const auto* fixed = std::get_if<axisly::FixedTrack>(&value)) return axisly::Track::fixed(fixed->size);
            return axisly::Track::content();
        };
        return axisly::Track::range(bound(range->minimum), bound(range->maximum));
    }
    return track;
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

AxislyLayout::AxislyLayout(Kind kind) : kind_(kind) {
    arranged_.set_measure(callback, this);
    widthCache_.set_measure(callback, this);
}
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
void AxislyLayout::setColumns(std::vector<axisly::Track> tracks) {
    if (columns_ == tracks) return;
    columns_ = std::move(tracks); changed();
}
void AxislyLayout::setRows(std::vector<axisly::Track> tracks) {
    if (rows_ == tracks) return;
    rows_ = std::move(tracks); changed();
}
void AxislyLayout::setAutoFlow(axisly::AutoFlow flow) {
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
void AxislyLayout::addCell(geeyoou::Widget* child, axisly::GridItem item) {
    const auto childIndex = indexOf(child);
    if (std::any_of(entries_.begin(), entries_.end(), [childIndex](const Entry& e) { return e.child == childIndex; }))
        throw std::invalid_argument("Widget already belongs to the layout");
    item.id = nextId_++;
    entries_.push_back({childIndex, item}); changed();
}
void AxislyLayout::addBoxWidget(geeyoou::Widget* child, std::uint16_t weight) {
    const auto childIndex = indexOf(child);
    if (std::any_of(entries_.begin(), entries_.end(), [childIndex](const Entry& e) { return e.child == childIndex; }))
        throw std::invalid_argument("Widget already belongs to the layout");
    axisly::GridItem item; item.id = nextId_++;
    entries_.push_back({childIndex, item, weight}); changed();
}
void AxislyLayout::addSpace(float pixels, std::uint16_t weight) {
    axisly::GridItem item; item.id = nextId_++;
    entries_.push_back({noChild, item, weight, nonnegative(pixels)}); changed();
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
    axisly::GridItem labelItem; labelItem.id = nextId_++; labelItem.row = row;
    auto fieldItem = labelItem; fieldItem.id = nextId_++; fieldItem.column = 1;
    entries_.push_back({labelIndex, labelItem}); entries_.push_back({fieldIndex, fieldItem});
    changed();
}
std::size_t AxislyLayout::columnCount() const {
    auto count = std::max(columns_.size(), columnWeights_.size());
    for (const auto& e : entries_) if (e.item.column != axisly::auto_position)
        count = std::max(count, std::size_t(e.item.column) + e.item.column_span);
    return count;
}
std::size_t AxislyLayout::rowCount() const {
    auto count = std::max(rows_.size(), rowWeights_.size());
    for (const auto& e : entries_) if (e.item.row != axisly::auto_position)
        count = std::max(count, std::size_t(e.item.row) + e.item.row_span);
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
    lookup_.clear();
    for (std::size_t i = 0; i < snapshots_.size(); ++i) lookup_.emplace(snapshots_[i].entry.item.id, i);
    gatheredRevision_ = epoch; gatheredStyle_ = style;
    ++cacheVersion_;
}

AxislyLayout::Request AxislyLayout::request(Mode mode) const {
    Request r;
    const bool intrinsicMode = mode == Mode::Minimum || mode == Mode::Preferred;
    const auto track = [&](std::uint16_t weight) {
        return weight && !intrinsicMode ? axisly::Track::range(axisly::Track::content(), axisly::Track::weighted(float(weight)))
                                        : axisly::Track::content();
    };
    if (kind_ == Kind::Grid) {
        r.spec.columns = columns_;
        r.spec.rows = rows_;
        for (auto i = r.spec.columns.size(); i < columnCount(); ++i)
            r.spec.columns.push_back(track(i < columnWeights_.size() ? columnWeights_[i] : 0));
        for (auto i = r.spec.rows.size(); i < rowCount(); ++i)
            r.spec.rows.push_back(mode == Mode::Width ? axisly::Track::content() : track(i < rowWeights_.size() ? rowWeights_[i] : 0));
        if (intrinsicMode) {
            for (auto& t : r.spec.columns) t = intrinsicTrack(t);
            for (auto& t : r.spec.rows) t = intrinsicTrack(t);
        }
        if (mode == Mode::Width) for (auto& t : r.spec.rows) t = intrinsicTrack(t);
        r.spec.column_gap = r.spec.row_gap = std::max(0.0f, spacing());
        r.spec.auto_flow = flow_;
        r.spec.allow_implicit_tracks = flow_ != axisly::AutoFlow::Explicit;
        for (const auto& s : snapshots_) {
            auto item = s.entry.item;
            item.preferred_size = {s.hint.preferred.width, s.hint.preferred.height};
            r.items.push_back(item);
        }
    } else {
        const bool vertical = kind_ == Kind::Vertical;
        auto& main = vertical ? r.spec.rows : r.spec.columns;
        auto& cross = vertical ? r.spec.columns : r.spec.rows;
        cross.push_back(intrinsicMode || (mode == Mode::Width && !vertical) ? axisly::Track::content()
            : axisly::Track::range(axisly::Track::content(), axisly::Track::weighted(1)));
        bool previousWidget = false;
        for (const auto& s : snapshots_) {
            const bool isWidget = s.entry.child != noChild;
            if (previousWidget && isWidget) main.push_back(axisly::Track::fixed(std::max(0.0f, spacing())));
            auto item = s.entry.item;
            item.preferred_size = {s.hint.preferred.width, s.hint.preferred.height};
            if (vertical) item.row = static_cast<std::uint32_t>(main.size());
            else item.column = static_cast<std::uint32_t>(main.size());
            if (!isWidget) {
                main.push_back(s.entry.weight && !intrinsicMode && !(mode == Mode::Width && vertical)
                    ? axisly::Track::weighted(float(s.entry.weight), s.entry.space) : axisly::Track::fixed(s.entry.space));
            } else if (s.entry.weight && !intrinsicMode && !(mode == Mode::Width && vertical)) {
                // Exactly one widget owns this track: its max can legitimately
                // cap this one-dimensional track. Shared grid columns never do.
                const float mn = vertical ? s.hint.min.height : s.hint.min.width;
                const float mx = vertical ? s.hint.max.height : s.hint.max.width;
                main.push_back(axisly::Track::weighted(float(s.entry.weight), mn, mx));
            } else main.push_back(axisly::Track::content());
            r.items.push_back(item);
            previousWidget = isWidget;
        }
    }
    return r;
}
AxislyLayout::FlexRequest AxislyLayout::flexRequest() const {
    FlexRequest request;
    const bool vertical = kind_ == Kind::Vertical;
    request.spec.direction = vertical ? axisly::FlexDirection::Column : axisly::FlexDirection::Row;
    bool previousWidget = false;
    request.items.reserve(snapshots_.size());
    for (const auto& snapshot : snapshots_) {
        const bool widget = snapshot.entry.child != noChild;
        axisly::FlexItem item;
        item.id = snapshot.entry.item.id;
        item.grow = static_cast<float>(snapshot.entry.weight);
        item.shrink = widget ? 1.0f : 0.0f;
        if (widget) {
            item.min_content = {snapshot.hint.min.width, snapshot.hint.min.height};
            item.max_content = {snapshot.hint.preferred.width, snapshot.hint.preferred.height};
            item.preferred_size = item.max_content;
            item.minimum_width = axisly::FlexLength::fixed(snapshot.hint.min.width);
            item.minimum_height = axisly::FlexLength::fixed(snapshot.hint.min.height);
            item.maximum_width = axisly::FlexLength::fixed(snapshot.hint.max.width);
            item.maximum_height = axisly::FlexLength::fixed(snapshot.hint.max.height);
            if (previousWidget) {
                if (vertical) item.margin.top = axisly::FlexLength::fixed(std::max(0.0f, spacing()));
                else item.margin.left = axisly::FlexLength::fixed(std::max(0.0f, spacing()));
            }
        } else {
            item.basis = axisly::FlexLength::fixed(snapshot.entry.space);
            if (vertical) item.minimum_height = axisly::FlexLength::fixed(snapshot.entry.space);
            else item.minimum_width = axisly::FlexLength::fixed(snapshot.entry.space);
        }
        request.items.push_back(item);
        previousWidget = widget;
    }
    return request;
}
axisly::MeasureResult AxislyLayout::callback(axisly::ItemId id, const axisly::MeasureInput& in, void* ctx) {
    return static_cast<AxislyLayout*>(ctx)->measureItem(id, in);
}
axisly::FlexMeasureResult AxislyLayout::flexCallback(axisly::ItemId id,
        const axisly::FlexMeasureInput& input, void* ctx) {
    return static_cast<AxislyLayout*>(ctx)->measureFlexItem(id, input);
}
axisly::FlexMeasureResult AxislyLayout::measureFlexItem(axisly::ItemId id,
        const axisly::FlexMeasureInput& input) const {
    axisly::MeasureInput gridInput;
    gridInput.known_width = input.known_content_width;
    gridInput.known_height = input.known_content_height;
    gridInput.available = {input.available_content_width.value_or(0),
        input.available_content_height.value_or(0)};
    const auto measured = measureItem(id, gridInput);
    return {measured.minimum, measured.preferred, measured.preferred,
        measured.baseline, measured.baseline};
}
axisly::MeasureResult AxislyLayout::measureItem(axisly::ItemId id, const axisly::MeasureInput& input) const {
    if (!valid(passRevision_, passStyle_)) throw Interrupted{};
    const Snapshot snapshot = snapshots_.at(lookup_.at(id));
    if (snapshot.entry.child == noChild) return {};
    auto h = snapshot.hint;
    if ((mode_ == Mode::Width || mode_ == Mode::Arrange) && input.known_width) {
        const auto* child = host()->children().at(snapshot.entry.child).get();
        const geeyoou::detail::DeathWatch watch(child);
        const float width = std::clamp(*input.known_width, h.min.width, h.max.width);
        if (const auto* label = dynamic_cast<const geeyoou::Label*>(child); label && label->wordWrap()) {
            ++totals.wrappedMeasures;
            h.preferred.height = std::max(h.min.height, label->heightForWidth(width));
            h.min.height = h.preferred.height;
        } else if (dynamic_cast<const AxislyLayout*>(child->layout())) {
            const auto sized = measureAtWidth(*child, width);
            if (!valid(passRevision_, passStyle_) || !watch.alive()) throw Interrupted{};
            h.preferred.height = std::max(h.min.height, sized.preferred.height);
            h.min.height = std::max(h.min.height, sized.min.height);
        }
        if (!valid(passRevision_, passStyle_) || !watch.alive()) throw Interrupted{};
    }
    if (mode_ == Mode::Minimum) h.preferred = h.min;
    auto baseline = snapshot.entry.item.baseline;
    if (baseline && mode_ == Mode::Minimum) baseline = std::min(*baseline, h.preferred.height);
    return {{h.min.width, h.min.height}, {h.preferred.width, h.preferred.height}, baseline};
}
axisly::Size AxislyLayout::intrinsic(Mode mode, std::optional<float> width) const {
    mode_ = mode; passRevision_ = revision_; passStyle_ = geeyoou::styleGeneration();
    auto req = request(mode);
    axisly::Size available{width.value_or(geeyoou::kUnbounded), geeyoou::kUnbounded};
    if (mode == Mode::Width) {
        widthCache_.set_spec(req.spec); widthCache_.set_items(req.items);
        const auto r = widthCache_.compute(available, cacheVersion_);
        if (!r.result.succeeded()) { ++totals.errors; throw Interrupted{}; }
        if (r.cache_hit) ++totals.cacheHits; else ++totals.solves;
        return r.result.size;
    }
    ++totals.solves;
    const auto r = axisly::compute_grid(req.spec, req.items, available, callback, const_cast<AxislyLayout*>(this));
    if (!r.succeeded()) { ++totals.errors; throw Interrupted{}; }
    return r.size;
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
        mode_ = Mode::Arrange; passRevision_ = revision_; passStyle_ = geeyoou::styleGeneration();
        std::vector<axisly::ItemLayout> layouts;
        axisly::Size occupied{};
        const axisly::Size available{std::max(0.0f, content.width()), std::max(0.0f, content.height())};
        if (kind_ == Kind::Grid) {
            auto req = request(mode_);
            arranged_.set_spec(req.spec); arranged_.set_items(req.items);
            const auto computed = arranged_.compute(available, cacheVersion_);
            if (!computed.result.succeeded()) {
                ++totals.errors;
                std::fprintf(stderr, "axisly host arrange: %s\n", computed.result.error.message.c_str());
                return {};
            }
            if (computed.cache_hit) ++totals.cacheHits; else ++totals.solves;
            layouts = computed.result.items;
            occupied = computed.result.size;
        } else {
            auto req = flexRequest();
            const auto computed = axisly::compute_flex(req.spec, req.items, available, flexCallback, this);
            if (!computed.succeeded()) {
                ++totals.errors;
                std::fprintf(stderr, "axisly host flex arrange: %s\n", computed.error.message.c_str());
                return {};
            }
            ++totals.solves;
            layouts.reserve(computed.items.size());
            for (const auto& item : computed.items) {
                axisly::ItemLayout layout;
                layout.id = item.id;
                layout.rect = item.rect;
                layouts.push_back(layout);
            }
            occupied = {computed.occupied_bounds.x + computed.occupied_bounds.width,
                computed.occupied_bounds.y + computed.occupied_bounds.height};
        }
        float scale = scale_;
        // window() can be overridden. Guard it just like measurement.
        auto* window = hostWidget.window();
        if (!valid(passRevision_, passStyle_)) throw Interrupted{};
        if (window) scale = window->scaleFactor();
        const auto snap = [scale](float v) { return std::round(v * scale) / scale; };
        geeyoou::LayoutOverflow overflow;
        overflow.widthShort = std::max(0.0f, occupied.width - content.width());
        overflow.heightShort = std::max(0.0f, occupied.height - content.height());
        // No geometry is published until ALL measurements and solving succeed.
        for (const auto& item : layouts) {
            if (!valid(passRevision_, passStyle_)) throw Interrupted{};
            const auto entry = snapshots_.at(lookup_.at(item.id));
            if (entry.entry.child == noChild) continue;
            auto* child = hostWidget.children().at(entry.entry.child).get();
            const geeyoou::detail::DeathWatch watch(child);
            const auto& r = item.rect;
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
    axisly::GridItem item;
    item.row = index(row); item.column = index(column);
    if (rowSpan < 1 || columnSpan < 1 || rowSpan > 4097 - row || columnSpan > 4097 - column)
        throw std::out_of_range("Invalid Showcase span");
    item.row_span = static_cast<std::uint32_t>(rowSpan);
    item.column_span = static_cast<std::uint32_t>(columnSpan);
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
    std::vector<axisly::Track> columns, rows;
    for (std::size_t i = 1; i < xs.size(); ++i) columns.push_back(axisly::Track::weighted(xs[i] - xs[i-1], xs[i] - xs[i-1]));
    for (std::size_t i = 1; i < ys.size(); ++i) rows.push_back(axisly::Track::fixed(ys[i] - ys[i-1]));
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
