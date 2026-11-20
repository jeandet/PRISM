#pragma once

// The font files compiled into the library, as byte spans. The arrays are generated at build
// time by scripts/embed.py (a meson custom_target): C++ #embed is not available in every
// compiler PRISM supports, so a small Python script emits the arrays instead.
//
// assets/fonts/embedded/NotoSans-Latin.ttf and NotoSans-Bold-Latin.ttf, SIL OFL (NotoSans-OFL.txt beside them).

#include <cstddef>
#include <cstdint>
#include <span>

namespace prism::raster::embedded {

std::span<const std::uint8_t> noto_sans_latin();
std::span<const std::uint8_t> noto_sans_bold_latin();

} // namespace prism::raster::embedded
