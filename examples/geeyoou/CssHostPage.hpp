#pragma once

namespace geeyoou {
class Widget;
class Label;
}

namespace axisly_demo {
class CssAxislyLayout;

struct CssHostPage {
    CssAxislyLayout* layout{};
    geeyoou::Label* wrapped{};
    geeyoou::Label* tail{};
    geeyoou::Label* side{};
};

// Populate a real GeeyoouUI widget with a nested Grid/Flex/Block CSS tree.
CssHostPage buildCssHostPage(geeyoou::Widget& host);
} // namespace axisly_demo
