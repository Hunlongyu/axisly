#include "CssHostAcceptance.hpp"
#include "CssAxislyLayout.hpp"
#include "geeyoou/widget/Label.hpp"
#include <cmath>
#include <stdexcept>

namespace axisly_demo {
namespace {
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
void validate(const CssFeaturePage& page, float scale, float width, float height) {
    const auto& p = page.labels;
    for (const auto* label : p) if (label) {
        const auto r = label->geometry();
        check(std::isfinite(r.x()) && std::isfinite(r.y()) && r.width() >= 0 && r.height() > 0,
            "CSS feature has invalid label geometry");
        check(std::abs(r.x() * scale - std::round(r.x() * scale)) < .02f
            && std::abs(r.y() * scale - std::round(r.y() * scale)) < .02f,
            "CSS feature label is off the physical pixel grid");
    }
    const float tolerance = 1.1f / scale;
    switch (page.feature) {
    case CssFeature::Flow:
        check(std::abs(p[0]->geometry().x() - 8) < tolerance,
            "Left float missed content edge");
        check(std::abs(p[1]->geometry().right() - (width - 8)) < tolerance,
            "Right float missed content edge");
        check(p[2]->geometry().x() + tolerance >= p[0]->geometry().right()
            && p[2]->geometry().right() <= p[1]->geometry().x() + tolerance,
            "FlowRoot did not avoid both floats");
        check(p[3]->geometry().y() + tolerance >= p[0]->geometry().bottom()
            && p[3]->geometry().y() + tolerance >= p[1]->geometry().bottom(),
            "Clear both did not pass both float bottoms");
        break;
    case CssFeature::Flex:
        check(p[2]->geometry().y() > p[0]->geometry().y(),
            "Balance did not create the requested second line");
        check(p[1]->geometry().x() >= p[0]->geometry().right(),
            "Balance items overlap");
        // GeeyoouUI exposes no text baseline. The adapter supplies no baseline,
        // so CSS synthesizes it at each content box bottom.
        check(std::abs(p[4]->geometry().bottom() - p[5]->geometry().bottom()) < tolerance,
            "Synthesized baseline did not align different font sizes");
        break;
    case CssFeature::Grid:
        check(std::abs(p[0]->geometry().width() - width) < tolerance,
            "Named/negative line header did not span both columns");
        check(p[2]->geometry().x() > p[1]->geometry().right(),
            "Named columns did not preserve the gap");
        check(p[4]->geometry().x() > p[3]->geometry().right(),
            "Nested auto-fit did not keep two occupied tracks");
        check(std::abs(p[5]->geometry().right() - (width - 4)) < tolerance
            && std::abs(p[5]->geometry().bottom() - (height - 4)) < tolerance,
            "Absolute badge missed the padding-box inset");
        break;
    }
}
}
void exerciseCssFeaturePage(geeyoou::Widget& host, const CssFeaturePage& page, float scale) {
    page.layout->setPixelScale(scale);
    host.setGeometry({24, 32, 430, 300});
    validate(page, scale, 430, 300);
    auto* wrapped = page.labels[page.feature == CssFeature::Flow ? 2 : 3];
    const auto old = wrapped->geometry().height();
    wrapped->setText("Long live font text 中文换行：布局需要跟随内容和窗口宽度变化。 "
        "Repeated text exercises intrinsic measurement, real glyphs and wrapping.");
    host.setGeometry({24, 32, 260, 300});
    validate(page, scale, 260, 300);
    if (page.feature == CssFeature::Flow)
        check(wrapped->geometry().height() > old, "Flow content mutation did not remeasure text");
    wrapped->setVisible(false);
    host.performLayout();
    wrapped->setVisible(true);
    host.performLayout();
    validate(page, scale, 260, 300);
    const float screenshot_height = page.feature == CssFeature::Flex ? 480.f : 300.f;
    host.setGeometry({24, 32, 430, screenshot_height});
    validate(page, scale, 430, screenshot_height);
}
} // namespace axisly_demo
