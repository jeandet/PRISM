#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// A minimal PNG writer, so an SVG capture can embed images (render/svg_export.hpp) without a
// dependency: render/ stays free of SDL. PNG: https://www.w3.org/TR/png-3/, zlib: RFC 1950,
// deflate stored blocks: RFC 1951 §3.2.4.
// simplify: the pixels are stored, not compressed (a 16 pt icon is about 1 KB). Upgrade: a
// deflate encoder, if large images ever end up in captures.
namespace prism::render {

namespace png_detail {

inline std::uint32_t crc32(std::span<const std::uint8_t> bytes, std::uint32_t crc = 0) {
    static const auto table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        return t;
    }();
    crc = ~crc;
    for (auto b : bytes) crc = table[(crc ^ b) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

inline std::uint32_t adler32(std::span<const std::uint8_t> bytes) {
    std::uint32_t a = 1, b = 0;
    for (auto byte : bytes) {
        a = (a + byte) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

inline void put_be32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int shift = 24; shift >= 0; shift -= 8) out.push_back(static_cast<std::uint8_t>(v >> shift));
}

// A chunk: length, type, data, and the CRC of type and data.
inline void put_chunk(std::vector<std::uint8_t>& out, const char (&type)[5], std::span<const std::uint8_t> data) {
    put_be32(out, static_cast<std::uint32_t>(data.size()));
    std::size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    put_be32(out, crc32(std::span{out}.subspan(start)));
}

// Raw bytes as a zlib stream of stored (uncompressed) deflate blocks, at most 65535 bytes each.
inline std::vector<std::uint8_t> zlib_stored(std::span<const std::uint8_t> raw) {
    std::vector<std::uint8_t> z{0x78, 0x01};   // deflate, 32K window, no preset dictionary
    std::size_t at = 0;
    do {
        std::size_t len = std::min<std::size_t>(raw.size() - at, 65535);
        bool last = at + len == raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(static_cast<std::uint8_t>(len));
        z.push_back(static_cast<std::uint8_t>(len >> 8));
        z.push_back(static_cast<std::uint8_t>(~len));
        z.push_back(static_cast<std::uint8_t>(~len >> 8));
        z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(at), raw.begin() + static_cast<std::ptrdiff_t>(at + len));
        at += len;
    } while (at < raw.size());
    put_be32(z, adler32(raw));
    return z;
}

} // namespace png_detail

// `rgba`: width x height pixels, 4 bytes each (straight alpha), rows top to bottom.
inline std::vector<std::uint8_t> encode_png(int width, int height, std::span<const std::uint8_t> rgba) {
    using namespace png_detail;
    std::vector<std::uint8_t> out{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    // A fixed array, not a vector filled in steps: GCC 14 misreads that as an overflow.
    auto w = static_cast<std::uint32_t>(width), h = static_cast<std::uint32_t>(height);
    const std::array<std::uint8_t, 13> header{
        std::uint8_t(w >> 24), std::uint8_t(w >> 16), std::uint8_t(w >> 8), std::uint8_t(w),
        std::uint8_t(h >> 24), std::uint8_t(h >> 16), std::uint8_t(h >> 8), std::uint8_t(h),
        8, 6, 0, 0, 0};   // 8 bits per channel, RGBA, deflate, no filter, no interlace
    put_chunk(out, "IHDR", header);
    std::vector<std::uint8_t> scanlines;   // each row starts with its filter type: 0, none
    std::size_t row = static_cast<std::size_t>(width) * 4;
    for (int y = 0; y < height; ++y) {
        scanlines.push_back(0);
        auto from = rgba.begin() + static_cast<std::ptrdiff_t>(y * row);
        scanlines.insert(scanlines.end(), from, from + static_cast<std::ptrdiff_t>(row));
    }
    put_chunk(out, "IDAT", zlib_stored(scanlines));
    put_chunk(out, "IEND", {});
    return out;
}

// RFC 4648 base64, for data: URIs.
inline std::string base64(std::span<const std::byte> bytes) {
    static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        std::uint32_t n = std::to_integer<std::uint32_t>(bytes[i]) << 16;
        if (i + 1 < bytes.size()) n |= std::to_integer<std::uint32_t>(bytes[i + 1]) << 8;
        if (i + 2 < bytes.size()) n |= std::to_integer<std::uint32_t>(bytes[i + 2]);
        out += alphabet[(n >> 18) & 63];
        out += alphabet[(n >> 12) & 63];
        out += i + 1 < bytes.size() ? alphabet[(n >> 6) & 63] : '=';
        out += i + 2 < bytes.size() ? alphabet[n & 63] : '=';
    }
    return out;
}

} // namespace prism::render
