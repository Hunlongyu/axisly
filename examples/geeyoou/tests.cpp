#include "AxislyLayout.hpp"
#include "CssAxislyLayout.hpp"
#include "CssHostPage.hpp"
#include "CssHostAcceptance.hpp"
#include "geeyoou/widget/Label.hpp"
#include "geeyoou/widget/GroupBox.hpp"
#include "geeyoou/render/Skin.hpp"
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <stdexcept>
#include <vector>
namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void near(float a, float b, const char* message) {
    if (std::abs(a-b) >= .02f) { std::fprintf(stderr, "%s: actual %.3f expected %.3f\n", message, a, b); require(false, message); }
}
using geeyoou::Widget;
using axisly_demo::AxislyGridLayout;
using axisly_demo::AxislyBoxLayout;
class Hint : public Widget {
public:
    mutable int calls{};
    geeyoou::SizeHint hint{{20, 20}, {80, 30}, {geeyoou::kUnbounded, 40}};
    geeyoou::SizeHint sizeHint() const override { ++calls; return hint; }
};
class Action final : public Hint {
public:
    mutable std::function<void()> measured;
    std::function<void()> arranged;
    geeyoou::SizeHint sizeHint() const override {
        const auto result = hint;
        auto callback = std::exchange(measured, {});
        if (callback) callback();
        return result;
    }
protected:
    void onGeometryChanged() override {
        auto callback = std::exchange(arranged, {});
        if (callback) callback();
    }
};
void cacheAndRemoval() {
    Widget host;
    auto* grid = host.setLayout<AxislyGridLayout>();
    auto* a = host.add<Hint>(); auto* b = host.add<Hint>();
    grid->setSpacing(10); grid->setColumnStretch(0, 1); grid->setColumnStretch(1, 1);
    grid->addWidget(a, 0, 0); grid->addWidget(b, 0, 1);
    host.setGeometry({0, 0, 410, 100});
    near(a->geometry().width(), 200, "weighted width");
    near(b->geometry().x(), 210, "weighted offset");
    const int calls = a->calls + b->calls;
    host.performLayout();
    require(a->calls + b->calls == calls, "unchanged cache");
    a->hint.preferred.width = 100; a->invalidateSizeHint();
    require(a->calls + b->calls > calls, "content invalidation");
    host.removeChild(a); host.performLayout();
    require(b->geometry().width() > 0, "child removal");
}
void maxAndOverflow() {
    Widget host;
    auto* grid = host.setLayout<AxislyGridLayout>();
    auto* a = host.add<Hint>(); auto* b = host.add<Hint>();
    a->hint.max.width = 60;
    grid->setColumnStretch(0, 1);
    grid->addWidget(a, 0, 0); grid->addWidget(b, 1, 0);
    host.setGeometry({0, 0, 300, 100});
    near(a->geometry().width(), 60, "per-widget max");
    near(b->geometry().width(), 300, "widget max must not cap shared column");
    host.setGeometry({0, 0, 10, 10});
    require(host.lastLayoutOverflow().any(), "minimum overflow is observable");
    require(a->geometry().width() >= 20, "minimum retained");
}
void boxSpacingAndVisibility() {
    Widget host;
    auto* box = host.setLayout<AxislyBoxLayout>(AxislyBoxLayout::Orientation::Horizontal);
    box->setSpacing(9);
    auto* a = host.add<Hint>(); auto* b = host.add<Hint>(); auto* c = host.add<Hint>();
    box->addWidget(a); box->addSpacing(20); box->addWidget(b); box->addWidget(c);
    host.setGeometry({0, 0, 500, 50});
    near(b->geometry().x(), 100, "explicit spacer has no automatic spacing");
    near(c->geometry().x(), 189, "adjacent widgets have spacing");
    b->setVisible(false);
    near(c->geometry().x(), 100, "hidden widget removes its gap");
    b->setVisible(true);
    near(c->geometry().x(), 189, "show restores gap");
}
void nestedInvalidation() {
    Widget host;
    auto* root = host.setLayout<AxislyBoxLayout>(AxislyBoxLayout::Orientation::Horizontal);
    auto* left = host.add<Widget>(); auto* right = host.add<Widget>();
    auto* ll = left->setLayout<AxislyGridLayout>(); auto* rl = right->setLayout<AxislyGridLayout>();
    auto* a = left->add<Hint>(); auto* b = right->add<Hint>();
    ll->addWidget(a, 0, 0); rl->addWidget(b, 0, 0);
    root->addWidget(left, 1); root->addWidget(right, 1);
    host.setGeometry({0, 0, 600, 100});
    const auto calls = b->calls;
    a->hint.preferred.width = 140; a->invalidateSizeHint();
    require(b->calls == calls, "unrelated nested branch remeasured");
    const auto before = host.sizeHint().preferred.width;
    ll->setMargins({10, 0, 20, 0});
    near(host.sizeHint().preferred.width, before+30, "layout setter must invalidate ancestor hint");
}
void textAndStyle() {
    Widget host;
    auto* root = host.setLayout<AxislyBoxLayout>(AxislyBoxLayout::Orientation::Vertical);
    auto* group = host.add<geeyoou::GroupBox>(); group->setTitle("Text");
    auto* inner = group->setLayout<AxislyBoxLayout>(AxislyBoxLayout::Orientation::Vertical);
    auto* label = group->add<geeyoou::Label>(); label->setText("Short"); label->setWordWrap(true);
    inner->addWidget(label); root->addWidget(group);
    auto* tail = host.add<Hint>(); root->addWidget(tail);
    host.setGeometry({0, 0, 240, 1000});
    const float before = tail->geometry().y();
    label->setText("Long text 中文换行 testing repeated words and constrained widths in a nested real host panel. Long text wraps to more than one line.");
    require(tail->geometry().y() > before, "nested wrap does not change following position");
    require(label->geometry().height() + .1f >= label->heightForWidth(label->geometry().width()), "wrapped text clipped");
    const auto height = label->geometry().height();
    label->setPixelSize(24);
    require(label->geometry().height() > height, "font invalidation");
    require(geeyoou::skins().apply("light"), "theme exists"); host.performLayout();
    require(geeyoou::skins().apply("dark"), "dark exists"); host.performLayout();
}
void percentageSpanBaselineAndPixels() {
    Widget host;
    auto* grid = host.setLayout<AxislyGridLayout>();
    grid->setSpacing(0);
    grid->setColumns({axisly::css::TrackSizing::percent(.25f), axisly::css::TrackSizing::fraction(1)});
    auto* a = host.add<Hint>(); auto* b = host.add<Hint>();
    grid->addWidget(a, 0, 0); grid->addWidget(b, 1, 0, 1, 2);
    host.setGeometry({0, 0, 400, 200});
    near(a->geometry().width(), 100, "percentage width");
    near(b->geometry().width(), 400, "spanned width");
    require(host.sizeHint().preferred.width < 1000, "intrinsic percentage has no huge synthetic basis");
    grid->setColumns({axisly::css::TrackSizing::fraction(1), axisly::css::TrackSizing::fraction(1)});
    require(host.sizeHint().preferred.width < 1000, "intrinsic Range must not expand against a sentinel size");
    for (float scale : {1.f, 1.25f, 1.5f, 2.f}) {
        grid->setPixelScale(scale); host.setGeometry({0, 0, 403, 203});
        const auto r = a->geometry();
        near(r.right()*scale, std::round(r.right()*scale), "physical pixel edge");
    }
    Widget baselines;
    auto* baseline = baselines.setLayout<AxislyGridLayout>();
    auto* c = baselines.add<Hint>(); auto* d = baselines.add<Hint>();
    c->hint.min.height = c->hint.preferred.height = c->hint.max.height = 30;
    d->hint.min.height = d->hint.preferred.height = d->hint.max.height = 40;
    axisly::css::Style x; x.align_self = axisly::css::ItemAlignment{axisly::css::ItemPosition::Baseline};
    baseline->addCell(c, x, 10.0f); x.grid.column.start = axisly::css::GridLine::index(2); baseline->addCell(d, x, 25.0f);
    baselines.setGeometry({0,0,300,100});
    near(c->geometry().y()+10, d->geometry().y()+25, "baseline alignment");
}
void lifetime(bool duringMeasure, int action) {
    Widget owner;
    auto* host = owner.add<Widget>();
    auto* grid = host->setLayout<AxislyGridLayout>();
    auto* a = host->add<Action>(); auto* b = host->add<Hint>();
    grid->addWidget(a, 0, 0); grid->addWidget(b, 0, 1);
    host->setGeometry({0,0,300,100});
    const auto callback = [&] {
        if (action == 0) owner.removeChild(host);
        else if (action == 1) host->setLayout<AxislyGridLayout>();
        else if (action == 2) host->removeChild(a);
        else (void)host->sizeHint();
    };
    if (duringMeasure) { a->measured = callback; a->invalidateSizeHint(); }
    else { a->arranged = callback; grid->setColumnStretch(0, 1); }
    require(geeyoou::detail::parkedLayoutCount() == 0, "parked layouts leaked");
}
void failureAtomicity() {
    Widget host;
    auto* grid = host.setLayout<AxislyGridLayout>();
    auto* a = host.add<Hint>(); auto* b = host.add<Hint>();
    grid->addWidget(a, 0, 0); grid->addWidget(b, 0, 1); host.setGeometry({0,0,300,100});
    const auto old = a->geometry();
    b->hint.preferred.width = std::numeric_limits<float>::quiet_NaN();
    b->invalidateSizeHint();
    require(a->geometry() == old, "measurement failure publishes partial geometry");
}
void responsiveViewport() {
    Widget host;
    auto* root = host.setLayout<AxislyGridLayout>();
    root->setColumns({axisly::css::TrackSizing::fraction(1)}); root->setRows({axisly::css::TrackSizing::fraction(1)});
    auto* area = host.add<axisly_demo::AxislyScrollArea>(); root->addWidget(area, 0, 0);
    auto* content = area->content();
    auto* page = content->setLayout<AxislyBoxLayout>(AxislyBoxLayout::Orientation::Vertical);
    page->setPreferredWidth(700);
    auto* text = content->add<geeyoou::Label>(); text->setWordWrap(true); text->setText("Short");
    page->addWidget(text);
    auto* tail = content->add<Hint>(); page->addWidget(tail);
    host.setGeometry({0,0,320,180}); host.animationTickTree();
    require(content->geometry().width() <= area->geometry().width(), "viewport must constrain preferred page width");
    const auto first = tail->geometry().y();
    text->setText("This longer text must be remeasured through the viewport, across its plain internal wrapper, without resizing the window.");
    host.animationTickTree();
    require(tail->geometry().y() > first, "viewport content dirtiness was lost across plain wrapper");
    text->setVisible(false); host.animationTickTree();
    near(tail->geometry().y(), 0, "hidden viewport content still reserves space");
}

void cssMixedHost() {
    Widget host;
    const auto page = axisly_demo::buildCssHostPage(host);
    auto* layout = page.layout;
    auto* wrapped = page.wrapped;
    auto* tail = page.tail;
    auto* side = page.side;

    host.setGeometry({0, 0, 400, 220});
    require(side->geometry().x() >= 200, "CSS Grid must place the Block panel in the second column");
    require(tail->geometry().y() >= wrapped->geometry().bottom(),
        "CSS Flex must place its next label after wrapped text");
    const auto before = wrapped->geometry().height();
    wrapped->setText("Long text 中文换行 testing repeated words in a real host label. "
        "This text must wrap after the content changes and the CSS tree is rebuilt.");
    host.setGeometry({0, 0, 240, 220});
    require(wrapped->geometry().height() > before,
        "CSS host must remeasure a wrapped GeeyoouUI label after text and width changes");
    require(tail->geometry().y() >= wrapped->geometry().bottom(),
        "CSS host must move the following Flex label after wrapping");
    for (float scale : {1.0f, 1.25f, 1.5f, 2.0f}) {
        layout->setPixelScale(scale);
        host.setGeometry({0, 0, 403, 220});
        near(side->geometry().x() * scale,
            std::round(side->geometry().x() * scale), "CSS host DPI edge");
    }
    host.removeChild(side);
    host.performLayout();
    require(tail->geometry().height() > 0,
        "CSS host child removal must retain the other layout branch");
}
void cssHostLifetime(bool during_measure) {
    geeyoou::Widget owner;
    auto* host = owner.add<geeyoou::Widget>();
    axisly::css::Style style;
    style.display = axisly::css::Display::Flex;
    auto* layout = host->setLayout<axisly_demo::CssAxislyLayout>(style);
    auto* child = host->add<Action>();
    layout->addWidget(layout->root(), child);
    host->setGeometry({0, 0, 200, 100});
    if (during_measure) {
        child->measured = [&] { owner.removeChild(host); };
        child->invalidateSizeHint();
    } else {
        child->arranged = [&] { owner.removeChild(host); };
        layout->setPixelScale(1.25f);
    }
    require(geeyoou::detail::parkedLayoutCount() == 0,
        "CSS host layout remained parked after reentrant destruction");
}
void cssHostInvalidMeasurement() {
    geeyoou::Widget host;
    axisly::css::Style style;
    style.display = axisly::css::Display::Flex;
    auto* layout = host.setLayout<axisly_demo::CssAxislyLayout>(style);
    auto* child = host.add<Hint>();
    layout->addWidget(layout->root(), child);
    host.setGeometry({0, 0, 200, 100});
    const auto prior = child->geometry();
    child->hint.preferred.width = std::numeric_limits<float>::quiet_NaN();
    child->invalidateSizeHint();
    require(child->geometry() == prior,
        "CSS host invalid measurement published partial geometry");
}
}
int main() {
    try {
        cacheAndRemoval(); maxAndOverflow(); boxSpacingAndVisibility(); nestedInvalidation();
        textAndStyle(); percentageSpanBaselineAndPixels(); failureAtomicity(); responsiveViewport();
        cssMixedHost();
        for (auto feature : {axisly_demo::CssFeature::Flow, axisly_demo::CssFeature::Flex,
                axisly_demo::CssFeature::Grid}) {
            Widget host;
            const auto page = axisly_demo::buildCssFeaturePage(host, feature);
            for (float scale : {1.f, 1.25f, 1.5f, 2.f})
                axisly_demo::exerciseCssFeaturePage(host, page, scale);
        }
        cssHostLifetime(false); cssHostLifetime(true);
        cssHostInvalidMeasurement();
        for (bool measure : {false, true}) for (int action = 0; action < 4; ++action) lifetime(measure, action);
        std::puts("axisly GeeyoouUI adapter: 22 scenario groups passed");
        return 0;
    } catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
