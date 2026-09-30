#include "CssFeaturePages.hpp"
#include "CssAxislyLayout.hpp"
#include "geeyoou/widget/Label.hpp"
#include "geeyoou/widget/Widget.hpp"
#include <stdexcept>
#include <utility>

namespace axisly_demo {
namespace {
namespace css = axisly::css;
css::Style block() {
    css::Style style;
    style.display = css::Display::Block;
    style.box.min_size.width = css::Length::length(0);
    return style;
}
geeyoou::Label* label(geeyoou::Widget& host, CssAxislyLayout& layout,
    CssAxislyLayout::Handle parent, const char* text, css::Style style = block(),
    float pixels = 14) {
    auto* item = host.add<geeyoou::Label>();
    item->setText(text);
    item->setWordWrap(true);
    item->setPixelSize(pixels);
    item->setAlign(geeyoou::HAlign::Left, geeyoou::VAlign::Top);
    layout.addWidget(parent, item, std::move(style));
    return item;
}
CssFeaturePage flow(geeyoou::Widget& host) {
    auto root = block();
    root.display = css::Display::FlowRoot;
    root.box.padding = {css::Length::length(8), css::Length::length(8),
        css::Length::length(8), css::Length::length(8)};
    auto* layout = host.setLayout<CssAxislyLayout>(root);
    auto floated = block();
    floated.box.size = {css::Length::length(80), css::Length::length(40)};
    floated.float_side = css::FloatSide::Left;
    auto* left = label(host, *layout, layout->root(), "Left float\n左浮动", floated);
    floated.float_side = css::FloatSide::Right;
    floated.box.size.height = css::Length::length(60);
    auto* right = label(host, *layout, layout->root(), "Right float\n右浮动", floated);
    auto body_style = block();
    body_style.display = css::Display::FlowRoot;
    const auto body = layout->addContainer(layout->root(), body_style);
    auto* wrapped = label(host, *layout, body, "FlowRoot text / 独立格式化上下文");
    auto cleared = block();
    cleared.clear = css::ClearSide::Both;
    cleared.box.margin.top = css::Length::length(8);
    auto* tail = label(host, *layout, layout->root(), "Clear both / 清除两侧浮动", cleared);
    return {layout, CssFeature::Flow, {left, right, wrapped, tail}};
}
CssFeaturePage flex(geeyoou::Widget& host) {
    auto* layout = host.setLayout<CssAxislyLayout>(block());
    auto row = block();
    row.display = css::Display::Flex;
    row.flex_wrap = css::FlexWrap::Balance;
    row.flex_line_count = 2;
    row.gap = {css::Length::length(6), css::Length::length(6)};
    const auto balanced = layout->addContainer(layout->root(), row);
    CssFeaturePage page{layout, CssFeature::Flex};
    constexpr const char* captions[] = {"Balance / 平衡", "Second item", "第三项", "Fourth item"};
    for (std::size_t i = 0; i != 4; ++i) {
        auto item = block();
        item.box.size.width = css::Length::length(100);
        item.flex_shrink = 0;
        page.labels[i] = label(host, *layout, balanced, captions[i], item);
    }
    row.flex_wrap = css::FlexWrap::NoWrap;
    row.align_items = css::ItemAlignment{css::ItemPosition::Baseline};
    row.box.margin.top = css::Length::length(12);
    const auto baseline = layout->addContainer(layout->root(), row);
    page.labels[4] = label(host, *layout, baseline, "14 px", block(), 14);
    page.labels[5] = label(host, *layout, baseline, "24 px / 字体", block(), 24);
    return page;
}
CssFeaturePage grid(geeyoou::Widget& host) {
    auto root = block();
    root.display = css::Display::Grid;
    root.grid.template_columns = {css::TrackSizing::fraction(1), css::TrackSizing::fraction(1)};
    root.grid.column_line_names = {{"left"}, {"middle"}, {"right"}};
    root.grid.template_rows = {css::TrackSizing::automatic(), css::TrackSizing::automatic(),
        css::TrackSizing::fraction(1)};
    root.gap = {css::Length::length(8), css::Length::length(8)};
    auto* layout = host.setLayout<CssAxislyLayout>(root);
    auto header = block();
    header.grid.column = {css::GridLine::named("left"), css::GridLine::index(-1)};
    header.grid.row.start = css::GridLine::index(1);
    auto* title = label(host, *layout, layout->root(), "Named lines / 具名线和跨列", header, 18);
    auto first = block();
    first.grid.column = {css::GridLine::named("left"), css::GridLine::named("middle")};
    first.grid.row.start = css::GridLine::index(2);
    auto* left = label(host, *layout, layout->root(), "Left / 第一列", first);
    first.grid.column = {css::GridLine::named("middle"), css::GridLine::named("right")};
    auto* right = label(host, *layout, layout->root(), "Right / 第二列", first);
    auto nested = block();
    nested.display = css::Display::Grid;
    nested.grid.column = {css::GridLine::index(1), css::GridLine::index(-1)};
    nested.grid.row.start = css::GridLine::index(3);
    nested.grid.template_columns = {css::GridRepeat{css::RepeatKind::AutoFit, 1,
        {css::TrackSizing::minmax(css::TrackBreadth::length(80), css::TrackBreadth::fraction(1))}}};
    nested.gap.width = css::Length::length(6);
    const auto panel = layout->addContainer(layout->root(), nested);
    auto* wrapped = label(host, *layout, panel, "Auto-fit text / 自动重复轨道中的换行文字");
    auto* peer = label(host, *layout, panel, "Nested grid / 嵌套网格");
    auto badge = block();
    badge.position = css::Position::Absolute;
    badge.inset.right = css::Length::length(4);
    badge.inset.bottom = css::Length::length(4);
    badge.box.size = {css::Length::length(100), css::Length::length(24)};
    auto* absolute = label(host, *layout, layout->root(), "ABS / 定位", badge);
    return {layout, CssFeature::Grid, {title, left, right, wrapped, peer, absolute}};
}
} // namespace
CssFeaturePage buildCssFeaturePage(geeyoou::Widget& host, CssFeature feature) {
    switch (feature) {
    case CssFeature::Flow: return flow(host);
    case CssFeature::Flex: return flex(host);
    case CssFeature::Grid: return grid(host);
    }
    throw std::invalid_argument("Unknown CSS feature page");
}
const char* cssFeatureName(CssFeature feature) noexcept {
    switch (feature) {
    case CssFeature::Flow: return "flow";
    case CssFeature::Flex: return "flex";
    case CssFeature::Grid: return "grid";
    }
    return "unknown";
}
} // namespace axisly_demo
