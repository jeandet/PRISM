#pragma once

#include <prism/ui/text_metrics.hpp>

#include <string>
#include <unordered_map>

typedef struct TTF_Font TTF_Font;

namespace prism::backends {

// SDL3_ttf text measurement for the logic thread. Owns its own TTF_Font, separate from the
// render thread's: SDL_ttf serializes font open/close internally, but one TTF_Font must not be
// used by two threads (its FreeType face is not thread-safe). Logic thread only; not locked.
// The font opens lazily on first use, after the backend's TTF_Init().
class SdlTextMeasurer final : public ui::TextMeasurer {
public:
    explicit SdlTextMeasurer(std::string font_path) : font_path_(std::move(font_path)) {}
    ~SdlTextMeasurer() override;
    SdlTextMeasurer(const SdlTextMeasurer&) = delete;
    SdlTextMeasurer& operator=(const SdlTextMeasurer&) = delete;

    // simplify: one font for both roles until the bundled UI font lands.
    float width(std::string_view utf8, float size, ui::FontRole role) const override;

private:
    TTF_Font* font(float size) const;

    std::string font_path_;
    mutable TTF_Font* font_ = nullptr;
    mutable bool open_failed_ = false;
    mutable float font_size_ = 0.f;
    // simplify: cleared wholesale when full instead of LRU; the working set (labels, the line
    // being edited) is far below the cap. Upgrade to LRU if profiles show re-measuring.
    mutable std::unordered_map<std::string, float> cache_;
    static constexpr std::size_t cache_cap = 4096;
};

} // namespace prism::backends
