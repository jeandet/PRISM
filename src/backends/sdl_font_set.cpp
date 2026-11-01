#include <prism/backends/sdl_font_set.hpp>

#include <SDL3_ttf/SDL_ttf.h>

#include <cmath>
#include <string_view>

namespace prism::backends {

bool SdlFontSet::open(const char* ui_path, const char* mono_path, float size) {
    close();
    if (ui_path) ui_ = TTF_OpenFont(ui_path, size);
    bool same_file = ui_path && mono_path && std::string_view{ui_path} == mono_path;
    if (mono_path && !same_file) mono_ = TTF_OpenFont(mono_path, size);   // else ui_ serves both
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

} // namespace prism::backends
