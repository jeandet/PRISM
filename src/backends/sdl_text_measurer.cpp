#include <prism/backends/sdl_text_measurer.hpp>

#include <SDL3_ttf/SDL_ttf.h>

#include <cstring>

namespace prism::backends {

SdlTextMeasurer::~SdlTextMeasurer() {
    // After TTF_Quit() closing would crash; leaking one font beats that on odd teardown orders.
    if (font_ && TTF_WasInit() > 0) TTF_CloseFont(font_);
}

TTF_Font* SdlTextMeasurer::font(float size) const {
    if (!font_ && !open_failed_ && TTF_WasInit() > 0) {   // never latch a failure from before TTF_Init
        font_ = TTF_OpenFont(font_path_.c_str(), size);
        open_failed_ = !font_;
        font_size_ = size;
    }
    if (font_ && font_size_ != size) {
        TTF_SetFontSize(font_, size);
        font_size_ = size;
    }
    return font_;
}

float SdlTextMeasurer::width(std::string_view utf8, float size) const {
    if (utf8.empty()) return 0.f;
    std::string key(sizeof size, '\0');
    std::memcpy(key.data(), &size, sizeof size);
    key.append(utf8);
    if (auto it = cache_.find(key); it != cache_.end()) return it->second;

    auto* f = font(size);
    int w = 0, h = 0;
    if (!f || !TTF_GetStringSize(f, utf8.data(), utf8.size(), &w, &h))
        return ui::text_width(nullptr, utf8, size);   // not cached: the font may open later
    if (cache_.size() >= cache_cap) cache_.clear();
    cache_.emplace(std::move(key), static_cast<float>(w));
    return static_cast<float>(w);
}

} // namespace prism::backends
