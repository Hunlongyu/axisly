#pragma once

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace gridly::playground {

// SDL-only host utility. Fonts are read from the machine, never copied into the
// core package. The explicit override is a UTF-8 file path in GRIDLY_FONT.
class TextPainter {
public:
    explicit TextPainter(SDL_Renderer* renderer);
    void begin_frame(float pixel_scale);
    void draw(float x, float y, std::string_view value, SDL_Color color, float size, float max_width);
    [[nodiscard]] const std::string& font_path() const noexcept { return path_; }
    [[nodiscard]] std::string family_name();
private:
    using Font = std::unique_ptr<TTF_Font, decltype(&TTF_CloseFont)>;
    using Texture = std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)>;
    struct Label {
        Texture texture{nullptr, SDL_DestroyTexture};
        float width{}, height{};
    };
    TTF_Font* font(int pixels);
    SDL_Renderer* renderer_{};
    std::string path_;
    float scale_{1};
    std::map<int, Font> fonts_;
    std::unordered_map<std::string, Label> labels_;
};
} // namespace gridly::playground
