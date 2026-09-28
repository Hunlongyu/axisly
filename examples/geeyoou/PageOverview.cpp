// GeeyoouUI Showcase adaptation, MIT; see NOTICE.md.
#include "Pages.hpp"
#include "AxislyLayout.hpp"
#include <cstdio>
#include "geeyoou/render/Theme.hpp"
#include "geeyoou/widget/GroupBox.hpp"
#include "geeyoou/widget/Label.hpp"
#include "geeyoou/widget/ProgressBar.hpp"
#include "i18n/I18n.hpp"

namespace showcase {
using namespace geeyoou;
namespace {
std::string words(const char* zh, const char* en) { return lang() == 0 ? zh : en; }
class StatTile final : public Widget {
public:
    std::string caption, value;
    Color accent;
    void setValue(std::string text) { value = std::move(text); update(); }
    SizeHint sizeHint() const override { return {{170, 76}, {220, 76}, {kUnbounded, 76}}; }
protected:
    void onPaint(Painter& p, const Rect&) override {
        const auto& t = Theme::current();
        const auto r = localRect();
        p.fillRoundRect(r, t.radius, t.panel);
        p.strokeRoundRect(r.deflated(0.5f), t.radius, t.panelBorder, 1);
        p.fillRoundRect({0, 10, 3, r.height()-20}, 1.5f, accent);
        p.drawText({16, 15}, caption, t.fontSmall, t.textDim, HAlign::Left, VAlign::Top);
        p.drawText({16, 38}, value, 22, t.text, HAlign::Left, VAlign::Top);
    }
};
AxislyBoxLayout* stack(Widget* host, float spacing = 10) {
    auto* layout = host->setLayout<AxislyBoxLayout>(AxislyBoxLayout::Orientation::Vertical);
    layout->setSpacing(spacing);
    return layout;
}
Label* paragraph(Widget* host, AxislyBoxLayout* layout, std::string text) {
    auto* label = host->add<Label>();
    label->setText(std::move(text));
    label->setWordWrap(true);
    label->setAlign(HAlign::Left, VAlign::Top);
    layout->addWidget(label);
    return label;
}
}

Size buildOverviewPage(Widget* content, AppState& app) {
    auto* page = content->setLayout<AxislyGridLayout>();
    page->setSpacing(16);
    page->setColumns({axisly::Track::weighted(1, 170), axisly::Track::weighted(1, 170),
                      axisly::Track::weighted(1, 170), axisly::Track::weighted(1, 170)});
    const auto& theme = Theme::current();
    StatTile* tiles[4]{};
    const std::string captions[] = {tr("釜内温度（实时）"), words("布局引擎", "Layout engine"),
                                  words("演示页面", "Showcase pages"), words("绘制与控件", "Renderer")};
    const std::string values[] = {"--", "axisly", "19", "GeeyoouUI"};
    const Color accents[] = {theme.accent, theme.ok, theme.warn, theme.primary};
    for (int i = 0; i < 4; ++i) {
        tiles[i] = content->add<StatTile>();
        tiles[i]->caption = captions[i]; tiles[i]->value = values[i]; tiles[i]->accent = accents[i];
        page->addWidget(tiles[i], 0, i);
    }
    auto* ticker = content->add<Ticker>();
    ticker->divisor = 6;
    ticker->onTick = [&app, tile = tiles[0]] {
        char text[48];
        std::snprintf(text, sizeof(text), "%.1f °C", app.hub.lastValue(app.chTemp));
        tile->setValue(text);
    };
    auto* about = content->add<GroupBox>();
    about->setTitle("axisly + GeeyoouUI");
    auto* aboutLayout = stack(about, 16);
    paragraph(about, aboutLayout, tr("面向工控 HMI / 上位机的跨平台 C++20 自绘控件库。\n\n"
        "· 自绘渲染（Blend2D），跨平台外观完全一致\n"
        "· 无 moc —— 信号槽用模板 + std::function，没有代码生成步骤\n"
        "· 脏矩形增量重绘 —— HMI 画面 90% 的像素是静止的\n"
        "· 热路径零分配 —— 实时数据走固定容量环形缓冲\n"
        "· 平台层是纯虚接口 —— 新后端只需给出一块像素缓冲\n\n"
        "左侧导航逐页浏览各控件族。所有页面共用同一个采集线程与 DataHub：\n"
        "「HMI 监控」和「运维控制台」看到的是同一份实时数据。"));
    page->addWidget(about, 1, 0, 1, 2);

    auto* layers = content->add<GroupBox>();
    layers->setTitle(words("宿主与布局的职责", "Host and layout responsibilities"));
    auto* layersLayout = stack(layers, 12);
    const std::string names[] = {
        words("控件、输入与事件 → GeeyoouUI", "Widgets, input and events → GeeyoouUI"),
        words("绘制、主题与图标 → GeeyoouUI / Blend2D", "Painting, themes and icons → GeeyoouUI / Blend2D"),
        words("页面与容器的排列 → axisly", "Page and container placement → axisly"),
        words("文字测量 → GeeyoouUI；尺寸约束 → axisly", "Text measurement → GeeyoouUI; constraints → axisly"),
        words("表格虚拟化与三维场景 → 原有控件", "Table virtualization and 3D scenes → original widgets")};
    for (int i = 0; i < 5; ++i) {
        paragraph(layers, layersLayout, names[i]);
    }
    page->addWidget(layers, 1, 2, 1, 2);

    auto* scope = content->add<GroupBox>();
    scope->setTitle(words("试一试", "Try it"));
    auto* scopeLayout = stack(scope, 12);
    paragraph(scope, scopeLayout, words(
        "拖动窗口边缘、切换页面，或在标题栏切换中英文。axisly 使用真实控件的尺寸提示与换行测量，计算卡片、表单和嵌套面板的位置。",
        "Resize the window, switch pages and change the theme or language. "
        "axisly measures real GeeyoouUI widgets and arranges the page containers. "
        "Charts, process diagrams, table virtualization and 3D rendering remain owned by their widgets."));
    paragraph(scope, scopeLayout, words(
        "布局引擎页面展示权重、尺寸限制、跨格与动态内容。固定设计画面保留其最小画布，在较小窗口内可滚动查看。",
        "The Layout Engine page demonstrates weighted tracks, min/max, spans and forms. "
        "Fixed-design pages preserve their minimum canvas and remain scrollable in smaller windows."));
    page->addWidget(scope, 2, 0, 1, 4);
    // Preferred width is the authored readable canvas, not one enormous line
    // of natural paragraph text. Height is measured at this width by axisly.
    page->setPreferredWidth(1044);
    return content->sizeHint().preferred;
}
} // namespace showcase
