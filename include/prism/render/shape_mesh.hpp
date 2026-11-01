#pragma once

#include <prism/core/types.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <span>
#include <vector>

// Triangle meshes for shapes a renderer cannot draw directly (rounded rectangles, smooth
// circles), with a 1 px fringe whose alpha falls to 0: interpolated by the rasterizer, it
// anti-aliases the edge without multisampling. Backend-neutral: any backend that can draw
// coloured triangles with alpha blending (SDL_RenderGeometry, a GPU vertex buffer) uses these.
namespace prism::render {

// Mesh-level vertex, like a GPU vertex: raw floats on purpose (the strong coordinate types are
// for the drawing API; this is what a backend uploads). alpha scales the shape's colour alpha.
struct Vec2 {
    float x, y;
};
struct ShapeVertex {
    float x, y, alpha;
};
struct ShapeMesh {
    std::vector<ShapeVertex> vertices;
    std::vector<int> indices;   // triangles
};

// Segments for a quarter-circle arc of this radius: roughly one per 2 px of arc, capped.
inline int arc_segments(float radius) {
    return std::clamp(static_cast<int>(std::ceil(radius * std::numbers::pi_v<float> / 4.f)), 2, 64);
}

inline std::vector<Vec2> circle_outline(core::Point center, float radius, int segments) {
    std::vector<Vec2> pts;
    pts.reserve(static_cast<std::size_t>(segments));
    for (int i = 0; i < segments; ++i) {
        float a = 2.f * std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(segments);
        pts.push_back({center.x.raw() + radius * std::cos(a), center.y.raw() + radius * std::sin(a)});
    }
    return pts;
}

// Clockwise on screen (y down): top-right, bottom-right, bottom-left, top-left corners.
inline std::vector<Vec2> rounded_rect_outline(core::Rect r, float radius) {
    float x0 = r.origin.x.raw(), y0 = r.origin.y.raw();
    float x1 = x0 + r.extent.w.raw(), y1 = y0 + r.extent.h.raw();
    radius = std::clamp(radius, 0.f, std::min(r.extent.w.raw(), r.extent.h.raw()) / 2.f);
    int seg = arc_segments(radius);
    struct Corner { float cx, cy, start; };
    const Corner corners[] = {{x1 - radius, y0 + radius, -0.5f}, {x1 - radius, y1 - radius, 0.f},
                              {x0 + radius, y1 - radius, 0.5f}, {x0 + radius, y0 + radius, 1.f}};
    std::vector<Vec2> pts;
    for (auto& c : corners)
        for (int i = 0; i <= seg; ++i) {
            float a = std::numbers::pi_v<float> * (c.start + 0.5f * static_cast<float>(i) / static_cast<float>(seg));
            pts.push_back({c.cx + radius * std::cos(a), c.cy + radius * std::sin(a)});
        }
    return pts;
}

namespace shape_detail {

// Outward unit normal at each vertex of a closed convex outline: the average of the two
// adjacent edge normals (smooth outlines like these have no sharp corners to miter).
inline std::vector<Vec2> vertex_normals(std::span<const Vec2> pts) {
    std::size_t n = pts.size();
    float area2 = 0.f;
    for (std::size_t i = 0; i < n; ++i)
        area2 += pts[i].x * pts[(i + 1) % n].y - pts[(i + 1) % n].x * pts[i].y;
    float out = area2 > 0.f ? 1.f : -1.f;   // which side of an edge is outside
    auto edge_normal = [&](Vec2 a, Vec2 b) {
        float dx = b.x - a.x, dy = b.y - a.y, len = std::hypot(dx, dy);
        return len > 0.f ? Vec2{out * dy / len, -out * dx / len} : Vec2{0.f, 0.f};
    };
    std::vector<Vec2> normals(n);
    for (std::size_t i = 0; i < n; ++i) {
        Vec2 a = edge_normal(pts[(i + n - 1) % n], pts[i]), b = edge_normal(pts[i], pts[(i + 1) % n]);
        float x = a.x + b.x, y = a.y + b.y, len = std::hypot(x, y);
        normals[i] = len > 0.f ? Vec2{x / len, y / len} : a;
    }
    return normals;
}

// Quads between ring `a` and ring `b` (both n vertices, starting at the given indices).
inline void join_rings(std::vector<int>& idx, int a, int b, int n) {
    for (int i = 0; i < n; ++i) {
        int j = (i + 1) % n;
        idx.insert(idx.end(), {a + i, a + j, b + i, b + i, a + j, b + j});
    }
}

} // namespace shape_detail

// Solid convex shape plus an outward anti-aliasing fringe of width `feather`.
inline ShapeMesh fill_convex(std::span<const Vec2> outline, float feather = 1.f) {
    ShapeMesh m;
    int n = static_cast<int>(outline.size());
    if (n < 3) return m;
    auto normals = shape_detail::vertex_normals(outline);
    Vec2 c{0.f, 0.f};
    for (auto& p : outline) { c.x += p.x / static_cast<float>(n); c.y += p.y / static_cast<float>(n); }
    m.vertices.push_back({c.x, c.y, 1.f});
    for (auto& p : outline) m.vertices.push_back({p.x, p.y, 1.f});
    for (int i = 0; i < n; ++i)
        m.vertices.push_back({outline[i].x + normals[i].x * feather, outline[i].y + normals[i].y * feather, 0.f});
    for (int i = 0; i < n; ++i) m.indices.insert(m.indices.end(), {0, 1 + i, 1 + (i + 1) % n});
    shape_detail::join_rings(m.indices, 1, 1 + n, n);
    return m;
}

// Band from the outline inward by `thickness`, with fringes on both sides.
inline ShapeMesh stroke_closed(std::span<const Vec2> outline, float thickness, float feather = 1.f) {
    ShapeMesh m;
    int n = static_cast<int>(outline.size());
    if (n < 3) return m;
    auto normals = shape_detail::vertex_normals(outline);
    auto ring = [&](float offset, float alpha) {   // offset along the outward normal
        for (int i = 0; i < n; ++i)
            m.vertices.push_back({outline[i].x + normals[i].x * offset, outline[i].y + normals[i].y * offset, alpha});
    };
    ring(feather, 0.f);                  // 0: outer fringe
    ring(0.f, 1.f);                      // n: outline
    ring(-thickness, 1.f);               // 2n: inner edge
    ring(-thickness - feather, 0.f);     // 3n: inner fringe
    shape_detail::join_rings(m.indices, 0, n, n);
    shape_detail::join_rings(m.indices, n, 2 * n, n);
    shape_detail::join_rings(m.indices, 2 * n, 3 * n, n);
    return m;
}

} // namespace prism::render
