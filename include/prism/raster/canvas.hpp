#pragma once

// A software rasterizer target: an ARGB8888 framebuffer that PRISM's draw commands paint into.
// Pixel format: each std::uint32_t is 0xAARRGGBB (on the little-endian targets built here the
// bytes in memory are R, G, B, A). `stride` is in pixels, so row y starts at pixels[y * stride].
//
// Coverage rule: a pixel is covered when its centre (x + 0.5, y + 0.5) lies inside the shape
// (half-open [x0, x1) x [y0, y1) for rects), so adjacent integer rects tile exactly with no seam.
// The clip rect is different: like the SDL backend it is rounded outward (floor top-left, ceil
// bottom-right) so a float-precision edge never crops content that legitimately falls inside it.
//
// simplify: shapes are aliased (binary coverage); only text glyph coverage is blended, so text is
// anti-aliased. Upgrade: coverage-based AA for rounded rects, thick lines and circles -- a
// per-pixel signed-distance/area estimate, as render/shape_mesh.hpp's 1px alpha fringe does for
// the GPU paths.

#include <prism/core/types.hpp>
#include <prism/render/draw_list.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace prism::raster {

class Fonts;

using prism::core::Color;
using prism::core::Point;
using prism::core::Rect;
using prism::render::FontStyle;
using prism::render::TextAnchor;

class Canvas {
public:
    Canvas(std::span<std::uint32_t> pixels, int width, int height, int stride);

    // Points the canvas at another buffer of the same size (the next back buffer) and resets
    // the clip. A canvas reused this way keeps its scratch storage: after the first frame,
    // drawing does not allocate.
    void retarget(std::span<std::uint32_t> pixels);

    int width() const { return width_; }
    int height() const { return height_; }
    int stride() const { return stride_; }
    std::span<std::uint32_t> pixels() const { return pixels_; }

    // --- clip (a stack; push intersects the current clip with the given rect) ---
    void reset_clip();          // the whole canvas
    void push_clip(Rect r);
    void pop_clip();
    [[nodiscard]] Rect clip() const;

    // --- drawing; every operation is clipped and src-over blended ---
    void clear(Color c);                                    // whole canvas, ignoring the clip
    void fill_rect(Rect r, Color c);
    void rect_outline(Rect r, Color c, float thickness);    // bands inside the rect
    void rounded_rect(Rect r, Color c, float radius, float thickness);  // thickness 0 = filled
    void line(Point from, Point to, Color c, float thickness);
    void polyline(std::span<const Point> pts, Color c, float thickness);  // each segment a thick line
    void circle(Point center, float radius, Color c, float thickness);   // thickness 0 = filled
    // Scanline fill, even-odd rule. `pts` is a closed loop in the order given; PRISM emits
    // triangle-strip order, which for the convex outlines it uses is the boundary order.
    void filled_polygon(std::span<const Point> pts, Color c);
    // The image scaled into `dest` nearest-neighbour, multiplied by `alpha` and `tint`
    // (a symbolic icon is white in the file: `tint` is the text colour beside it).
    void image(const prism::render::Image& img, Rect dest, std::uint8_t alpha, Color tint);
    // Via the font module: origin is the top-left of the text box (TextAnchor::TopLeft) or its
    // centre (Center); the box is width(text) x line_height(size), the baseline ascent(size)
    // below the top -- the same metrics ui::TextMeasurer reports.
    void text(const Fonts& fonts, std::string_view utf8, Point origin, float size, Color c,
              float angle, TextAnchor anchor, FontStyle style);

    // --- pixel level (the text path uses these) ---
    void blend_pixel(int x, int y, Color c);   // src-over
    // Blends an 8-bit coverage bitmap (w x h, row-major) with its top-left at pixel (x, y):
    // effective alpha per pixel is c.a * coverage / 255.
    void blend_bitmap(int x, int y, int w, int h, const std::uint8_t* coverage, Color c);

private:
    struct Clip { int x0, y0, x1, y1; };   // pixel range [x0, x1) x [y0, y1)

    [[nodiscard]] Clip clip_of(Rect r) const;             // r rounded outward, cut to the canvas
    void fill_span(int y, int xa, int xb, Color c);       // row y, pixels [xa, xb), clip applied
    void fill_xspan(int y, float xa, float xb, Color c);  // centre rule on a float x span
    // x span (floats, centre rule) of the rounded rect at row y; nullopt when the row misses it.
    [[nodiscard]] std::optional<std::pair<float, float>> rounded_span(int y, Rect r, float radius) const;

    std::span<std::uint32_t> pixels_;
    int width_, height_, stride_;
    std::vector<Clip> clip_stack_;   // empty means the clip is the whole canvas
    std::vector<float> crossings_;   // filled_polygon's scratch: kept so drawing does not allocate
    Clip clip_;
};

} // namespace prism::raster
