#include "Pages.hpp"
#include "AxislyLayout.hpp"
#include "geeyoou/render/Theme.hpp"
#include "geeyoou/widget/GroupBox.hpp"
#include "geeyoou/widget/Label.hpp"
#include "geeyoou/widget/PushButton.hpp"
#include "i18n/I18n.hpp"
#include <cstdio>
#include <memory>

namespace showcase {
using namespace geeyoou;
namespace {
std::string words(const char* zh, const char* en) { return lang() == 0 ? zh : en; }
class Swatch final : public Widget {
public:
    std::string text;
    Color color;
    SizeHint hint{{60, 48}, {120, 56}, {kUnbounded, 56}};
    std::optional<float> baseline;
    SizeHint sizeHint() const override { return hint; }
protected:
    void onPaint(Painter& p, const Rect&) override {
        const auto& theme = Theme::current();
        const auto r = localRect();
        p.fillRoundRect(r, theme.radius, color.withAlpha(40));
        p.strokeRoundRect(r.deflated(.5f), theme.radius, color, 1);
        p.drawText({12, 9}, text, 12, theme.text, HAlign::Left, VAlign::Top);
        char size[64]; std::snprintf(size, sizeof(size), "%.1f x %.1f", r.width(), r.height());
        p.drawText({12, r.height()-10}, size, 11, theme.textDim, HAlign::Left, VAlign::Bottom);
        if (baseline) p.strokeLine({0, *baseline}, {r.width(), *baseline}, color, 1);
    }
};
AxislyBoxLayout* stack(Widget* host, float gap = 12) {
    auto* layout = host->setLayout<AxislyBoxLayout>(AxislyBoxLayout::Orientation::Vertical);
    layout->setSpacing(gap); return layout;
}
AxislyBoxLayout* line(Widget* host) {
    auto* layout = host->setLayout<AxislyBoxLayout>(AxislyBoxLayout::Orientation::Horizontal);
    layout->setSpacing(12); return layout;
}
Label* note(Widget* host, AxislyBoxLayout* layout, std::string text) {
    auto* label = host->add<Label>(); label->setText(std::move(text));
    label->setWordWrap(true); label->addStyleClass("caption");
    layout->addWidget(label); return label;
}
GroupBox* panel(Widget* host, AxislyBoxLayout* page, std::string title) {
    auto* group = host->add<GroupBox>(); group->setTitle(std::move(title)); page->addWidget(group); return group;
}
Swatch* swatch(Widget* host, std::string text, Color color) {
    auto* widget = host->add<Swatch>(); widget->text = std::move(text); widget->color = color; return widget;
}
}

Size buildLayoutPage(Widget* content) {
    const auto& theme = Theme::current();
    auto* page = stack(content, 16);
    page->setPreferredWidth(1044);
    note(content, page, words("这里的控件位置均由 axisly 计算。缩放窗口可查看实际宽度；下方按钮可修改内容、增删控件。",
        "axisly computes these widget rectangles. Resize to inspect their widths; use the buttons below to update, add or remove items."));

    auto* weights = panel(content, page, words("① 权重轨道与最大尺寸", "① Weighted tracks and maximum size"));
    auto* column = stack(weights);
    note(weights, column, words("固定控件宽 140；其余空间按 1 : 2 分配。有上限的控件最多 240，余量分给另一轨道。",
        "The fixed item is 140 wide. Flexible tracks share space 1 : 2; the capped track stops at 240 and releases space to its neighbor."));
    auto* row = weights->add<Widget>(); column->addWidget(row);
    auto* boxes = line(row);
    auto* fixed = swatch(row, words("固定 140", "Fixed 140"), theme.textDim);
    fixed->hint = {{140, 56}, {140, 56}, {140, 56}}; boxes->addWidget(fixed);
    auto* growing = swatch(row, "1fr / min 80", theme.accent);
    growing->hint.min.width = 80; boxes->addWidget(growing, 1);
    auto* capped = swatch(row, "2fr / max 240", theme.ok);
    capped->hint.max.width = 240; boxes->addWidget(capped, 2);

    auto* fractions = panel(content, page, words("② 百分比、跨格与自动放置", "② Percentage, spans and auto placement"));
    auto* fractionColumn = stack(fractions);
    note(fractions, fractionColumn, words("第一列占可用宽度 25%，其余两列按权重分配；未指定位置的控件按 dense 规则填入，并自动增加行。",
        "The first column uses 25% of the available width. Two weighted columns share the remainder; dense auto placement creates implicit rows."));
    auto* matrix = fractions->add<Widget>(); fractionColumn->addWidget(matrix);
    auto* grid = matrix->setLayout<AxislyGridLayout>(); grid->setSpacing(12);
    grid->setColumns({axisly::css::TrackSizing::percent(.25f), axisly::css::TrackSizing::minmax(axisly::css::TrackBreadth::length(120), axisly::css::TrackBreadth::fraction(1)), axisly::css::TrackSizing::minmax(axisly::css::TrackBreadth::length(120), axisly::css::TrackBreadth::fraction(1))});
    grid->setAutoFlow(axisly::css::GridAutoFlow::RowDense);
    for (int i = 0; i < 6; ++i) {
        auto* tile = swatch(matrix, i == 0 ? "span 2" : "auto " + std::to_string(i), i % 2 ? theme.accent : theme.ok);
        axisly::css::Style item;

        if (i == 0) item.grid.column.end = axisly::css::GridLine::span(2);
        grid->addCell(tile, item);
    }

    auto* baselineGroup = panel(content, page, words("③ 首基线对齐", "③ First baseline alignment"));
    auto* baselineGrid = baselineGroup->setLayout<AxislyGridLayout>();
    baselineGrid->setSpacing(12);
    baselineGrid->setColumns({axisly::css::TrackSizing::fraction(1), axisly::css::TrackSizing::fraction(1), axisly::css::TrackSizing::fraction(1)});
    for (int i = 0; i < 3; ++i) {
        const float height = 56 + float(i)*12, baseline = 24 + float(i)*12;
        auto* tile = swatch(baselineGroup, "baseline " + std::to_string(int(baseline)), theme.warn);
        tile->hint = {{100, height}, {180, height}, {kUnbounded, height}}; tile->baseline = baseline;
        axisly::css::Style item; item.grid.column.start = axisly::css::GridLine::index(static_cast<std::int16_t>(i + 1));
        item.align_self = axisly::css::ItemAlignment{axisly::css::ItemPosition::Baseline};
        baselineGrid->addCell(tile, item, baseline);
    }

    auto* dynamic = panel(content, page, words("④ 动态内容、换行与布局失效", "④ Dynamic content, wrapping and invalidation"));
    auto* dynamicColumn = stack(dynamic);
    auto* description = note(dynamic, dynamicColumn, words("短文字。点击“修改文字”观察换行和面板高度。",
        "Short text. Click Update text to observe wrapping and panel height."));
    auto* buttonsHost = dynamic->add<Widget>(); dynamicColumn->addWidget(buttonsHost);
    auto* buttons = line(buttonsHost);
    auto addButton = [&](std::string title) {
        auto* button = buttonsHost->add<PushButton>(); button->setText(std::move(title)); buttons->addWidget(button); return button;
    };
    auto* update = addButton(words("修改文字", "Update text"));
    auto* show = addButton(words("隐藏 / 显示", "Hide / show"));
    auto* add = addButton(words("增加一项", "Add item"));
    auto* remove = addButton(words("移除末项", "Remove last"));
    auto* itemsHost = dynamic->add<Widget>(); dynamicColumn->addWidget(itemsHost);
    auto* items = itemsHost->setLayout<AxislyGridLayout>(); items->setSpacing(10);
    items->setColumns({axisly::css::TrackSizing::minmax(axisly::css::TrackBreadth::length(100), axisly::css::TrackBreadth::fraction(1)), axisly::css::TrackSizing::minmax(axisly::css::TrackBreadth::length(100), axisly::css::TrackBreadth::fraction(1)), axisly::css::TrackSizing::minmax(axisly::css::TrackBreadth::length(100), axisly::css::TrackBreadth::fraction(1))});
    items->setAutoFlow(axisly::css::GridAutoFlow::Row);
    auto live = std::make_shared<std::vector<Widget*>>();
    auto serial = std::make_shared<int>(0);
    auto append = [itemsHost, items, live, serial] {
        auto* tile = swatch(itemsHost, "item " + std::to_string(++*serial), Theme::current().accent);
        live->push_back(tile);
        axisly::css::Style item;
        items->addCell(tile, item);
    };
    append(); append(); append();
    update->clicked.connect([description, expanded = false]() mutable {
        expanded = !expanded;
        description->setText(expanded ? words(
            "这是一段更长的真实控件文字。axisly 会按当前可用宽度重新测量，改变行高与外层面板高度。缓存只在文字、约束或样式变化后失效；继续缩小窗口，可以看到中文和英文的自动换行。",
            "This is longer text measured by the real host widget. axisly uses the available width to update line and panel heights. "
            "Cached measurements are invalidated when text, constraints or style change. Resize the window to inspect wrapping and nested layout.")
            : words("短文字。", "Short text."));
    });
    show->clicked.connect([description] { description->setVisible(!description->isVisible()); });
    add->clicked.connect(append);
    remove->clicked.connect([itemsHost, live] {
        if (live->empty()) return;
        auto* widget = live->back(); live->pop_back(); itemsHost->removeChild(widget);
    });
    return content->sizeHint().preferred;
}
} // namespace showcase
