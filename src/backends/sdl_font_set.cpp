#include <prism/backends/sdl_font_set.hpp>
#include <prism/ui/text_bidi.hpp>

#include <SDL3/SDL.h>
#include <cstdint>
#include <cstring>

#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string_view>
#include <vector>

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
        // Each falls back on the other, but mono gets its own copy of the UI font: a fallback
        // loop (ui -> mono -> ui) sends SDL_ttf's resize walk around it forever when it starts
        // from a third font, as the script fallbacks do (UpdateFontText only stops at its start).
        TTF_AddFallbackFont(ui_, mono_);
        if (TTF_Font* ui_copy = open_extra(ui_path, size)) TTF_AddFallbackFont(mono_, ui_copy);
    }
    open_script_fallbacks(ui_path ? ui_path : mono_path, size);
    size_ = size;
    return ui_ || mono_;
}

void SdlFontSet::open_script_fallbacks(const char* beside, float size) {
    if (!beside) return;
    auto dir = std::filesystem::path(beside).parent_path();
    for (const char* name : script_font_files) {
        auto path = (dir / name).string();
        if (!std::filesystem::is_regular_file(path)) continue;
        if (TTF_Font* f = open_extra(path.c_str(), size))
            for (TTF_Font* primary : {ui_, mono_})
                if (primary) TTF_AddFallbackFont(primary, f);
    }
}

TTF_Font* SdlFontSet::open_extra(const char* path, float size) {
    TTF_Font* f = TTF_OpenFont(path, size);
    if (!f) return nullptr;
    TTF_SetFontHinting(f, TTF_HINTING_LIGHT_SUBPIXEL);
    extra_.push_back(f);
    return f;
}

void SdlFontSet::close() {
    if (TTF_WasInit() > 0) {
        if (ui_) TTF_CloseFont(ui_);
        if (mono_) TTF_CloseFont(mono_);
        for (TTF_Font* f : extra_) TTF_CloseFont(f);
    }
    ui_ = mono_ = nullptr;
    extra_.clear();
}

TTF_Font* SdlFontSet::get(render::FontRole role, float px_size) {
    TTF_Font* wanted = role == render::FontRole::Mono ? mono_ : ui_;
    TTF_Font* font = wanted ? wanted : (ui_ ? ui_ : mono_);
    if (font && std::abs(size_ - px_size) > 0.01f) {
        if (ui_) TTF_SetFontSize(ui_, px_size);      // both: each is the other's fallback
        if (mono_) TTF_SetFontSize(mono_, px_size);
        for (TTF_Font* f : extra_) TTF_SetFontSize(f, px_size);
        size_ = px_size;
    }
    return font;
}

namespace {
// One string in one direction, as SDL_ttf draws it.
std::optional<render::TextBitmap> rasterize_run(TTF_Font* font, std::string_view text) {
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

// A run SDL_ttf draws nothing for (only spaces): blank, but as wide as measured, so the runs
// after it stay where the caret math puts them.
render::TextBitmap blank_run(TTF_Font* font, std::string_view text) {
    int w = 0, h = 0;
    TTF_GetStringSize(font, text.data(), text.size(), &w, &h);
    return {w, h, std::vector<std::uint8_t>(static_cast<size_t>(w) * h * 4, 0)};
}

// Pieces side by side, left to right, top-aligned (one font, so one baseline).
render::TextBitmap concat(const std::vector<render::TextBitmap>& pieces) {
    render::TextBitmap out{0, 0, {}};
    for (const auto& p : pieces) {
        out.width += p.width;
        out.height = std::max(out.height, p.height);
    }
    out.rgba.assign(static_cast<size_t>(out.width) * out.height * 4, 0);
    int x = 0;
    for (const auto& p : pieces) {
        for (int y = 0; y < p.height; ++y)
            std::memcpy(&out.rgba[(static_cast<size_t>(y) * out.width + x) * 4],
                        &p.rgba[static_cast<size_t>(y) * p.width * 4], static_cast<size_t>(p.width) * 4);
        x += p.width;
    }
    return out;
}

// Mixed-direction text: each run shaped in its own direction, the runs in visual order (UAX #9),
// as ui::caret_x_at() expects them.
std::optional<render::TextBitmap> rasterize_bidi(TTF_Font* font, std::string_view text,
                                                 const std::vector<ui::BidiRun>& runs) {
    std::vector<render::TextBitmap> pieces;
    for (const auto& r : runs) {
        TTF_SetFontDirection(font, r.rtl ? TTF_DIRECTION_RTL : TTF_DIRECTION_LTR);
        auto run = text.substr(r.begin, r.end - r.begin);
        pieces.push_back(rasterize_run(font, run).value_or(blank_run(font, run)));
    }
    TTF_SetFontDirection(font, TTF_DIRECTION_INVALID);   // back to HarfBuzz's guess
    return concat(pieces);
}
} // namespace

std::optional<render::TextBitmap> SdlFontSet::rasterize(std::string_view text, float px_size, render::FontRole role) {
    TTF_Font* font = get(role, px_size);
    if (!font || text.empty()) return std::nullopt;
    auto runs = ui::bidi_runs(text);
    return ui::is_plain_ltr(runs) ? rasterize_run(font, text) : rasterize_bidi(font, text, runs);
}

} // namespace prism::backends
