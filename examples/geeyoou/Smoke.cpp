#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "Smoke.hpp"
#include "AxislyLayout.hpp"
#include "CssHostPage.hpp"
#include "CssHostAcceptance.hpp"
#include "ShowcaseWindow.hpp"
#include "i18n/I18n.hpp"
#include "geeyoou/render/Canvas.hpp"
#include "geeyoou/render/Offscreen.hpp"
#include "geeyoou/render/Skin.hpp"
#include "geeyoou/widget/Label.hpp"
#include "geeyoou/widget/LineEdit.hpp"
#include "geeyoou/widget/PushButton.hpp"
#include "geeyoou/widget/ScrollArea.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace axisly_demo {
namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
struct Inspection { std::size_t widgets{}, layouts{}, visible{}; };
void inspect(const geeyoou::Widget& widget, Inspection& count) {
    ++count.widgets;
    const auto r = widget.geometry();
    check(std::isfinite(r.x()) && std::isfinite(r.y()) && std::isfinite(r.width()) &&
          std::isfinite(r.height()) && r.width() >= 0 && r.height() >= 0, "Invalid widget geometry");
    if (dynamic_cast<const AxislyLayout*>(widget.layout())) ++count.layouts;
    if (widget.isVisible() && !r.isEmpty()) ++count.visible;
    for (const auto& child : widget.children()) inspect(*child, count);
}
geeyoou::ScrollArea* activePage(showcase::ShowcaseWindow& win) {
    // Shell children are sidebar / title / page area in the pinned source.
    for (const auto& child : win.shell()->children().at(2)->children())
        if (auto* area = dynamic_cast<geeyoou::ScrollArea*>(child.get()); area && area->isVisible()) return area;
    return nullptr;
}
template<class T> T* find(geeyoou::Widget& root) {
    if (auto* p = dynamic_cast<T*>(&root)) return p;
    for (const auto& child : root.children()) if (auto* p = find<T>(*child)) return p;
    return nullptr;
}
geeyoou::PushButton* button(geeyoou::Widget& root, const std::string& caption) {
    if (auto* p = dynamic_cast<geeyoou::PushButton*>(&root); p && p->text() == caption) return p;
    for (const auto& child : root.children()) if (auto* p = button(*child, caption)) return p;
    return nullptr;
}
void render(showcase::ShowcaseWindow& win, const std::filesystem::path& path) {
    const float scale = win.scaleFactor();
    const auto rect = win.localRect();
    geeyoou::OffscreenImage image(int(std::ceil(rect.width()*scale)), int(std::ceil(rect.height()*scale)), scale);
    geeyoou::Canvas canvas;
    check(canvas.begin(image.surface(), {0, 0, float(image.width()), float(image.height())}), "Canvas begin failed");
    auto painter = canvas.painter();
    painter.fillRect(rect, geeyoou::Theme::current().background);
    win.paintTree(painter, rect, rect);
    canvas.end();
    check(geeyoou::writePng(image, path.string().c_str()), "Screenshot export failed");
}
void renderCssHost(geeyoou::Widget& host, float scale,
                   const std::filesystem::path& path) {
    const auto local = host.localRect();
    geeyoou::OffscreenImage image(int(std::ceil(local.width() * scale)),
        int(std::ceil(local.height() * scale)), scale);
    geeyoou::Canvas canvas;
    check(canvas.begin(image.surface(),
        {0, 0, float(image.width()), float(image.height())}),
        "CSS host Canvas begin failed");
    auto painter = canvas.painter();
    painter.fillRect(local, geeyoou::Theme::current().background);
    const auto window_rect = host.windowRect();
    painter.translate(-window_rect.x(), -window_rect.y());
    host.paintTree(painter, window_rect, window_rect);
    canvas.end();
    check(geeyoou::writePng(image, path.string().c_str()),
        "CSS host screenshot export failed");
}
void validateCssWindow(showcase::ShowcaseWindow& win,
                       const std::filesystem::path& screenshot) {
    auto* host = win.add<geeyoou::Widget>();
    const auto page = buildCssHostPage(*host);
    host->setGeometry({24, 32, 430, 220});
    check(page.side->geometry().x() >= 210,
        "Native CSS Grid did not place the Block panel in the second column");
    const auto short_height = page.wrapped->geometry().height();
    page.wrapped->setText("Long text 中文换行 testing a live window and the host's actual font. "
        "The CSS Grid, Flex and Block tree must respond to the narrower width.");
    host->setGeometry({24, 32, 260, 220});
    check(page.wrapped->geometry().height() > short_height,
        "Native CSS host did not remeasure wrapped text");
    check(page.tail->geometry().y() >= page.wrapped->geometry().bottom(),
        "Native CSS Flex label overlaps wrapped text");
    const float scale = win.scaleFactor();
    const float physical = page.side->geometry().x() * scale;
    check(std::abs(physical - std::round(physical)) < .02f,
        "Native CSS panel edge is not aligned to the display DPI");
    renderCssHost(*host, scale, screenshot);
    win.removeChild(host);
}
HWND ownWindow() {
    HWND result{};
    EnumWindows([](HWND w, LPARAM value) -> BOOL {
        DWORD pid{};
        GetWindowThreadProcessId(w, &pid);
        if (pid != GetCurrentProcessId() || GetWindow(w, GW_OWNER)) return TRUE;
        wchar_t name[128]{}; GetWindowTextW(w, name, 128);
        if (std::wstring_view(name) != L"axisly Showcase / GeeyoouUI") return TRUE;
        *reinterpret_cast<HWND*>(value) = w;
        return FALSE;
    }, reinterpret_cast<LPARAM>(&result));
    return result;
}
struct NativeMonitor {
    HMONITOR handle{};
    RECT work{};
    std::wstring device;
};
std::vector<NativeMonitor> nativeMonitors() {
    struct Inventory { std::vector<NativeMonitor> monitors; bool ok{true}; } state;
    const auto success = EnumDisplayMonitors(nullptr, nullptr,
        [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
            auto& inventory = *reinterpret_cast<Inventory*>(data);
            MONITORINFOEXW info{};
            info.cbSize = sizeof(info);
            if (!GetMonitorInfoW(monitor, &info)) { inventory.ok = false; return FALSE; }
            try { inventory.monitors.push_back({monitor, info.rcWork, info.szDevice}); }
            catch (...) { inventory.ok = false; return FALSE; }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&state));
    check(success && state.ok && !state.monitors.empty(), "Monitor inventory failed");
    return state.monitors;
}
struct MonitorResult { std::string device; UINT dpi{}; float scale{}; };
std::string jsonDevice(const std::wstring& source) {
    // Windows display device names are ASCII; escape their path separators.
    std::string result;
    for (const auto c : source) {
        check(c >= 32 && c < 127, "Unexpected display device name");
        if (c == L'\\' || c == L'"') result += '\\';
        result += static_cast<char>(c);
    }
    return result;
}
struct Run {
    showcase::ShowcaseWindow& window;
    std::filesystem::path directory;
    std::ofstream report;
    geeyoou::TimerId timer{};
    int step{};
    std::vector<NativeMonitor> monitors;
    std::vector<MonitorResult> monitor_results;
    explicit Run(showcase::ShowcaseWindow& win, std::filesystem::path path)
        : window(win), directory(std::move(path)), monitors(nativeMonitors()) {
        std::filesystem::create_directories(directory);
        report.open(directory / "pages.csv");
        check(bool(report), "Cannot open acceptance report");
        report << "language,skin,page,width,height,widgets,layouts,visible\n";
        std::ofstream(directory / "summary.json") << "{\"status\":\"running\"}\n";
    }
    void tick() {
        try {
            constexpr int pages = 19;
            const int round = step / pages, page = step % pages;
            if (step >= 3 * pages) {
                const auto stage = static_cast<std::size_t>(step - 3 * pages);
                const auto index = stage / 3;
                if (index < monitors.size()) {
                    const auto native = ownWindow();
                    check(native != nullptr, "Native window not found during DPI acceptance");
                    const auto& monitor = monitors[index];
                    if (stage % 3 == 0) {
                        const auto& r = monitor.work;
                        check(SetWindowPos(native, nullptr, r.left + 16, r.top + 16,
                            std::min<LONG>(1100, r.right - r.left - 32),
                            std::min<LONG>(800, r.bottom - r.top - 32),
                            SWP_NOZORDER | SWP_NOACTIVATE), "Cross-monitor move failed");
                    } else if (stage % 3 == 2) {
                        window.animationTickTree();
                        check(MonitorFromWindow(native, MONITOR_DEFAULTTONEAREST) == monitor.handle,
                            "Window did not reach the requested monitor");
                        const UINT dpi = GetDpiForWindow(native);
                        const float scale = window.scaleFactor();
                        check(dpi > 0 && std::abs(scale - float(dpi) / 96.f) < .001f,
                            "Host scale differs from native monitor DPI");
                        validateCssWindow(window, directory / ("monitor-" + std::to_string(index) + "-css-mixed.png"));
                        for (auto feature : {CssFeature::Flow, CssFeature::Flex, CssFeature::Grid}) {
                            auto* host = window.add<geeyoou::Widget>();
                            const auto feature_page = buildCssFeaturePage(*host, feature);
                            exerciseCssFeaturePage(*host, feature_page, scale);
                            renderCssHost(*host, scale, directory / ("monitor-" + std::to_string(index)
                                + "-css-" + cssFeatureName(feature) + ".png"));
                            window.removeChild(host);
                        }
                        monitor_results.push_back({jsonDevice(monitor.device), dpi, scale});
                    }
                    ++step;
                    return;
                }
                const auto c = counters();
                check(c.errors == 0, "axisly solve failed during Showcase run");
                check(c.solves > 0 && c.cacheHits > 0, "Showcase never exercised axisly/cache");
                std::ofstream summary(directory / "summary.json");
                summary << "{\"status\":\"passed\",\"pages\":19,\"visits\":57,\"native_dpi_scale\":" << window.scaleFactor()
                    << ",\"solves\":" << c.solves << ",\"cache_hits\":" << c.cacheHits
                    << ",\"widget_measures\":" << c.widgetMeasures << ",\"wrapped_measures\":" << c.wrappedMeasures
                    << ",\"css_mixed_native\":\"passed\""
                    << ",\"css_feature_pages\":3,\"monitor_visits\":" << monitor_results.size()
                    << ",\"monitors\":[";
                for (std::size_t i = 0; i < monitor_results.size(); ++i) {
                    const auto& monitor = monitor_results[i];
                    if (i) summary << ',';
                    summary << "{\"device\":\"" << monitor.device << "\",\"dpi\":" << monitor.dpi
                        << ",\"scale\":" << monitor.scale << ",\"status\":\"passed\"}";
                }
                summary << "],\"aborted_passes\":" << c.aborted << ",\"errors\":" << c.errors << "}\n";
                report.close();
                std::puts("Showcase: 19 pages x 3 configurations and four CSS pages on every connected monitor passed");
                geeyoou::platform().stopTimer(timer);
                geeyoou::platform().quit(0);
                return;
            }
            if (page == 0) {
                showcase::setLang(round == 0 ? 0 : 1);
                check(geeyoou::skins().apply(round == 2 ? "light" : "dark"), "Skin switch failed");
                const int widths[] = {1320, 1024, 1600};
                const int heights[] = {860, 720, 1000};
                const auto native = ownWindow(); check(native != nullptr, "Native window not found");
                const float scale = window.scaleFactor();
                check(SetWindowPos(native, nullptr, 0, 0, int(float(widths[round])*scale), int(float(heights[round])*scale),
                    SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != FALSE, "Native window resize failed");
            }
            window.shell()->showPage(page);
            auto* area = activePage(window);
            check(area && area->content(), "Showcase page missing");
            auto* content = area->content();
            check(dynamic_cast<AxislyLayout*>(content->layout()) != nullptr, "Page did not adopt axisly");
            Inspection count;
            inspect(*content, count);
            check(count.widgets > 1 && count.visible > 1 && count.layouts > 0, "Empty Showcase page");
            // Exercise actual handlers/data setters with a live native root.
            if (page == 6) {
                auto press = [&](const char* zh, const char* en) {
                    auto* target = button(*content, showcase::lang() == 0 ? zh : en);
                    check(target != nullptr, "Dynamic test button missing");
                    window.setFocusWidget(target);
                    const HWND native = ownWindow();
                    SendMessageW(native, WM_KEYDOWN, VK_SPACE, 0);
                    SendMessageW(native, WM_KEYUP, VK_SPACE, 0);
                    window.setFocusWidget(nullptr);
                };
                press("增加一项", "Add item");
                Inspection added; inspect(*content, added);
                check(added.widgets == count.widgets + 1, "Add button did not add a widget");
                press("移除末项", "Remove last");
                Inspection removed; inspect(*content, removed);
                check(removed.widgets == count.widgets, "Remove button did not remove its widget");
                press("修改文字", "Update text");
                window.animationTickTree();
                press("隐藏 / 显示", "Hide / show");
                Inspection hidden; inspect(*content, hidden);
                check(hidden.visible + 1 == count.visible, "Hide button did not hide its label");
                press("隐藏 / 显示", "Hide / show");
            }
            if (page == 8) {
                auto* input = find<geeyoou::LineEdit>(*content);
                check(input != nullptr, "Input page has no editor");
                input->setText(""); window.setFocusWidget(input);
                SendMessageW(ownWindow(), WM_CHAR, L'A', 0);
                check(input->text() == "A", "Native text input did not reach the editor");
                input->setText("axisly resize / 中文输入 123");
                window.setFocusWidget(nullptr);
            }
            area->scrollTo({0, 120}); area->scrollTo({0, 0});
            window.animationTickTree();
            if (page == 6) check(content->geometry().width() <= area->geometry().width() + 1, "Layout demo failed to fit viewport");
            if (page == 0 || page == 6 || page == 11 || page == 18)
                render(window, directory / ("round-" + std::to_string(round) + "-page-" + std::to_string(page) + ".png"));
            report << showcase::langId(showcase::lang()) << ',' << geeyoou::skins().currentName() << ',' << page << ','
                << window.geometry().width() << ',' << window.geometry().height() << ',' << count.widgets << ','
                << count.layouts << ',' << count.visible << '\n';
            report.flush();
            ++step;
        } catch (const std::exception& e) {
            std::fprintf(stderr, "Showcase step %d failed: %s\n", step, e.what());
            geeyoou::platform().stopTimer(timer);
            geeyoou::platform().quit(1);
        }
    }
};
}
bool enableSmoke(showcase::ShowcaseWindow& window, int argc, char** argv) {
    if (argc != 3 || std::string_view(argv[1]) != "--smoke") return false;
    resetCounters();
    auto run = std::make_shared<Run>(window, std::filesystem::path(argv[2]));
    run->timer = geeyoou::platform().startTimer(60, [run] { run->tick(); });
    return true;
}
} // namespace axisly_demo
