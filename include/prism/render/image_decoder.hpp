#pragma once

#include <prism/core/types.hpp>
#include <prism/render/draw_list.hpp>

#include <cstddef>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>

// Image files (PNG, JPEG, SVG, ...) decoded into Image. Declared here, implemented by the image
// backend chosen at build time -- SDL3_image today (src/backends/sdl_image_decoder.cpp), the
// build-time form of Qt's platform plugins (doc/review-2026-11-02.md §4). Image itself stays
// format-free. Safe to call from any thread.
namespace prism::render {

// `svg_size`: the box to draw a vector image in -- the largest size that fits it, keeping the
// image's aspect ratio; ignored for bitmaps. Omitted for an SVG, it is drawn at the size the
// file declares.
std::expected<std::shared_ptr<const Image>, std::string>
decode_image(std::span<const std::byte> bytes, std::optional<core::Size> svg_size = std::nullopt);

std::expected<std::shared_ptr<const Image>, std::string>
load_image(const std::filesystem::path& path, std::optional<core::Size> svg_size = std::nullopt);

} // namespace prism::render
