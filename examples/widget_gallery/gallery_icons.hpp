// The gallery's icons: SVG files beside its sources (bundled into the page on the web).
#pragma once

#include <prism/render/image_decoder.hpp>

#include <memory>
#include <string>

#ifndef GALLERY_ICON_DIR
#define GALLERY_ICON_DIR "icons"
#endif

namespace gallery {

// Drawn at 16 pt; a missing file gives no icon rather than failing the app.
inline std::shared_ptr<const prism::render::Image> icon(const char* name) {
    auto img = prism::render::load_image(std::string(GALLERY_ICON_DIR) + "/" + name,
                                         prism::core::Size{prism::core::Width{16}, prism::core::Height{16}});
    return img ? *img : nullptr;
}

} // namespace gallery
