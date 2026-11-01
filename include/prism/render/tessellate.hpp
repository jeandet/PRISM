#pragma once

#include <prism/render/scene_snapshot.hpp>
#include <prism/render/shape_mesh.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <optional>
#include <type_traits>
#include <variant>
#include <vector>

// A snapshot as triangles, split into batches wherever the clip or the texture changes: what a
// GPU backend uploads and draws (one vertex buffer, a scissor rect and a texture per batch).
// Shapes are tessellated here; a text or image batch only says where its quad goes, because
// its size comes from the bitmap the backend rasterizes. Backend-neutral and pure, so it is
// tested without a GPU. Coordinates stay in points; the backend scales by the pixel density.
namespace prism::render {

struct Vertex {
    float x, y;
    float u, v;         // texture coordinates; shapes sample a white texel
    float r, g, b, a;   // straight (not premultiplied) alpha, 0..1; multiplies the texture
};
// GPU vertex layouts read these offsets: a reordered or padded field would shift colours.
static_assert(std::is_standard_layout_v<Vertex> && sizeof(Vertex) == 8 * sizeof(float));

struct Batch {
    std::optional<core::Rect> clip;   // none: the whole target
    std::uint32_t first = 0;          // first vertex (shapes)
    std::uint32_t count = 0;          // vertices, a multiple of 3 (shapes)
    // What the batch draws: shapes (its vertices), or one text or image quad the backend builds.
    // Pointers into the snapshot being drawn.
    std::variant<std::monostate, const TextCmd*, const ImageCmd*> source;
};

struct Tessellation {
    std::vector<Vertex> vertices;
    std::vector<Batch> batches;
};

// The four corners (top-left, top-right, bottom-right, bottom-left) of a w x h point text box,
// as the SDL backend places it: from the origin, or centred on it for TextAnchor::Center; a
// non-zero angle turns it counter-clockwise (on screen) about the origin or the centre.
inline std::array<Vec2, 4> text_quad(const TextCmd& t, float w, float h) {
    float ox = t.origin.x.raw(), oy = t.origin.y.raw();
    bool centred = t.anchor == TextAnchor::Center;
    if (centred) { ox -= w / 2.f; oy -= h / 2.f; }
    std::array<Vec2, 4> q{{{ox, oy}, {ox + w, oy}, {ox + w, oy + h}, {ox, oy + h}}};
    if (t.angle == 0.f) return q;
    Vec2 pivot = centred ? Vec2{ox + w / 2.f, oy + h / 2.f} : Vec2{ox, oy};
    float rad = -t.angle * std::numbers::pi_v<float> / 180.f;   // y points down: CCW is negative
    float c = std::cos(rad), s = std::sin(rad);
    for (auto& p : q) {
        float dx = p.x - pivot.x, dy = p.y - pivot.y;
        p = {pivot.x + dx * c - dy * s, pivot.y + dx * s + dy * c};
    }
    return q;
}

namespace tess_detail {

struct Builder {
    Tessellation out;
    std::vector<core::Rect> clips;

    std::optional<core::Rect> clip() const { return clips.empty() ? std::nullopt : std::optional{clips.back()}; }

    // A new shapes batch, unless the last one is still an empty shapes batch (then it takes the clip).
    void begin_batch() {
        auto& bs = out.batches;
        if (!bs.empty() && bs.back().count == 0 && std::holds_alternative<std::monostate>(bs.back().source)) {
            bs.back().clip = clip();
            return;
        }
        bs.push_back(Batch{clip(), static_cast<std::uint32_t>(out.vertices.size()), 0, {}});
    }

    template <typename Cmd>
    void textured(const Cmd& c) {
        auto& bs = out.batches;
        if (!bs.empty() && bs.back().count == 0 && std::holds_alternative<std::monostate>(bs.back().source))
            bs.pop_back();
        bs.push_back(Batch{clip(), 0, 0, &c});
        begin_batch();
    }

    void vertex(float x, float y, Color c, float alpha = 1.f) {
        out.vertices.push_back({x, y, 0.f, 0.f, c.r / 255.f, c.g / 255.f, c.b / 255.f, c.a / 255.f * alpha});
        ++out.batches.back().count;
    }

    void triangle(Vec2 a, Vec2 b, Vec2 c, Color col) {
        vertex(a.x, a.y, col);
        vertex(b.x, b.y, col);
        vertex(c.x, c.y, col);
    }

    void quad(Vec2 a, Vec2 b, Vec2 c, Vec2 d, Color col) {   // a b c d around the quad
        triangle(a, b, c, col);
        triangle(a, c, d, col);
    }

    void rect(float x, float y, float w, float h, Color col) {
        if (w <= 0.f || h <= 0.f) return;
        quad({x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}, col);
    }

    void mesh(const ShapeMesh& m, Color col) {
        for (int i : m.indices) vertex(m.vertices[i].x, m.vertices[i].y, col, m.vertices[i].alpha);
    }

    // A straight segment as a quad `thickness` wide (at least one point).
    void segment(core::Point from, core::Point to, float thickness, Color col) {
        float dx = to.x.raw() - from.x.raw(), dy = to.y.raw() - from.y.raw();
        float len = std::hypot(dx, dy);
        if (len <= 0.f) return;
        float half = std::max(thickness, 1.f) / 2.f;
        float nx = -dy / len * half, ny = dx / len * half;
        float x0 = from.x.raw(), y0 = from.y.raw(), x1 = to.x.raw(), y1 = to.y.raw();
        quad({x0 + nx, y0 + ny}, {x1 + nx, y1 + ny}, {x1 - nx, y1 - ny}, {x0 - nx, y0 - ny}, col);
    }

    void add(const FilledRect& c) {
        rect(c.rect.origin.x.raw(), c.rect.origin.y.raw(), c.rect.extent.w.raw(), c.rect.extent.h.raw(), c.color);
    }
    void add(const RectOutline& c) {   // a box thinner than two strokes keeps its top and bottom only
        float x = c.rect.origin.x.raw(), y = c.rect.origin.y.raw();
        float w = c.rect.extent.w.raw(), h = c.rect.extent.h.raw(), t = std::max(c.thickness, 1.f);
        rect(x, y, w, t, c.color);
        rect(x, y + h - t, w, t, c.color);
        rect(x, y + t, t, h - 2 * t, c.color);
        rect(x + w - t, y + t, t, h - 2 * t, c.color);
    }
    void add(const RoundedRect& c) {
        if (c.radius <= 0.5f) {   // a square corner needs no mesh
            if (c.thickness > 0.f) add(RectOutline{c.rect, c.color, c.thickness});
            else add(FilledRect{c.rect, c.color});
            return;
        }
        auto outline = rounded_rect_outline(c.rect, c.radius);
        mesh(c.thickness > 0.f ? stroke_closed(outline, c.thickness) : fill_convex(outline), c.color);
    }
    void add(const Circle& c) {
        auto outline = circle_outline(c.center, c.radius, 4 * arc_segments(c.radius));
        mesh(c.thickness > 0.f ? stroke_closed(outline, c.thickness) : fill_convex(outline), c.color);
    }
    void add(const Line& c) { segment(c.from, c.to, c.thickness, c.color); }
    void add(const Polyline& c) {
        for (size_t i = 0; i + 1 < c.points.size(); ++i) segment(c.points[i], c.points[i + 1], c.thickness, c.color);
    }
    void add(const FilledPolygon& c) {   // triangle-strip order, as the SDL backend draws it
        auto p = [&](size_t i) { return Vec2{c.points[i].x.raw(), c.points[i].y.raw()}; };
        for (size_t i = 0; i + 2 < c.points.size(); ++i) triangle(p(i), p(i + 1), p(i + 2), c.color);
    }
    void add(const ClipPush& c) {   // clips arrive pre-intersected (resolve_clips)
        clips.push_back(c.rect);
        begin_batch();
    }
    void add(const ClipPop&) {
        if (!clips.empty()) clips.pop_back();
        begin_batch();
    }
    void add(const TextCmd& c) { if (!c.text.empty()) textured(c); }
    void add(const ImageCmd& c) { if (c.image) textured(c); }

    void draw_list(const DrawList& dl) {
        for (const auto& cmd : dl.commands) std::visit([&](const auto& c) { add(c); }, cmd);
    }
};

} // namespace tess_detail

// Back to front: the snapshot's z order, then the overlay (never clipped).
inline Tessellation tessellate(const SceneSnapshot& snap) {
    tess_detail::Builder b;
    b.begin_batch();
    for (auto idx : snap.z_order)
        if (idx < snap.draw_lists.size() && snap.draw_lists[idx]) b.draw_list(*snap.draw_lists[idx]);
    b.clips.clear();
    b.begin_batch();
    b.draw_list(snap.overlay);
    auto& bs = b.out.batches;
    if (!bs.empty() && bs.back().count == 0 && std::holds_alternative<std::monostate>(bs.back().source)) bs.pop_back();
    return std::move(b.out);
}

} // namespace prism::render
