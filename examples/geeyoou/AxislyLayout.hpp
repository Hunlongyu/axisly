#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>
#include <axisly/axisly.hpp>
#include "geeyoou/widget/Widget.hpp"
#include "geeyoou/widget/ScrollArea.hpp"

namespace axisly_demo {

struct Counters {
    std::uint64_t solves{}, cacheHits{}, widgetMeasures{}, wrappedMeasures{}, aborted{}, errors{};
};
Counters counters();
void resetCounters();
// Uses Widget::sizeHint(), preserving GeeyoouUI's measurement/reentry gate.
geeyoou::SizeHint measureAtWidth(const geeyoou::Widget&, float width);

class AxislyLayout : public geeyoou::Layout {
public:
    enum class Kind { Grid, Horizontal, Vertical };
    explicit AxislyLayout(Kind kind = Kind::Grid);
    void setColumns(std::vector<axisly::Track>);
    void setRows(std::vector<axisly::Track>);
    void setAutoFlow(axisly::AutoFlow);
    void setPixelScale(float); // Offscreen acceptance; windows use their actual scale.
    void setColumnStretch(int, std::uint16_t);
    void setRowStretch(int, std::uint16_t);
    void addCell(geeyoou::Widget*, axisly::GridItem);
    std::size_t itemCount() const { return entries_.size(); }
    std::size_t columnCount() const;
    std::size_t rowCount() const;
    void setDesignMinimum(geeyoou::Size size);
    void setPreferredWidth(float width);
    std::uint64_t revision() const { return revision_; }

protected:
    geeyoou::SizeHint measure(const geeyoou::Widget&) const override;
    geeyoou::LayoutOverflow arrange(geeyoou::Widget&, const geeyoou::Rect&) override;
    void onInvalidated() override final;
    void onChildAppended() override final;
    void onChildRemoved(std::size_t) override final;
    void addBoxWidget(geeyoou::Widget*, std::uint16_t);
    void addSpace(float, std::uint16_t);
    void addFormRow(geeyoou::Widget*, geeyoou::Widget*);

private:
    static constexpr std::size_t noChild = ~std::size_t{};
    enum class Mode { Minimum, Preferred, Width, Arrange };
    struct Entry {
        std::size_t child{noChild};
        axisly::GridItem item;
        std::uint16_t weight{};
        float space{};
    };
    struct Snapshot { Entry entry; geeyoou::SizeHint hint; };
    struct Request { axisly::GridSpec spec; std::vector<axisly::GridItem> items; };
    std::size_t indexOf(const geeyoou::Widget*) const;
    bool valid(std::uint64_t epoch, std::uint64_t style) const;
    void gather(const geeyoou::Widget&) const;
    Request request(Mode) const;
    axisly::Size intrinsic(Mode, std::optional<float> width = {}) const;
    static axisly::MeasureResult callback(axisly::ItemId, const axisly::MeasureInput&, void*);
    axisly::MeasureResult measureItem(axisly::ItemId, const axisly::MeasureInput&) const;
    void changed();
    Kind kind_;
    std::vector<Entry> entries_;
    std::vector<axisly::Track> columns_, rows_;
    std::vector<std::uint16_t> columnWeights_, rowWeights_;
    axisly::AutoFlow flow_{axisly::AutoFlow::Explicit};
    axisly::ItemId nextId_{1};
    std::uint64_t revision_{1};
    float scale_{1};
    geeyoou::Size designMinimum_{};
    std::optional<float> preferredWidth_;
    mutable std::uint64_t gatheredRevision_{}, gatheredStyle_{};
    mutable std::uint64_t cacheVersion_{};
    mutable std::vector<Snapshot> snapshots_;
    mutable std::unordered_map<axisly::ItemId, std::size_t> lookup_;
    mutable Mode mode_{Mode::Preferred};
    mutable std::uint64_t passRevision_{}, passStyle_{};
    mutable axisly::GridCache arranged_, widthCache_;
    mutable geeyoou::SizeHint natural_{};
    mutable bool naturalValid_{};
};

class AxislyBoxLayout final : public AxislyLayout {
public:
    enum class Orientation { Horizontal, Vertical };
    explicit AxislyBoxLayout(Orientation orientation)
        : AxislyLayout(orientation == Orientation::Horizontal ? Kind::Horizontal : Kind::Vertical) {}
    void addWidget(geeyoou::Widget* child, std::uint16_t stretch = 0) { addBoxWidget(child, stretch); }
    void addSpacing(float pixels) { addSpace(pixels, 0); }
    void addStretch(std::uint16_t weight = 1) { addSpace(0, weight); }
};

class AxislyGridLayout final : public AxislyLayout {
public:
    void addWidget(geeyoou::Widget*, int row, int column, int rowSpan = 1, int columnSpan = 1);
    void addRow(geeyoou::Widget* label, geeyoou::Widget* field);
};

// Width-aware viewport for Showcase pages. Coalesces content/extent changes
// onto the host animation tick, including changes behind non-layout wrappers.
class AxislyScrollArea final : public geeyoou::ScrollArea {
protected:
    void onGeometryChanged() override;
    void onAnimationTick() override;
private:
    std::uint64_t revision_{}, style_{};
};

// The fixed-design pages retain their component-local drawings. Their direct
// page panels become grid cells, preserving the original design as minimum size.
geeyoou::Size adoptDesignedPage(geeyoou::Widget&, geeyoou::Size design);

} // namespace axisly_demo

namespace showcase {
using axisly_demo::AxislyBoxLayout;
using axisly_demo::AxislyGridLayout;
using axisly_demo::AxislyScrollArea;
}
