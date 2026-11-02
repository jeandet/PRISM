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
        ui_copy_ = open_extra(ui_path, size);
        if (ui_copy_) TTF_AddFallbackFont(mono_, ui_copy_);
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
        if (TTF_Font* f = open_extra(path.c_str(), size)) {
            for (TTF_Font* primary : {ui_, mono_})
                if (primary) TTF_AddFallbackFont(primary, f);
            scripts_.push_back(f);
        }
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
    ui_ = mono_ = ui_copy_ = nullptr;
    extra_.clear();
    scripts_.clear();
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

// A run's bitmap and how far down to draw it so its baseline meets the line's.
struct Piece {
    render::TextBitmap bitmap;
    int dy = 0;   // negative: raised (its top rows, empty space above the font's ascent, are cut)
};

// Pieces side by side, left to right; the line's top is the first font's top.
render::TextBitmap concat(const std::vector<Piece>& pieces) {
    render::TextBitmap out{0, 0, {}};
    for (const auto& p : pieces) {
        out.width += p.bitmap.width;
        out.height = std::max(out.height, p.dy + p.bitmap.height);
    }
    out.rgba.assign(static_cast<size_t>(out.width) * out.height * 4, 0);
    int x = 0;
    for (const auto& p : pieces) {
        for (int y = std::max(0, -p.dy); y < p.bitmap.height; ++y)
            std::memcpy(&out.rgba[(static_cast<size_t>(y + p.dy) * out.width + x) * 4],
                        &p.bitmap.rgba[static_cast<size_t>(y) * p.bitmap.width * 4],
                        static_cast<size_t>(p.bitmap.width) * 4);
        x += p.bitmap.width;
    }
    return out;
}

// The first character of `run` that is not a space (spaces come from any font).
Uint32 first_letter(std::string_view run) {
    const char* p = run.data();
    size_t left = run.size();
    while (left > 0) {
        Uint32 cp = SDL_StepUTF8(&p, &left);
        if (cp != ' ' && cp != '\t') return cp;
    }
    return 0;
}
} // namespace

// SDL_ttf puts each glyph on the ascent of the font it came from: a run drawn by a fallback with
// a larger ascent (Noto Sans Arabic) sits lower in its bitmap. Its piece moves up by the difference.
// Only the script fonts and the UI copy are asked: they have no fallbacks, so "has the glyph" is
// about them alone (asked of ui_ or mono_, SDL_ttf answers for their fallbacks too).
int SdlFontSet::baseline_shift(TTF_Font* primary, std::string_view run) const {
    Uint32 cp = first_letter(run);
    if (cp == 0 || (ui_copy_ && TTF_FontHasGlyph(ui_copy_, cp))) return 0;
    for (TTF_Font* f : scripts_)
        if (TTF_FontHasGlyph(f, cp)) return TTF_GetFontAscent(primary) - TTF_GetFontAscent(f);
    return 0;
}

// Mixed-direction text: each run shaped in its own direction, the runs in visual order (UAX #9),
// as ui::caret_x_at() expects them, on one baseline.
std::optional<render::TextBitmap> SdlFontSet::rasterize_bidi(TTF_Font* font, std::string_view text,
                                                             const std::vector<ui::BidiRun>& runs) {
    std::vector<Piece> pieces;
    for (const auto& r : runs) {
        TTF_SetFontDirection(font, r.rtl ? TTF_DIRECTION_RTL : TTF_DIRECTION_LTR);
        auto run = text.substr(r.begin, r.end - r.begin);
        pieces.push_back({rasterize_run(font, run).value_or(blank_run(font, run)), baseline_shift(font, run)});
    }
    TTF_SetFontDirection(font, TTF_DIRECTION_INVALID);   // back to HarfBuzz's guess
    return concat(pieces);
}

std::optional<render::TextBitmap> SdlFontSet::rasterize(std::string_view text, float px_size, render::FontRole role) {
    TTF_Font* font = get(role, px_size);
    if (!font || text.empty()) return std::nullopt;
    auto runs = ui::bidi_runs(text);
    return ui::is_plain_ltr(runs) ? rasterize_run(font, text) : rasterize_bidi(font, text, runs);
}

} // namespace prism::backends
