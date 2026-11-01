#pragma once

#include <prism/render/draw_list.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <vector>

namespace prism::render {

// A string rasterized in white, coverage in alpha: RGBA8, rows tightly packed, top row first.
// A renderer tints it with the text colour, so one bitmap serves every colour.
struct TextBitmap {
    int width = 0, height = 0;
    std::vector<std::uint8_t> rgba;
};

// Rasterizes `text` at `px_size` device pixels in the font for `role`; nullopt draws nothing.
// The platform layer supplies it (the SDL backend through SDL_ttf), so a renderer has no font
// code and text is drawn with the same fonts the measurer measures with.
using TextRasterizer = std::function<std::optional<TextBitmap>(std::string_view text, float px_size, FontRole)>;

} // namespace prism::render
