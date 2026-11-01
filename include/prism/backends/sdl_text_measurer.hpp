#pragma once

#include <prism/backends/sdl_font_set.hpp>
#include <prism/ui/text_metrics.hpp>

#include <string>
#include <unordered_map>

namespace prism::backends {

// SDL3_ttf text measurement for the logic thread. Owns its own fonts, separate from the render
// thread's (see SdlFontSet). Logic thread only; not locked. Fonts open lazily on first use after
// the backend's TTF_Init().
class SdlTextMeasurer final : public ui::TextMeasurer {
public:
    SdlTextMeasurer(std::string ui_font_path, std::string mono_font_path)
        : ui_path_(std::move(ui_font_path)), mono_path_(std::move(mono_font_path)) {}
    SdlTextMeasurer(SdlTextMeasurer&&) = delete;

    float width(std::string_view utf8, float size, ui::FontRole role) const override;
    float line_height(float size, ui::FontRole role) const override;

private:
    TTF_Font* font(float size, ui::FontRole role) const;   // null before TTF_Init or if none opened

    std::string ui_path_, mono_path_;
    mutable SdlFontSet fonts_;
    mutable bool opened_ = false;
    // simplify: cleared wholesale when full instead of LRU; the working set (labels, the line
    // being edited) is far below the cap. Upgrade to LRU if profiles show re-measuring.
    mutable std::unordered_map<std::string, float> cache_;
    static constexpr std::size_t cache_cap = 4096;
};

} // namespace prism::backends
