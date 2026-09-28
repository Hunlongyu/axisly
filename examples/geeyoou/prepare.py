"""Produce a build-local Showcase overlay from the pinned upstream sources.

Upstream MIT copyright is preserved in NOTICE.md and the fetched LICENSE.
Every structural replacement is checked, so upstream drift fails at configure.
"""
import pathlib
import re
import sys

source, output = map(pathlib.Path, sys.argv[1:])
output.mkdir(parents=True, exist_ok=True)

def write_changed(path, text):
    if not path.exists() or path.read_text(encoding="utf-8") != text:
        path.write_text(text, encoding="utf-8")


def replace_once(text, before, after):
    if text.count(before) != 1:
        raise RuntimeError(f"Pinned upstream patch mismatch: {before[:100]!r}")
    return text.replace(before, after)


widget = (source / "src/widget/Widget.cpp").read_text(encoding="utf-8")
widget = replace_once(widget, "    w->layoutDirty_ = true;\n    top = w;",
                      "    w->layoutDirty_ = true;\n"
                      "    w->layout_->onInvalidated(); // axisly cache revision\n    top = w;")
write_changed(output / "Widget.cpp", widget)
layout = (source / "src/widget/Layout.cpp").read_text(encoding="utf-8")
layout = replace_once(layout, "  if (host_) host_->performLayout();",
                      "  if (host_) host_->invalidateSizeHint(); // invalidate ancestor measurements too")
write_changed(output / "Layout.cpp", layout)

for path in (source / "examples/showcase").rglob("*"):
    if path.suffix not in {".cpp", ".hpp"}:
        continue
    dest = output / "showcase" / path.relative_to(source / "examples/showcase")
    dest.parent.mkdir(parents=True, exist_ok=True)
    text = path.read_text(encoding="utf-8")
    text = text.replace('#include "geeyoou/widget/BoxLayout.hpp"', '#include "AxislyLayout.hpp"')
    text = text.replace('#include "geeyoou/widget/GridLayout.hpp"', '#include "AxislyLayout.hpp"')
    text = re.sub(r"\bBoxLayout\b", "AxislyBoxLayout", text)
    text = re.sub(r"\bGridLayout\b", "AxislyGridLayout", text)
    if path.name == "Shell.hpp":
        text = replace_once(text, "  float preferredWidth() const {", "  float contentHeight() const;\n  Rect currentRect() const;\n  float preferredWidth() const {")
        text = replace_once(text, "  Sidebar* sidebar_ = nullptr;", "  ScrollArea* sidebarHost_ = nullptr;\n  Sidebar* sidebar_ = nullptr;")
    if path.name == "Shell.cpp":
        text = '#include "AxislyLayout.hpp"\n' + text
        text = replace_once(text, "  sidebar_ = add<Sidebar>();", """  sidebarHost_ = add<ScrollArea>();
  sidebarHost_->setFrameVisible(false);
  sidebar_ = sidebarHost_->content()->add<Sidebar>();""")
        text = replace_once(text, "    pg.host = pageArea_->add<ScrollArea>();",
                            "    pg.host = pageArea_->add<AxislyScrollArea>();")
        text = replace_once(text, "  pageArea_ = add<Widget>();", """  pageArea_ = add<Widget>();
  auto* shellGrid = setLayout<AxislyGridLayout>();
  shellGrid->setSpacing(0);
  shellGrid->setColumns({axisly::Track::fixed(sidebar_->preferredWidth()), axisly::Track::weighted(1)});
  shellGrid->setRows({axisly::Track::fixed(kTitleH), axisly::Track::weighted(1)});
  shellGrid->addWidget(sidebarHost_, 0, 0, 2, 1);
  shellGrid->addWidget(titleBar_, 0, 1);
  shellGrid->addWidget(pageArea_, 1, 1);""")
        text = replace_once(text, "    const Size design = pg.builder ? pg.builder(pg.host->content()) : Size{};", """    Size design = pg.builder ? pg.builder(pg.host->content()) : Size{};
    design = axisly_demo::adoptDesignedPage(*pg.host->content(), design);""")
        begin = text.index("  sidebar_->setGeometry({0.0f, 0.0f, rail, r.height()});")
        end = text.index("  const Rect pa = pageArea_->localRect();", begin)
        text = text[:begin] + """  static_cast<AxislyGridLayout*>(layout())->setColumns(
      {axisly::Track::fixed(rail), axisly::Track::weighted(1)});
  const float navHeight = std::max(sidebar_->contentHeight(), sidebarHost_->geometry().height());
  sidebar_->setGeometry({0, 0, std::max(0.0f, rail - 12), navHeight});
  sidebarHost_->setContentSize({std::max(0.0f, rail - 12), navHeight});
  sidebarHost_->ensureVisible(sidebar_->currentRect());

""" + text[end:]
        text = text.replace("  const Rect r = localRect();\n  const float rail", "  const float rail")
        text = replace_once(text, "int Sidebar::rowAtY(float y) const {", """float Sidebar::contentHeight() const { return rowTop(int(rows_.size())) + 12; }
Rect Sidebar::currentRect() const {
  for (std::size_t i = 0; i < rows_.size(); ++i)
    if (!rows_[i].isSection && rows_[i].itemIndex == current_)
      return {0, rowTop(int(i)), geometry().width(), rowHeight(rows_[i])};
  return {};
}

int Sidebar::rowAtY(float y) const {""")
    if path.name == "ShowcaseWindow.cpp":
        text = text.replace('h->setTitle("GeeyoouUI");', 'h->setTitle("axisly + GeeyoouUI");')
        text = text.replace('tr("GeeyoouUI 控件库演示")', '"axisly Showcase / GeeyoouUI"')
    if path.name == "main.cpp":
        text = '#include "Smoke.hpp"\n' + text
        text = replace_once(text, "int main() {", "int main(int argc, char** argv) {")
        text = replace_once(text, "  win.show();", "  if (!axisly_demo::enableSmoke(win, argc, argv)) win.show();")
    write_changed(dest, text)
