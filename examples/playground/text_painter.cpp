#include "text_painter.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <vector>

namespace axisly::playground {
namespace {
std::string utf8(const std::filesystem::path& path) {
    const auto bytes = path.u8string();
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}
std::filesystem::path from_utf8(const char* value) {
    const std::string bytes(value);
    return std::filesystem::path(std::u8string(bytes.begin(), bytes.end()));
}
std::string find_font() {
    if (const char* specified = SDL_getenv("AXISLY_FONT"); specified && *specified) {
        if (!std::filesystem::is_regular_file(from_utf8(specified)))
            throw std::runtime_error("AXISLY_FONT does not name a readable font file: " + std::string(specified));
        return specified;
    }
    std::vector<std::filesystem::path> roots;
    if (const char* local = SDL_getenv("LOCALAPPDATA")) roots.push_back(from_utf8(local) / "Microsoft/Windows/Fonts");
    // SDL's cached environment keys can retain the original Windows casing.
    const char* system = SDL_getenv("WINDIR");
    if (!system) system = SDL_getenv("windir");
    if (!system) system = SDL_getenv("SystemRoot");
    if (system) roots.push_back(from_utf8(system) / "Fonts");
    // Known regular mono font files, in preference order. The renderer remains
    // portable without embedding personal font files or assuming one username.
    // Match VS Code's default editor font on Windows: Consolas.
    for (const char* name : {"consola.ttf", "CascadiaMono.ttf", "CascadiaCode.ttf",
                             "FiraCode-Regular.ttf", "SourceCodePro-Medium.ttf", "MapleMonoNormal-NF-CN-Regular.ttf"}) {
        for (const auto& root : roots)
            if (std::filesystem::is_regular_file(root / name)) return utf8(root / name);
    }
    for (const char* path : {"/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
                            "/usr/share/fonts/truetype/liberation2/LiberationMono-Regular.ttf",
                            "/System/Library/Fonts/Menlo.ttc", "/System/Library/Fonts/Monaco.ttf"}) {
        if (std::filesystem::is_regular_file(path)) return path;
    }
    throw std::runtime_error("No monospace font found. Set AXISLY_FONT to a .ttf or .otf font file.");
}
void require(bool ok, const char* action) {
    if (!ok) throw std::runtime_error(std::string(action) + ": " + SDL_GetError());
}
}

TextPainter::TextPainter(SDL_Renderer* renderer) : renderer_(renderer), path_(find_font()) {
    (void)font(16); // Fail visibly at startup, before entering the event loop.
}
TTF_Font* TextPainter::font(int pixels) {
    const auto existing = fonts_.find(pixels);
    if (existing != fonts_.end()) return existing->second.get();
    Font opened(TTF_OpenFont(path_.c_str(), static_cast<float>(pixels)), TTF_CloseFont);
    require(opened != nullptr, "Open monospace font");
    return fonts_.emplace(pixels, std::move(opened)).first->second.get();
}
std::string TextPainter::family_name() {
    const char* family = TTF_GetFontFamilyName(font(16));
    return family ? family : "Monospace";
}
void TextPainter::begin_frame(float pixel_scale) {
    const float next = std::max(1.0f, pixel_scale);
    if (std::abs(next-scale_) > 0.01f) {
        labels_.clear();
        fonts_.clear();
        scale_ = next;
    }
    // Cached strings include timing and coordinates. Bound memory across resizes.
    if (labels_.size() > 512) labels_.clear();
}
void TextPainter::draw(float x, float y, std::string_view value, SDL_Color color, float size, float max_width) {
    if (value.empty() || max_width <= 0) return;
    // Existing layout tokens map to readable antialiased font heights.
    const float logical_size = size <= 14 ? size+3 : size;
    const int pixels = static_cast<int>(std::ceil(logical_size*scale_));
    auto* face = font(pixels);
    std::string line(value);
    int width{}, height{};
    require(TTF_GetStringSize(face,line.c_str(),line.size(),&width,&height), "Measure text");
    if (static_cast<float>(width) / scale_ > max_width) {
        // The demo's labels are ASCII. Trim at UTF-8 boundaries as well for a
        // system font's family name, which is not necessarily ASCII.
        while (!line.empty()) {
            auto last = line.size()-1;
            while (last > 0 && (static_cast<unsigned char>(line[last]) & 0xc0) == 0x80) --last;
            line.resize(last);
            const auto candidate = line + "...";
            require(TTF_GetStringSize(face,candidate.c_str(),candidate.size(),&width,&height), "Measure ellipsis");
            if (static_cast<float>(width)/scale_ <= max_width) { line = candidate; break; }
        }
        if (line.empty()) return;
    }
    const auto key = std::to_string(pixels) + "/" + std::to_string(color.r) + "/" +
        std::to_string(color.g) + "/" + std::to_string(color.b) + "/" + std::to_string(color.a) + "/" + line;
    auto found = labels_.find(key);
    if (found == labels_.end()) {
        std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> surface(
            TTF_RenderText_Blended(face,line.c_str(),line.size(),color), SDL_DestroySurface);
        require(surface != nullptr,"Rasterize text");
        Label label;
        label.texture.reset(SDL_CreateTextureFromSurface(renderer_,surface.get()));
        require(label.texture != nullptr,"Create text texture");
        require(SDL_SetTextureScaleMode(label.texture.get(),SDL_SCALEMODE_LINEAR),"Set font filtering");
        label.width = static_cast<float>(surface->w)/scale_;
        label.height = static_cast<float>(surface->h)/scale_;
        found = labels_.emplace(key,std::move(label)).first;
    }
    const auto& label = found->second;
    const SDL_FRect destination{x,y,label.width,label.height};
    require(SDL_RenderTexture(renderer_,label.texture.get(),nullptr,&destination),"Draw text");
}
} // namespace axisly::playground
