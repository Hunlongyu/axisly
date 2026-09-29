#include "CssHostPage.hpp"
#include "CssAxislyLayout.hpp"

#include "geeyoou/widget/Label.hpp"
#include "geeyoou/widget/Widget.hpp"

namespace axisly_demo {
CssHostPage buildCssHostPage(geeyoou::Widget& host) {
    namespace css = axisly::css;
    css::Style root_style;
    root_style.display = css::Display::Grid;
    root_style.grid.template_columns = {
        css::TrackSizing::fraction(1), css::TrackSizing::fraction(1)};
    root_style.grid.template_rows = {css::TrackSizing::automatic()};
    root_style.gap.width = css::Length::length(8);
    auto* layout = host.setLayout<CssAxislyLayout>(root_style);

    css::Style flex_style;
    flex_style.display = css::Display::Flex;
    flex_style.flex_direction = css::FlexDirection::Column;
    flex_style.grid.column.start = css::GridLine::index(1);
    css::Style block_style;
    block_style.display = css::Display::Block;
    block_style.grid.column.start = css::GridLine::index(2);
    const auto flex = layout->addContainer(layout->root(), flex_style);
    const auto block = layout->addContainer(layout->root(), block_style);

    auto* wrapped = host.add<geeyoou::Label>();
    wrapped->setWordWrap(true);
    wrapped->setText("Short diagnostic label");
    auto* tail = host.add<geeyoou::Label>();
    tail->setText("Following Flex item");
    auto* side = host.add<geeyoou::Label>();
    side->setText("Block-side label");
    css::Style leaf_style;
    leaf_style.display = css::Display::Block;
    layout->addWidget(flex, wrapped, leaf_style);
    layout->addWidget(flex, tail, leaf_style);
    layout->addWidget(block, side, leaf_style);
    return {layout, wrapped, tail, side};
}
} // namespace axisly_demo
