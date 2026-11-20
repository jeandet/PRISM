#pragma once

// The font files compiled into the firmware, as byte spans. The arrays are generated at build
// time by scripts/embed.py (a meson custom_target): GCC 15 supports #embed in C as an extension
// but not in this C++ TU, so a small Python script emits the arrays instead.
//
// assets/fonts/embedded/NotoSans-Latin.ttf and NotoSans-Bold-Latin.ttf, SIL OFL (NotoSans-OFL.txt beside them).

#include <cstddef>
#include <cstdint>
#include <span>

namespace prism::raster::embedded {

std::span<const std::uint8_t> noto_sans_latin();
std::span<const std::uint8_t> noto_sans_bold_latin();

} // namespace prism::raster::embedded
