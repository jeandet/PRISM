#pragma once

#include <prism/render/draw_list.hpp>
#include <prism/render/text_bitmap.hpp>
#include <prism/ui/text_bidi.hpp>

#include <array>
#include <optional>
#include <string_view>
#include <vector>

typedef struct TTF_Font TTF_Font;

namespace prism::backends {

// The UI font and the mono font, each a fallback of the other (mono through a copy, see open()): Noto Sans lacks
// the geometric symbols (password dot, dropdown arrow, check mark) and the Nerd Font icons, which
// the mono font has. SDL_ttf does not resize fallbacks with their primary, so both fonts always
// share one size. One instance per thread: a TTF_Font must not be used by two threads.
// Script fonts found next to the UI font (Noto Sans Hebrew and Arabic, bundled) are fallbacks of
// both, so right-to-left text draws real letters; they are resized with the others too.
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

    // File names of the script fallbacks, looked for in the UI font's folder.
    static constexpr std::array<const char*, 2> script_font_files{"NotoSansHebrew-Regular.ttf",
                                                                  "NotoSansArabic-Regular.ttf"};

private:
    void open_script_fallbacks(const char* beside, float size);
    TTF_Font* open_extra(const char* path, float size);   // a fallback-only font, kept in extra_
    int baseline_shift(TTF_Font* primary, std::string_view run) const;
    std::optional<render::TextBitmap> rasterize_bidi(TTF_Font* font, std::string_view text,
                                                     const std::vector<ui::BidiRun>& runs);

    TTF_Font* ui_ = nullptr;
    TTF_Font* mono_ = nullptr;
    std::vector<TTF_Font*> extra_;   // fallbacks only: a copy of the UI font, the script fonts
    TTF_Font* ui_copy_ = nullptr;      // in extra_: the UI font again, without fallbacks of its own
    std::vector<TTF_Font*> scripts_;   // in extra_: Hebrew, Arabic
    float size_ = 0.f;
};

} // namespace prism::backends
