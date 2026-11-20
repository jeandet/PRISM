#include <prism/raster/canvas.hpp>

#include <prism/raster/fonts.hpp>

#include <prism/core/utf8.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace prism::raster {
using namespace prism::core;
using prism::render::Image;
using prism::render::TextAnchor;

namespace {

// src-over for straight alpha. The framebuffer's alpha stays 255 (the caller clears it opaque),
// so the plain channel lerp below is exact; a translucent destination would need the /out_a step.
inline std::uint32_t over(std::uint32_t dst, std::uint8_t r, std::uint8_t g, std::uint8_t b,
                          std::uint8_t a) {
    if (a == 0) return dst;
    if (a == 255)
        return 0xFF000000u | (std::uint32_t(r) << 16) | (std::uint32_t(g) << 8) | b;
    const std::uint32_t ia = 255u - a;
    auto ch = [&](std::uint32_t s, std::uint32_t d) { return (s * a + d * ia + 127u) / 255u; };
    const std::uint32_t da = dst >> 24;
    const std::uint32_t oa = a + (da * ia + 127u) / 255u;
    return (oa << 24) | (ch(r, (dst >> 16) & 0xFFu) << 16) | (ch(g, (dst >> 8) & 0xFFu) << 8)
         | ch(b, dst & 0xFFu);
}

inline std::uint32_t pack_opaque(Color c) {
    return 0xFF000000u | (std::uint32_t(c.r) << 16) | (std::uint32_t(c.g) << 8) | c.b;
}

// First pixel index whose centre lies in [lo, hi): x + 0.5 >= lo and x + 0.5 < hi.
inline int span_begin(float lo) { return static_cast<int>(std::ceil(lo - 0.5f)); }
inline int span_end(float hi) { return static_cast<int>(std::ceil(hi - 0.5f)); }

} // namespace

Canvas::Canvas(std::span<std::uint32_t> pixels, int width, int height, int stride)
    : pixels_(pixels), width_(width), height_(height), stride_(stride) {
    reset_clip();
}

void Canvas::retarget(std::span<std::uint32_t> pixels) {
    pixels_ = pixels;
    reset_clip();
}

Canvas::Clip Canvas::clip_of(Rect r) const {
    int x0 = static_cast<int>(std::floor(r.origin.x.raw()));
    int y0 = static_cast<int>(std::floor(r.origin.y.raw()));
    int x1 = static_cast<int>(std::ceil(r.origin.x.raw() + r.extent.w.raw()));
    int y1 = static_cast<int>(std::ceil(r.origin.y.raw() + r.extent.h.raw()));
    x0 = std::max(x0, 0);  y0 = std::max(y0, 0);
    x1 = std::min(x1, width_);  y1 = std::min(y1, height_);
    if (x0 > x1) x0 = x1;
    if (y0 > y1) y0 = y1;
    return {x0, y0, x1, y1};
}

void Canvas::reset_clip() {
    clip_ = {0, 0, width_, height_};
    clip_stack_.clear();
}

void Canvas::push_clip(Rect r) {
    // The snapshot's ClipPush rects are already resolved against the outer clips (resolve_clips),
    // so intersecting with the current clip too changes nothing for a published snapshot -- and
    // keeps an unresolved one from widening the clip. Intersected with the canvas bounds as well.
    Clip c = clip_of(r);
    c.x0 = std::max(c.x0, clip_.x0);  c.y0 = std::max(c.y0, clip_.y0);
    c.x1 = std::min(c.x1, clip_.x1);  c.y1 = std::min(c.y1, clip_.y1);
    clip_stack_.push_back(clip_);
    clip_ = c;
}

void Canvas::pop_clip() {
    if (!clip_stack_.empty()) {
        clip_ = clip_stack_.back();
        clip_stack_.pop_back();
    } else {
        reset_clip();
    }
}

Rect Canvas::clip() const {
    return Rect{Point{X{static_cast<float>(clip_.x0)}, Y{static_cast<float>(clip_.y0)}},
                Size{Width{static_cast<float>(clip_.x1 - clip_.x0)},
                     Height{static_cast<float>(clip_.y1 - clip_.y0)}}};
}

void Canvas::clear(Color c) {
    // Writes the pixel as-is (no blending): the framebuffer is fully repainted each frame, and a
    // translucent clear over the previous frame is not a case the board needs.
    const std::uint32_t packed = (std::uint32_t(c.a) << 24) | (std::uint32_t(c.r) << 16)
                               | (std::uint32_t(c.g) << 8) | c.b;
    for (int y = 0; y < height_; ++y)
        for (int x = 0; x < width_; ++x)
            pixels_[static_cast<std::size_t>(y) * stride_ + x] = packed;
}

void Canvas::fill_span(int y, int xa, int xb, Color c) {
    if (y < clip_.y0 || y >= clip_.y1) return;
    xa = std::max(xa, clip_.x0);
    xb = std::min(xb, clip_.x1);
    if (xa >= xb) return;
    std::uint32_t* row = pixels_.data() + static_cast<std::size_t>(y) * stride_;
    if (c.a == 255) {
        const std::uint32_t p = pack_opaque(c);
        for (int x = xa; x < xb; ++x) row[x] = p;
    } else {
        for (int x = xa; x < xb; ++x) row[x] = over(row[x], c.r, c.g, c.b, c.a);
    }
}

void Canvas::fill_xspan(int y, float xa, float xb, Color c) {
    fill_span(y, span_begin(xa), span_end(xb), c);
}

void Canvas::fill_rect(Rect r, Color c) {
    const int y0 = span_begin(r.origin.y.raw());
    const int y1 = span_end(r.origin.y.raw() + r.extent.h.raw());
    for (int y = y0; y < y1; ++y)
        fill_xspan(y, r.origin.x.raw(), r.origin.x.raw() + r.extent.w.raw(), c);
}

void Canvas::rect_outline(Rect r, Color c, float thickness) {
    if (thickness <= 0.f) return;
    const float x0 = r.origin.x.raw(), y0 = r.origin.y.raw();
    const float w = r.extent.w.raw(), h = r.extent.h.raw();
    if (thickness * 2.f >= std::min(w, h)) {   // the bands would cover the whole rect
        fill_rect(r, c);
        return;
    }
    fill_rect(Rect{Point{X{x0}, Y{y0}}, Size{Width{w}, Height{thickness}}}, c);
    fill_rect(Rect{Point{X{x0}, Y{y0 + h - thickness}}, Size{Width{w}, Height{thickness}}}, c);
    fill_rect(Rect{Point{X{x0}, Y{y0 + thickness}}, Size{Width{thickness}, Height{h - 2.f * thickness}}}, c);
    fill_rect(Rect{Point{X{x0 + w - thickness}, Y{y0 + thickness}}, Size{Width{thickness}, Height{h - 2.f * thickness}}}, c);
}

std::optional<std::pair<float, float>> Canvas::rounded_span(int y, Rect r, float radius) const {
    const float x0 = r.origin.x.raw(), y0 = r.origin.y.raw();
    const float x1 = x0 + r.extent.w.raw(), y1 = y0 + r.extent.h.raw();
    const float cy = static_cast<float>(y) + 0.5f;
    if (cy < y0 || cy >= y1) return std::nullopt;
    float inset = 0.f;
    auto corner = [&](float dy) {   // dy > 0: the row is inside the corner band
        if (dy > 0.f) inset = std::max(inset, radius - std::sqrt(std::max(0.f, radius * radius - dy * dy)));
    };
    corner((y0 + radius) - cy);   // top corners
    corner(cy - (y1 - radius));   // bottom corners
    const float xa = x0 + inset, xb = x1 - inset;
    if (!(xb > xa)) return std::nullopt;
    return std::pair{xa, xb};
}

void Canvas::rounded_rect(Rect r, Color c, float radius, float thickness) {
    const float w = r.extent.w.raw(), h = r.extent.h.raw();
    radius = std::clamp(radius, 0.f, std::min(w, h) / 2.f);
    if (radius <= 0.5f) {   // a square corner needs no arcs (as the SDL backend special-cases)
        if (thickness > 0.f) rect_outline(r, c, thickness);
        else fill_rect(r, c);
        return;
    }
    if (thickness <= 0.f) {
        const int y0 = span_begin(r.origin.y.raw());
        const int y1 = span_end(r.origin.y.raw() + h);
        for (int y = y0; y < y1; ++y)
            if (auto s = rounded_span(y, r, radius)) fill_xspan(y, s->first, s->second, c);
        return;
    }
    // Stroke: pixels within thickness/2 of the outline = the rounded rect grown by thickness/2
    // minus the one shrunk by thickness/2 (offsetting a rounded rect keeps its corner radius).
    const float t2 = thickness / 2.f;
    const float x0 = r.origin.x.raw(), y0 = r.origin.y.raw();
    const Rect outer{Point{X{x0 - t2}, Y{y0 - t2}}, Size{Width{w + 2.f * t2}, Height{h + 2.f * t2}}};
    const Rect inner{Point{X{x0 + t2}, Y{y0 + t2}}, Size{Width{std::max(0.f, w - 2.f * t2)}, Height{std::max(0.f, h - 2.f * t2)}}};
    const int y0i = span_begin(outer.origin.y.raw());
    const int y1i = span_end(outer.origin.y.raw() + outer.extent.h.raw());
    for (int y = y0i; y < y1i; ++y) {
        auto so = rounded_span(y, outer, radius + t2);
        if (!so) continue;
        auto si = rounded_span(y, inner, std::max(0.f, radius - t2));
        if (!si) {
            fill_xspan(y, so->first, so->second, c);
        } else {
            fill_xspan(y, so->first, si->first, c);
            fill_xspan(y, si->second, so->second, c);
        }
    }
}

void Canvas::line(Point from, Point to, Color c, float thickness) {
    const float dx = to.x.raw() - from.x.raw(), dy = to.y.raw() - from.y.raw();
    const float len = std::sqrt(dx * dx + dy * dy);
    if (thickness <= 1.f) {
        // Hairline: set the pixel under each half-pixel sample along the segment (the SDL backend
        // uses SDL_RenderLine for these; this is its integer twin).
        const int n = std::max(1, static_cast<int>(std::ceil(len * 2.f)));
        for (int i = 0; i <= n; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(n);
            blend_pixel(static_cast<int>(std::lround(from.x.raw() + dx * t)),
                        static_cast<int>(std::lround(from.y.raw() + dy * t)), c);
        }
        return;
    }
    // Thick line: the segment as a square-ended quad. simplify: square ends, no caps or joins --
    // polyline() below draws each segment separately, so joins are covered by overlap.
    const float ux = len > 0.f ? dx / len : 1.f, uy = len > 0.f ? dy / len : 0.f;
    const float nx = -uy * thickness / 2.f, ny = ux * thickness / 2.f;
    const Point quad[4] = {
        Point{X{from.x.raw() + nx}, Y{from.y.raw() + ny}},
        Point{X{to.x.raw() + nx}, Y{to.y.raw() + ny}},
        Point{X{to.x.raw() - nx}, Y{to.y.raw() - ny}},
        Point{X{from.x.raw() - nx}, Y{from.y.raw() - ny}},
    };
    filled_polygon(quad, c);
}

void Canvas::polyline(std::span<const Point> pts, Color c, float thickness) {
    for (std::size_t i = 1; i < pts.size(); ++i)
        line(pts[i - 1], pts[i], c, thickness);
}

void Canvas::circle(Point center, float radius, Color c, float thickness) {
    if (radius <= 0.f) return;
    const float cx = center.x.raw(), cy = center.y.raw();
    if (thickness <= 0.f) {
        const float r2 = radius * radius;
        const int y0 = span_begin(cy - radius), y1 = span_end(cy + radius);
        for (int y = y0; y < y1; ++y) {
            const float d = static_cast<float>(y) + 0.5f - cy;
            if (std::abs(d) > radius) continue;
            const float half = std::sqrt(r2 - d * d);
            fill_xspan(y, cx - half, cx + half, c);
        }
        return;
    }
    // Stroke: an annulus around the nominal circle, |dist - radius| <= thickness/2 (the SDL
    // backend strokes the same outline through a mesh).
    const float ro = radius + thickness / 2.f, ri = std::max(0.f, radius - thickness / 2.f);
    const int y0 = span_begin(cy - ro), y1 = span_end(cy + ro);
    for (int y = y0; y < y1; ++y) {
        const float d = static_cast<float>(y) + 0.5f - cy;
        if (std::abs(d) > ro) continue;
        const float ho = std::sqrt(ro * ro - d * d);
        const float hi = std::abs(d) <= ri ? std::sqrt(ri * ri - d * d) : 0.f;
        if (hi > 0.f) {
            fill_xspan(y, cx - ho, cx - hi, c);
            fill_xspan(y, cx + hi, cx + ho, c);
        } else {
            fill_xspan(y, cx - ho, cx + ho, c);
        }
    }
}

void Canvas::filled_polygon(std::span<const Point> pts, Color c) {
    const std::size_t n = pts.size();
    if (n < 3) return;
    float min_y = pts[0].y.raw(), max_y = pts[0].y.raw();
    for (const Point p : pts) {
        min_y = std::min(min_y, p.y.raw());
        max_y = std::max(max_y, p.y.raw());
    }
    std::vector<float>& xs = crossings_;
    xs.reserve(n); // a row crosses at most n edges: no growth inside the row loop
    const int y0 = span_begin(min_y), y1 = span_end(max_y);
    for (int y = y0; y < y1; ++y) {
        const float yc = static_cast<float>(y) + 0.5f;
        xs.clear();
        for (std::size_t i = 0; i < n; ++i) {
            const Point a = pts[i], b = pts[(i + 1) % n];
            const float ya = a.y.raw(), yb = b.y.raw();
            if ((ya <= yc) != (yb <= yc)) {   // half-open: a vertex on the line counts once
                const float t = (yc - ya) / (yb - ya);
                xs.push_back(a.x.raw() + t * (b.x.raw() - a.x.raw()));
            }
        }
        std::sort(xs.begin(), xs.end());
        for (std::size_t k = 0; k + 1 < xs.size(); k += 2)
            fill_xspan(y, xs[k], xs[k + 1], c);
    }
}

void Canvas::image(const Image& img, Rect dest, std::uint8_t alpha, Color tint) {
    const float dx0 = dest.origin.x.raw(), dy0 = dest.origin.y.raw();
    const float dw = dest.extent.w.raw(), dh = dest.extent.h.raw();
    if (dw <= 0.f || dh <= 0.f) return;
    const int iw = img.width(), ih = img.height();
    const std::span<const std::uint8_t> px = img.pixels();
    const int x0 = span_begin(dx0), x1 = span_end(dx0 + dw);
    const int y0 = span_begin(dy0), y1 = span_end(dy0 + dh);
    for (int y = y0; y < y1; ++y) {
        const float v = (static_cast<float>(y) + 0.5f - dy0) / dh;
        const int sy = std::clamp(static_cast<int>(v * ih), 0, ih - 1);
        for (int x = x0; x < x1; ++x) {
            const float u = (static_cast<float>(x) + 0.5f - dx0) / dw;
            const int sx = std::clamp(static_cast<int>(u * iw), 0, iw - 1);
            const std::uint8_t* t = px.data() + (static_cast<std::size_t>(sy) * iw + sx) * 4;
            const int sa = (static_cast<int>(t[3]) * alpha + 127) / 255;
            if (sa == 0) continue;
            blend_pixel(x, y,
                        Color::rgba(static_cast<std::uint8_t>((t[0] * tint.r + 127) / 255),
                                    static_cast<std::uint8_t>((t[1] * tint.g + 127) / 255),
                                    static_cast<std::uint8_t>((t[2] * tint.b + 127) / 255),
                                    static_cast<std::uint8_t>(sa)));
        }
    }
}

void Canvas::text(const Fonts& fonts, std::string_view utf8, Point origin, float size, Color col,
                  float angle, TextAnchor anchor, FontStyle style) {
    if (utf8.empty() || size <= 0.f) return;
    const float w = fonts.width(utf8, size, style);
    const float h = fonts.line_height(size, style);
    float bx = origin.x.raw(), by = origin.y.raw();
    if (anchor == TextAnchor::Center) {
        bx -= w / 2.f;
        by -= h / 2.f;
    }
    // simplify: angle != 0 is drawn unrotated (the SDL backend rotates the rasterized text box
    // around its pivot). Upgrade: rotate the pen per glyph -- exact for 90-degree multiples.
    (void)angle;
    const float baseline = by + fonts.ascent(size, style);
    const int face = Fonts::face_of(style);
    float pen = bx;
    std::uint32_t prev = 0;
    for (std::size_t i = 0; i < utf8.size();) {
        const std::size_t next = prism::core::utf8::next(utf8, i);
        const std::uint32_t cp = decode_codepoint(utf8, i);
        if (prev != 0) pen += fonts.kern(face, prev, cp, size);
        const auto g = fonts.glyph(face, cp, size);
        if (g && g->w > 0 && g->h > 0)
            blend_bitmap(static_cast<int>(std::lround(pen + g->xoff)),
                         static_cast<int>(std::lround(baseline + g->yoff)),
                         g->w, g->h, g->bitmap.data(), col);
        pen += g ? g->advance : fonts.advance(face, cp, size);
        prev = cp;
        i = next;
    }
}

void Canvas::blend_pixel(int x, int y, Color c) {
    if (x < clip_.x0 || x >= clip_.x1 || y < clip_.y0 || y >= clip_.y1) return;
    std::uint32_t& px = pixels_[static_cast<std::size_t>(y) * stride_ + x];
    px = over(px, c.r, c.g, c.b, c.a);
}

void Canvas::blend_bitmap(int x, int y, int w, int h, const std::uint8_t* coverage, Color c) {
    for (int j = 0; j < h; ++j) {
        for (int i = 0; i < w; ++i) {
            const int a = (static_cast<int>(c.a) * coverage[static_cast<std::size_t>(j) * w + i] + 127) / 255;
            if (a == 0) continue;
            blend_pixel(x + i, y + j,
                        Color::rgba(c.r, c.g, c.b, static_cast<std::uint8_t>(a)));
        }
    }
}

} // namespace prism::raster
