#include "scenes.hpp"
#include "text_painter.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
using namespace axisly;
using namespace axisly::playground;
constexpr int initial_width = 1280;
constexpr int initial_height = 840;
constexpr SDL_Color background{14, 20, 31, 255};
constexpr SDL_Color panel{22, 31, 46, 255};
constexpr SDL_Color ink{238, 244, 255, 255};
constexpr SDL_Color muted{159, 179, 203, 255};
constexpr SDL_Color border{72, 91, 115, 255};
constexpr SDL_Color accent{109, 229, 179, 255};
constexpr SDL_Color preferred_color{255, 206, 112, 255};
constexpr SDL_Color error_color{255, 145, 145, 255};
constexpr std::array<SDL_Color, 6> fills{{
    {36, 77, 102, 255}, {42, 82, 77, 255}, {66, 61, 105, 255},
    {97, 68, 49, 255}, {48, 76, 113, 255}, {93, 54, 81, 255}
}};

void require(bool ok, const char* operation) {
    if (!ok) throw std::runtime_error(std::string(operation) + ": " + SDL_GetError());
}
void verify(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
std::string number(float value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(1) << value;
    return out.str();
}
std::string size_text(Size size) { return number(size.width) + " x " + number(size.height); }
std::string rect_text(Rect r) {
    return number(r.x) + ", " + number(r.y) + "  " + size_text({r.width, r.height});
}
bool contains(SDL_FRect r, float x, float y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

class App {
public:
    explicit App(bool hidden) {
        window.reset(SDL_CreateWindow("axisly Playground", initial_width, initial_height,
            SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | (hidden ? SDL_WINDOW_HIDDEN : 0)));
        require(window != nullptr, "Create window");
        require(SDL_SetWindowMinimumSize(window.get(), 960, 720), "Set minimum window size");
        renderer.reset(SDL_CreateRenderer(window.get(), nullptr));
        require(renderer != nullptr, "Create renderer");
        text_painter = std::make_unique<TextPainter>(renderer.get());
        refresh_size();
    }

    void run() {
        while (running) {
            if (dirty) render();
            SDL_Event event;
            if (SDL_WaitEventTimeout(&event, 100)) handle(event);
            drain_events();
        }
    }

    void smoke(const std::filesystem::path& directory) {
        std::filesystem::create_directories(directory);
        std::size_t frames = 0;
        const std::array sizes{std::pair{1280, 840}, std::pair{960, 720}, std::pair{1600, 1000}};
        for (const auto& [w, h] : sizes) {
            resize(w, h);
            verify(width == w && height == h, "Resize did not reach the requested size");
            for (std::size_t i = 0; i < scenes.size(); ++i) {
                push_key(i == 9 ? SDLK_0 : SDLK_1 + static_cast<SDL_Keycode>(i));
                verify(scene == i, "Direct scene selection failed");
                render(directory / ("scene-" + std::to_string(i + 1) + "-" + std::to_string(w) + ".png"));
                verify(frame.succeeded() && !frame.items.empty(), "Scene layout failed");
                verify(frame.available.width == canvas.w && frame.available.height == canvas.h,
                    "Resize did not reach the layout container");
                ++frames;
            }
        }
        push_key(SDLK_RIGHT);
        verify(scene == 0, "Next scene did not wrap");
        push_key(SDLK_LEFT);
        verify(scene == 9, "Previous scene did not wrap");
        for (const auto key : {SDLK_G, SDLK_B, SDLK_M}) push_key(key);
        verify(!show_grid && !show_bounds && !show_metrics, "Overlay toggles failed");
        render(directory / "overlays-off.png");
        for (const auto key : {SDLK_G, SDLK_B, SDLK_M}) push_key(key);
        render();
        push_key(SDLK_DOWN);
        verify(selected == 1, "Keyboard item inspection failed");
        push_key(SDLK_UP);
        verify(selected == 0, "Keyboard item inspection failed");
        SDL_Event click{};
        click.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        click.button.windowID = SDL_GetWindowID(window.get());
        click.button.button = SDL_BUTTON_LEFT;
        click.button.x = 40;
        click.button.y = scene_button(3).y + 10;
        require(SDL_PushEvent(&click), "Queue scene click");
        drain_events();
        verify(scene == 3, "Mouse scene navigation failed");
        render();
        const auto first = frame.items.front().rect;
        SDL_Event move{};
        move.type = SDL_EVENT_MOUSE_MOTION;
        move.motion.windowID = SDL_GetWindowID(window.get());
        move.motion.x = canvas.x + first.x + first.width * 0.5f;
        move.motion.y = canvas.y + first.y + first.height * 0.5f;
        require(SDL_PushEvent(&move), "Queue hover");
        drain_events();
        verify(hovered && *hovered == 0, "Hover inspector did not resolve item geometry");
        push_key(SDLK_9);
        for (int step = 0; step < 9; ++step) push_key(SDLK_LEFTBRACKET);
        render(directory / "range-overflow.png");
        verify(frame.occupied.width > frame.available.width, "Narrow canvas did not expose minimum overflow");
        for (int step = 0; step < 9; ++step) push_key(SDLK_RIGHTBRACKET);
        verify(canvas_percent == 100, "Canvas width expansion failed");
        push_key(SDLK_R);
        require(SDL_SyncWindow(window.get()), "Wait for reset size");
        drain_events();
        refresh_size();
        verify(width == initial_width && height == initial_height, "Window reset failed");
        push_key(SDLK_4);
        render(directory / "mixed-inspector.png");
        push_key(SDLK_ESCAPE);
        verify(!running, "Escape did not exit");
        running = true;
        const auto loop_start = std::chrono::steady_clock::now();
        const auto quit_timer = SDL_AddTimer(250, +[](void*, SDL_TimerID, Uint32) -> Uint32 {
            SDL_Event quit{};
            quit.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&quit);
            return 0;
        }, nullptr);
        require(quit_timer != 0,"Schedule event loop check");
        run();
        SDL_RemoveTimer(quit_timer);
        verify(std::chrono::steady_clock::now()-loop_start >= std::chrono::milliseconds(200),
            "Interactive loop exited before receiving quit");
        std::cout << "PASS: " << frames << " scene/size renders; navigation, overlays, hover, reset, exit; renderer="
                  << SDL_GetRendererName(renderer.get()) << ", video=" << SDL_GetCurrentVideoDriver()
                  << ", font=" << text_painter->family_name() << ", file=" << text_painter->font_path() << '\n';
    }

private:
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window{nullptr, SDL_DestroyWindow};
    std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer{nullptr, SDL_DestroyRenderer};
    std::unique_ptr<TextPainter> text_painter;
    int width{}, height{};
    std::size_t scene{3}, selected{};
    std::optional<std::size_t> hovered;
    float mouse_x{-1}, mouse_y{-1};
    bool running{true}, dirty{true}, layout_dirty{true};
    bool show_grid{true}, show_bounds{true}, show_metrics{true};
    int canvas_percent{100};
    SDL_FRect canvas{};
    Frame frame;
    double layout_ms{};

    void color(SDL_Color c) {
        require(SDL_SetRenderDrawColor(renderer.get(), c.r, c.g, c.b, c.a), "Set draw color");
    }
    void fill(SDL_FRect r, SDL_Color c) {
        if (r.w <= 0 || r.h <= 0) return;
        color(c);
        require(SDL_RenderFillRect(renderer.get(), &r), "Fill rectangle");
    }
    void outline(SDL_FRect r, SDL_Color c) {
        if (r.w <= 0 || r.h <= 0) return;
        color(c);
        require(SDL_RenderRect(renderer.get(), &r), "Outline rectangle");
    }
    void clip(const SDL_FRect* r) {
        SDL_Rect pixels{};
        if (r) pixels = {static_cast<int>(std::ceil(r->x)), static_cast<int>(std::ceil(r->y)),
            std::max(0, static_cast<int>(std::floor(r->w))), std::max(0, static_cast<int>(std::floor(r->h)))};
        require(SDL_SetRenderClipRect(renderer.get(), r ? &pixels : nullptr), "Set clip");
    }
    void text(float x, float y, std::string_view value, SDL_Color c = ink, float font = 12,
              float max_width = 10000) {
        text_painter->draw(x,y,value,c,font,max_width);
    }
    SDL_FRect scene_button(std::size_t index) const {
        return {12, 122 + static_cast<float>(index) * 38, 194, 32};
    }
    SDL_FRect toggle_button(std::size_t index) const {
        return {static_cast<float>(width) - 330 + static_cast<float>(index) * 104, 22, 96, 32};
    }
    SDL_FRect translated(Rect r) const { return {canvas.x + r.x, canvas.y + r.y, r.width, r.height}; }
    SDL_FRect layout_area() const { return {canvas.x,canvas.y,frame.available.width,frame.available.height}; }

    void refresh_size() {
        require(SDL_GetWindowSize(window.get(), &width, &height), "Query window size");
        if (width <= 0 || height <= 0) return;
        // Logical window coordinates keep rendering and input aligned on high-DPI displays.
        require(SDL_SetRenderLogicalPresentation(renderer.get(), width, height,
            SDL_LOGICAL_PRESENTATION_STRETCH), "Set logical presentation");
        canvas = {244, 182, std::max(0.0f, static_cast<float>(width) - 268),
            std::max(0.0f, static_cast<float>(height) - 430)};
        dirty = layout_dirty = true;
    }
    void resize(int w, int h) {
        require(SDL_SetWindowSize(window.get(), w, h), "Resize window");
        require(SDL_SyncWindow(window.get()), "Wait for resize");
        drain_events();
        refresh_size();
    }
    void choose(std::size_t index) {
        scene = index;
        selected = 0;
        hovered.reset();
        dirty = layout_dirty = true;
    }
    void inspect_pointer() {
        hovered.reset();
        if (!contains(layout_area(), mouse_x, mouse_y)) return;
        // Descendants are drawn later and take hit-test precedence over their parents.
        for (std::size_t i = frame.items.size(); i > 0; --i) {
            if (contains(translated(frame.items[i - 1].rect), mouse_x, mouse_y)) {
                hovered = i - 1;
                break;
            }
        }
    }
    void handle(SDL_Event event) {
        if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) running = false;
        else if (event.type == SDL_EVENT_WINDOW_RESIZED || event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
                 event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED) refresh_size();
        else if (event.type == SDL_EVENT_WINDOW_EXPOSED) dirty = true;
        else if (event.type == SDL_EVENT_WINDOW_MOUSE_LEAVE) {
            mouse_x = mouse_y = -1;
            hovered.reset();
            dirty = true;
        } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
            const auto key = event.key.key;
            if (key >= SDLK_1 && key <= SDLK_9) choose(static_cast<std::size_t>(key - SDLK_1));
            else if (key == SDLK_0) choose(9);
            else if (key == SDLK_RIGHT) choose((scene + 1) % scenes.size());
            else if (key == SDLK_LEFT) choose((scene + scenes.size() - 1) % scenes.size());
            else if (key == SDLK_G) show_grid = !show_grid;
            else if (key == SDLK_B) show_bounds = !show_bounds;
            else if (key == SDLK_M) show_metrics = !show_metrics;
            else if (key == SDLK_R) {
                canvas_percent = 100;
                layout_dirty = true;
                require(SDL_SetWindowSize(window.get(), initial_width, initial_height), "Reset window");
            }
            else if (key == SDLK_LEFTBRACKET || key == SDLK_RIGHTBRACKET) {
                canvas_percent = std::clamp(canvas_percent + (key == SDLK_LEFTBRACKET ? -10 : 10),10,100);
                layout_dirty = true;
            }
            else if (key == SDLK_ESCAPE) running = false;
            else if ((key == SDLK_DOWN || key == SDLK_UP) && !frame.items.empty()) {
                selected = (selected + (key == SDLK_DOWN ? 1 : frame.items.size() - 1)) % frame.items.size();
                hovered.reset();
            }
            dirty = true;
        } else if (event.type == SDL_EVENT_MOUSE_MOTION || event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            require(SDL_ConvertEventToRenderCoordinates(renderer.get(), &event), "Convert mouse coordinates");
            const bool motion = event.type == SDL_EVENT_MOUSE_MOTION;
            mouse_x = motion ? event.motion.x : event.button.x;
            mouse_y = motion ? event.motion.y : event.button.y;
            if (!motion && event.button.button == SDL_BUTTON_LEFT) {
                for (std::size_t i = 0; i < scenes.size(); ++i)
                    if (contains(scene_button(i), mouse_x, mouse_y)) choose(i);
                for (std::size_t i = 0; i < 3; ++i) {
                    if (!contains(toggle_button(i), mouse_x, mouse_y)) continue;
                    if (i == 0) show_grid = !show_grid;
                    if (i == 1) show_bounds = !show_bounds;
                    if (i == 2) show_metrics = !show_metrics;
                }
            }
            if (!layout_dirty) inspect_pointer();
            dirty = true;
        }
    }
    void drain_events() { SDL_Event event; while (SDL_PollEvent(&event)) handle(event); }
    void push_key(SDL_Keycode key) {
        SDL_Event event{};
        event.type = SDL_EVENT_KEY_DOWN;
        event.key.windowID = SDL_GetWindowID(window.get());
        event.key.key = key;
        require(SDL_PushEvent(&event), "Queue key");
        drain_events();
    }
    void draw_chrome() {
        const float w = static_cast<float>(width), h = static_cast<float>(height);
        fill({0,0,w,76}, panel);
        text(24,22,"axisly",ink,32);
        text(242,30,"LAYOUT PLAYGROUND",muted,14);
        const std::array values{show_grid, show_bounds, show_metrics};
        const std::array labels{"G GRID", "B BOUNDS", "M METRICS"};
        for (std::size_t i = 0; i < values.size(); ++i) {
            const auto r = toggle_button(i);
            fill(r, values[i] ? SDL_Color{33,69,61,255} : background);
            outline(r, contains(r, mouse_x, mouse_y) ? ink : (values[i] ? accent : border));
            text(r.x+8,r.y+10,labels[i],values[i] ? accent : muted,10);
        }
        fill({0,77,218,h-77}, panel);
        text(24,96,"SCENES / 10",muted,12);
        for (std::size_t i = 0; i < scenes.size(); ++i) {
            const auto r = scene_button(i);
            if (i == scene) { fill(r,{34,58,64,255}); fill({r.x,r.y,3,r.h},accent); }
            else if (contains(r,mouse_x,mouse_y)) fill(r,{36,47,65,255});
            text(24,r.y+10,std::to_string((i+1)%10), i == scene ? accent : muted,12);
            text(42,r.y+10,scenes[i].title,i == scene ? ink : muted,10,162);
        }
        const float help_y = std::max(520.0f,h-210);
        text(24,help_y,"KEYBOARD",accent,12);
        const std::array help{"1..0  Select scene", "< >   Prev / next", "Up/Dn Inspect item", "G B M Overlays", "[ ]   Canvas width", "R     Reset size", "Esc   Exit"};
        for (std::size_t i = 0; i < help.size(); ++i) text(24,help_y+28+static_cast<float>(i)*20,help[i],muted,10);
        text(24,help_y+175,"Canvas: "+std::to_string(canvas_percent)+"%",accent,10);
        text(244,96,std::to_string(scene+1)+" / "+std::string(scenes[scene].title),ink,24,w-268);
        text(244,132,scenes[scene].description,muted,12,w-268);
        text(244,157,scenes[scene].hint,accent,10,w-268);
        fill(canvas,{11,17,27,255});
        outline(canvas,border);
        if (canvas_percent < 100) outline(layout_area(),accent);
    }
    void draw_items() {
        const auto area = layout_area();
        clip(&area);
        const auto current = hovered.value_or(selected);
        for (std::size_t i = 0; i < frame.items.size(); ++i) {
            const auto& item = frame.items[i];
            const auto r = translated(item.rect);
            fill(r, fills[(i+item.depth) % fills.size()]);
            if (show_bounds) outline(r,muted);
            if (show_grid) outline(translated(item.cell),border);
            if (show_metrics && item.has_preferred && item.preferred.width > 0 && item.preferred.height > 0) {
                // Preferred footprint is deliberately clipped to the allocated item.
                // The inspector retains the full, unclipped preferred dimensions.
                outline({r.x+2,r.y+2,std::max(0.0f,std::min(item.preferred.width,r.w)-4),
                    std::max(0.0f,std::min(item.preferred.height,r.h)-4)},preferred_color);
            }
            if (show_metrics && r.w >= 24 && r.h >= 24) {
                text(r.x+8,r.y+10,item.label,ink,12,r.w-16);
                if (r.h >= 62 && item.has_preferred) text(r.x+8,r.y+34,size_text({r.w,r.h}),muted,10,r.w-16);
            }
        }
        if (current < frame.items.size()) outline(translated(frame.items[current].rect),accent);
        clip(nullptr);
    }
    void draw_inspector() {
        const float w = static_cast<float>(width), y = static_cast<float>(height) - 228;
        const float split = 244 + canvas.w * 0.5f;
        const float half = canvas.w * 0.5f - 20;
        fill({244,y,canvas.w,202},panel);
        text(256,y+12,"COLUMNS / ROWS",accent,12);
        text(split+8,y+12,"ITEM INSPECTOR",accent,12);
        if (!frame.succeeded()) { text(256,y+44,frame.error,error_color,12,w-292); return; }
        if (!show_metrics) { text(256,y+48,"Metrics hidden. Press M to show.",muted,12,w-292); return; }
        for (std::size_t i = 0; i < frame.columns.size(); ++i) {
            text(256,y+40+static_cast<float>(i)*22,"C"+std::to_string(i)+"  "+frame.column_rules[i]+"  = "+number(frame.columns[i]),ink,10,half);
        }
        std::string rows = "Rows: ";
        for (std::size_t i = 0; i < frame.rows.size(); ++i) {
            if (i) rows += ", ";
            rows += number(frame.rows[i]);
        }
        text(256,y+134,rows,muted,10,half);
        text(256,y+157,"cell / item / preferred*",muted,10,half);
        text(256,y+178,"*clipped to allocated item",preferred_color,10,half);
        const auto current = hovered.value_or(selected);
        if (current < frame.items.size()) {
            const auto& item = frame.items[current];
            const auto& input = item.input;
            const float x = split+8;
            text(x,y+40,"ID "+item.path+" / "+item.label,ink,12,half);
            text(x,y+66,"row="+std::to_string(input.row_start)+" col="+std::to_string(input.column_start)+
                " span="+std::to_string(input.column_end-input.column_start)+"x"+std::to_string(input.row_end-input.row_start),muted,10,half);
            text(x,y+90,"cell "+rect_text(item.cell),muted,10,half);
            text(x,y+112,"item "+rect_text(item.rect),ink,10,half);
            text(x,y+134,item.has_preferred ? "preferred "+size_text(item.preferred) : "preferred: host child probes",preferred_color,10,half);
            text(x,y+164,"Hover / Up / Down to inspect",muted,10,half);
        }
        const bool overflow = frame.occupied.width > frame.available.width+0.01f || frame.occupied.height > frame.available.height+0.01f;
        std::ostringstream status;
        status << size_text(frame.available) << " -> " << size_text(frame.occupied)
               << "  " << std::fixed << std::setprecision(3) << layout_ms << " ms"
               << "  M:" << frame.measurements << " L:" << frame.layouts;
        if (overflow) status << "  CLIPPED";
        text(244,static_cast<float>(height)-22,status.str(),overflow ? preferred_color : muted,10,w-260);
    }
    void render(const std::filesystem::path& capture = {}) {
        if (layout_dirty) {
            const auto start = std::chrono::steady_clock::now();
            frame = arrange_scene(scene,{canvas.w*static_cast<float>(canvas_percent)/100,canvas.h});
            layout_ms = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            layout_dirty = false;
            inspect_pointer();
        }
        color(background);
        require(SDL_RenderClear(renderer.get()), "Clear renderer");
        int pixel_width{}, pixel_height{};
        require(SDL_GetRenderOutputSize(renderer.get(),&pixel_width,&pixel_height),"Query pixel density");
        text_painter->begin_frame(std::max(static_cast<float>(pixel_width)/static_cast<float>(width),
            static_cast<float>(pixel_height)/static_cast<float>(height)));
        draw_chrome();
        draw_items();
        draw_inspector();
        if (!capture.empty()) {
            std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> surface(
                SDL_RenderReadPixels(renderer.get(),nullptr), SDL_DestroySurface);
            require(surface != nullptr,"Capture frame");
            const auto path = capture.u8string();
            require(SDL_SavePNG(surface.get(),reinterpret_cast<const char*>(path.c_str())),"Save frame");
        }
        require(SDL_RenderPresent(renderer.get()),"Present frame");
        dirty = false;
    }
};
} // namespace

int main(int argc, char** argv) {
    const bool smoke = argc == 3 && std::string_view(argv[1]) == "--smoke-test";
    if (argc != 1 && !smoke) {
        SDL_Log("Usage: axisly_playground [--smoke-test capture-directory]");
        return 2;
    }
    try {
        require(SDL_Init(SDL_INIT_VIDEO), "Initialize SDL video");
        struct Session { ~Session() { SDL_Quit(); } } session;
        require(TTF_Init(),"Initialize font renderer");
        struct FontSession { ~FontSession() { TTF_Quit(); } } font_session;
        App app(smoke);
        if (smoke) {
            const std::string path(argv[2]);
            app.smoke(std::filesystem::path(std::u8string(path.begin(),path.end())));
        }
        else app.run();
        return 0;
    } catch (const std::exception& error) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"%s",error.what());
        // Double-click failures stay visible instead of looking like a silent crash.
        if (!smoke) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"axisly Playground",error.what(),nullptr);
        SDL_Quit();
        return 1;
    }
}
