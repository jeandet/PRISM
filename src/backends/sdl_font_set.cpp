#include <prism/backends/sdl_font_set.hpp>

#include <SDL3/SDL.h>
#include <cstdint>
#include <cstring>

#include <SDL3_ttf/SDL_ttf.h>

#include <cmath>
#include <string_view>

namespace prism::backends {

bool SdlFontSet::open(const char* ui_path, const char* mono_path, float size) {
    close();
    if (ui_path) ui_ = TTF_OpenFont(ui_path, size);
    bool same_file = ui_path && mono_path && std::string_view{ui_path} == mono_path;
    if (mono_path && !same_file) mono_ = TTF_OpenFont(mono_path, size);   // else ui_ serves both
    // Subpixel positioning: glyph advances keep their fraction instead of being rounded to whole
    // pixels at each size. The renderer rasterizes at size x device scale while widgets measure
    // at the point size; with whole-pixel advances the two disagreed by ~6% at 2x.
    for (TTF_Font* f : {ui_, mono_})
        if (f) TTF_SetFontHinting(f, TTF_HINTING_LIGHT_SUBPIXEL);
    if (ui_ && mono_) {
        // Mutual fallbacks are fine: SDL_ttf stops the glyph search when it returns to the start.
        TTF_AddFallbackFont(ui_, mono_);
        TTF_AddFallbackFont(mono_, ui_);
    }
    size_ = size;
    return ui_ || mono_;
}

void SdlFontSet::close() {
    if (TTF_WasInit() > 0) {
        if (ui_) TTF_CloseFont(ui_);
        if (mono_) TTF_CloseFont(mono_);
    }
    ui_ = mono_ = nullptr;
}

TTF_Font* SdlFontSet::get(render::FontRole role, float px_size) {
    TTF_Font* wanted = role == render::FontRole::Mono ? mono_ : ui_;
    TTF_Font* font = wanted ? wanted : (ui_ ? ui_ : mono_);
    if (font && std::abs(size_ - px_size) > 0.01f) {
        if (ui_) TTF_SetFontSize(ui_, px_size);      // both: each is the other's fallback
        if (mono_) TTF_SetFontSize(mono_, px_size);
        size_ = px_size;
    }
    return font;
}

std::optional<render::TextBitmap> SdlFontSet::rasterize(std::string_view text, float px_size, render::FontRole role) {
    TTF_Font* font = get(role, px_size);
    if (!font || text.empty()) return std::nullopt;
    SDL_Surface* raw = TTF_RenderText_Blended(font, text.data(), text.size(), SDL_Color{255, 255, 255, 255});
    if (!raw) return std::nullopt;
    SDL_Surface* rgba = SDL_ConvertSurface(raw, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(raw);
    if (!rgba) return std::nullopt;
    render::TextBitmap out{rgba->w, rgba->h, {}};
    out.rgba.resize(static_cast<size_t>(rgba->w) * rgba->h * 4);
    for (int y = 0; y < rgba->h; ++y)
        std::memcpy(&out.rgba[static_cast<size_t>(y) * rgba->w * 4],
                    static_cast<const std::uint8_t*>(rgba->pixels) + static_cast<size_t>(y) * rgba->pitch,
                    static_cast<size_t>(rgba->w) * 4);
    SDL_DestroySurface(rgba);
    return out;
}

} // namespace prism::backends
