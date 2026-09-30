#pragma once
#include <array>

namespace geeyoou { class Widget; class Label; }
namespace axisly_demo {
class CssAxislyLayout;
enum class CssFeature { Flow, Flex, Grid };
struct CssFeaturePage {
    CssAxislyLayout* layout{};
    CssFeature feature{};
    std::array<geeyoou::Label*, 6> labels{};
};
CssFeaturePage buildCssFeaturePage(geeyoou::Widget&, CssFeature);
const char* cssFeatureName(CssFeature) noexcept;
} // namespace axisly_demo
