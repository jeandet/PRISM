#include <prism/backends/sdl_text_measurer.hpp>

#include <SDL3_ttf/SDL_ttf.h>

#include <cstring>

namespace prism::backends {

TTF_Font* SdlTextMeasurer::font(float size, ui::FontRole role) const {
    if (!opened_ && TTF_WasInit() > 0) {   // never latch a failure from before TTF_Init
        fonts_.open(ui_path_.empty() ? nullptr : ui_path_.c_str(),
                    mono_path_.empty() ? nullptr : mono_path_.c_str(), size);
        opened_ = true;
    }
    return opened_ ? fonts_.get(role, size) : nullptr;
}

float SdlTextMeasurer::line_height(float size, ui::FontRole role) const {
    TTF_Font* f = font(size, role);
    return f ? static_cast<float>(TTF_GetFontLineSkip(f)) : ui::TextMeasurer::line_height(size, role);
}

float SdlTextMeasurer::width(std::string_view utf8, float size, ui::FontRole role) const {
    if (utf8.empty()) return 0.f;
    std::string key(1 + sizeof size, '\0');
    key[0] = static_cast<char>(role);
    std::memcpy(key.data() + 1, &size, sizeof size);
    key.append(utf8);
    if (auto it = cache_.find(key); it != cache_.end()) return it->second;

    TTF_Font* f = font(size, role);
    int w = 0, h = 0;
    if (!f || !TTF_GetStringSize(f, utf8.data(), utf8.size(), &w, &h))
        return ui::text_width(nullptr, utf8, size, role);   // not cached: before TTF_Init the fonts open later
    if (cache_.size() >= cache_cap) cache_.clear();
    cache_.emplace(std::move(key), static_cast<float>(w));
    return static_cast<float>(w);
}

} // namespace prism::backends
