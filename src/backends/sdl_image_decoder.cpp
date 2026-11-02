// render/image_decoder.hpp with SDL3_image.
#include <prism/render/image_decoder.hpp>

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <fstream>
#include <iterator>
#include <vector>

namespace prism::render {

namespace {

// RGBA8, straight alpha, row by row: what Image holds.
std::expected<std::shared_ptr<const Image>, std::string> to_image(SDL_Surface* decoded) {
    if (!decoded) return std::unexpected(std::string(SDL_GetError()));
    SDL_Surface* rgba = SDL_ConvertSurface(decoded, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(decoded);
    if (!rgba) return std::unexpected(std::string(SDL_GetError()));
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(rgba->w) * static_cast<std::size_t>(rgba->h) * 4);
    for (int y = 0; y < rgba->h; ++y) {
        auto* row = static_cast<const std::uint8_t*>(rgba->pixels) + static_cast<std::ptrdiff_t>(y) * rgba->pitch;
        std::copy(row, row + rgba->w * 4, pixels.begin() + static_cast<std::ptrdiff_t>(y) * rgba->w * 4);
    }
    int w = rgba->w, h = rgba->h;
    SDL_DestroySurface(rgba);
    return Image::from_rgba(w, h, std::move(pixels));
}

} // namespace

std::expected<std::shared_ptr<const Image>, std::string>
decode_image(std::span<const std::byte> bytes, std::optional<core::Size> svg_size) {
    SDL_IOStream* io = SDL_IOFromConstMem(bytes.data(), bytes.size());
    if (!io) return std::unexpected(std::string(SDL_GetError()));
    if (svg_size && IMG_isSVG(io)) {
        auto w = static_cast<int>(svg_size->w.raw()), h = static_cast<int>(svg_size->h.raw());
        SDL_Surface* svg = IMG_LoadSizedSVG_IO(io, w, h);   // does not close io: we do
        SDL_CloseIO(io);
        return to_image(svg);
    }
    return to_image(IMG_Load_IO(io, true));   // closes io
}

std::expected<std::shared_ptr<const Image>, std::string>
load_image(const std::filesystem::path& path, std::optional<core::Size> svg_size) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::unexpected("cannot read " + path.string());
    std::vector<char> data{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    return decode_image(std::as_bytes(std::span{data}), svg_size);
}

} // namespace prism::render
