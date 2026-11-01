#pragma once

#include <prism/render/draw_list.hpp>
#include <prism/render/text_bitmap.hpp>

#include <optional>
#include <string_view>

typedef struct TTF_Font TTF_Font;

namespace prism::backends {

// The UI font and the mono font, each registered as the other's SDL_ttf fallback: Noto Sans lacks
// the geometric symbols (password dot, dropdown arrow, check mark) and the Nerd Font icons, which
// the mono font has. SDL_ttf does not resize fallbacks with their primary, so both fonts always
// share one size. One instance per thread: a TTF_Font must not be used by two threads.
// simplify: a string mixing fonts (e.g. "abc●") baselines each glyph on its own font's ascent,
// about 1 px apart at UI sizes; PRISM draws its symbols as separate text commands.
class SdlFontSet {
public:
    SdlFontSet() = default;
    ~SdlFontSet() { close(); }
    SdlFontSet(const SdlFontSet&) = delete;
    SdlFontSet& operator=(const SdlFontSet&) = delete;

    // Opens whatever it can; returns false only if neither font opened. Needs TTF_Init().
    bool open(const char* ui_path, const char* mono_path, float size);
    // Safe after TTF_Quit() (then the fonts are leaked, not closed: closing would crash).
    void close();

    // The font for `role` at `px_size` (falls back to the other role's font if one failed to open);
    // nullptr if none opened.
    TTF_Font* get(render::FontRole role, float px_size);

    // `text` in white at `px_size`, coverage in alpha (what a GPU renderer uploads and tints).
    std::optional<render::TextBitmap> rasterize(std::string_view text, float px_size, render::FontRole role);

private:
    TTF_Font* ui_ = nullptr;
    TTF_Font* mono_ = nullptr;
    float size_ = 0.f;
};

} // namespace prism::backends
