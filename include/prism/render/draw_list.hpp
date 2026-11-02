#pragma once

#include <prism/core/types.hpp>

#include <algorithm>
#include <cstdint>
#include <span>
#include <memory>
#include <expected>
#include <atomic>
#include <limits>
#include <string>
#include <variant>
#include <vector>

namespace prism::render {
using namespace prism::core;


struct FilledRect {
    Rect rect;
    Color color;
};

struct RectOutline {
    Rect rect;
    Color color;
    float thickness;
};

enum class TextAnchor { TopLeft, Center };

// Which font a backend draws the text with. Ui: proportional (labels, buttons, fields).
// Mono: fixed advance (code, text areas). Each backend maps roles to its own fonts.
enum class FontRole : std::uint8_t { Ui, Mono };

struct TextCmd {
    std::string text;
    Point origin;
    float size;
    Color color;
    float angle = 0.f;       // degrees, counter-clockwise
    TextAnchor anchor = TextAnchor::TopLeft;
    FontRole role = FontRole::Ui;
};

struct ClipPush {
    Rect rect;
};

struct ClipPop {};

struct RoundedRect {
    Rect rect;
    Color color;
    float radius;
    float thickness;  // 0 = filled, >0 = stroke only
};

struct Line {
    Point from;
    Point to;
    Color color;
    float thickness;
};

struct Polyline {
    std::vector<Point> points;
    Color color;
    float thickness;
};

struct Circle {
    Point center;
    float radius;
    Color color;
    float thickness;  // 0 = filled, >0 = stroke only
};

struct FilledPolygon {
    // Vertices in triangle-strip order: triangle i is (i, i+1, i+2) for every
    // i in [0, points.size()-3] -- the same generic primitive as a GPU triangle strip.
    std::vector<Point> points;
    Color color;
};

// Immutable RGBA8 pixels (row-major, 4 bytes per pixel, straight alpha). Shared by pointer: draw
// lists hand them to the render thread, and each backend uploads an image once, keyed by id().
// Format-free: files (PNG, SVG, ...) are decoded into it by render/image_decoder.hpp.
class Image {
public:
    static std::expected<std::shared_ptr<const Image>, std::string>
    from_rgba(int width, int height, std::vector<std::uint8_t> rgba) {
        if (width <= 0 || height <= 0) return std::unexpected("image size must be positive");
        if (rgba.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4)
            return std::unexpected("pixel data does not match width x height x 4");
        return std::shared_ptr<const Image>(new Image(width, height, std::move(rgba)));
    }

    int width() const { return width_; }
    int height() const { return height_; }
    std::span<const std::uint8_t> pixels() const { return pixels_; }
    std::uint64_t id() const { return id_; }

private:
    Image(int w, int h, std::vector<std::uint8_t> px) : width_(w), height_(h), pixels_(std::move(px)) {}
    static std::uint64_t next_id() {
        static std::atomic<std::uint64_t> counter{1};
        return counter.fetch_add(1, std::memory_order_relaxed);
    }
    int width_, height_;
    std::vector<std::uint8_t> pixels_;
    std::uint64_t id_ = next_id();
};

struct ImageCmd {
    std::shared_ptr<const Image> image;
    Rect rect;   // destination; the image is scaled to fit
};

using DrawCmd = std::variant<FilledRect, RectOutline, TextCmd, ClipPush, ClipPop,
                             RoundedRect, Line, Polyline, Circle, FilledPolygon, ImageCmd>;

struct DrawList {
    std::vector<DrawCmd> commands;

    void filled_rect(Rect r, Color c)
    {
        commands.emplace_back(FilledRect{translated(r), c});
    }

    void rect_outline(Rect r, Color c, float thickness = 1.0f)
    {
        commands.emplace_back(RectOutline{translated(r), c, thickness});
    }

    void circle(Point center, float radius, Color c, float thickness = 0.f)
    {
        commands.emplace_back(Circle{translated(center), radius, c, thickness});
    }

    void polyline(std::vector<Point> pts, Color c, float thickness = 1.f)
    {
        for (auto& p : pts)
            p = translated(p);
        commands.emplace_back(Polyline{std::move(pts), c, thickness});
    }

    void filled_polygon(std::vector<Point> pts, Color c)
    {
        for (auto& p : pts)
            p = translated(p);
        commands.emplace_back(FilledPolygon{std::move(pts), c});
    }

    void line(Point from, Point to, Color c, float thickness = 1.f)
    {
        commands.emplace_back(Line{translated(from), translated(to), c, thickness});
    }

    void rounded_rect(Rect r, Color c, float radius, float thickness = 0.f)
    {
        commands.emplace_back(RoundedRect{translated(r), c, radius, thickness});
    }

    void text(std::string s, Point origin, float size, Color c,
              float angle = 0.f, TextAnchor anchor = TextAnchor::TopLeft,
              FontRole role = FontRole::Ui)
    {
        commands.emplace_back(
            TextCmd{std::move(s), translated(origin), size, c, angle, anchor, role});
    }

    void image(std::shared_ptr<const Image> img, Rect r)
    {
        commands.emplace_back(ImageCmd{std::move(img), translated(r)});
    }

    void clip_push(Point origin, Size extent)
    {
        Point abs = translated(origin);
        origin_stack_.push_back(Offset{DX{abs.x.raw()}, DY{abs.y.raw()}});
        commands.emplace_back(ClipPush{{abs, extent}});
    }


    void clip_pop()
    {
        if (!origin_stack_.empty()) origin_stack_.pop_back();
        commands.emplace_back(ClipPop{});
    }

    void clear()
    {
        commands.clear();
        origin_stack_.clear();
    }

    [[nodiscard]] bool empty() const { return commands.empty(); }
    [[nodiscard]] std::size_t size() const { return commands.size(); }

    [[nodiscard]] Rect bounding_box() const {
        if (commands.empty()) return {Point{X{0}, Y{0}}, Size{Width{0}, Height{0}}};
        float min_x = std::numeric_limits<float>::max();
        float min_y = std::numeric_limits<float>::max();
        float max_x = std::numeric_limits<float>::lowest();
        float max_y = std::numeric_limits<float>::lowest();
        auto expand = [&](Rect r) {
            min_x = std::min(min_x, r.origin.x.raw());
            min_y = std::min(min_y, r.origin.y.raw());
            max_x = std::max(max_x, r.origin.x.raw() + r.extent.w.raw());
            max_y = std::max(max_y, r.origin.y.raw() + r.extent.h.raw());
        };
        for (const auto& cmd : commands) {
            std::visit([&](const auto& c) {
                if constexpr (requires { c.rect; })
                    expand(c.rect);
                else if constexpr (requires { c.center; c.radius; }) {
                    expand({Point{X{c.center.x.raw() - c.radius}, Y{c.center.y.raw() - c.radius}},
                            Size{Width{2 * c.radius}, Height{2 * c.radius}}});
                }
                else if constexpr (requires { c.points; }) {
                    for (const auto& p : c.points)
                        expand({p, Size{Width{0}, Height{0}}});
                }
                else if constexpr (requires { c.from; c.to; }) {
                    expand({c.from, Size{Width{0}, Height{0}}});
                    expand({c.to, Size{Width{0}, Height{0}}});
                }
                else if constexpr (requires { c.origin; })
                    expand({c.origin, Size{Width{0}, Height{c.size}}});
            }, cmd);
        }
        return {Point{X{min_x}, Y{min_y}},
                Size{Width{max_x - min_x}, Height{max_y - min_y}}};
    }

    // Rough heap footprint: command storage plus the string/point payloads
    // commands own. Deliberately approximate (uses capacity, not size, and
    // ignores variant/allocator overhead) -- good enough to compare snapshots
    // relatively, not a precise allocator accounting.
    [[nodiscard]] std::size_t approx_bytes() const {
        std::size_t total = commands.capacity() * sizeof(DrawCmd);
        for (const auto& cmd : commands) {
            std::visit([&](const auto& c) {
                if constexpr (requires { c.text; }) total += c.text.capacity();
                if constexpr (requires { c.points; })
                    total += c.points.capacity() * sizeof(Point);
            }, cmd);
        }
        return total;
    }

  private:
    std::vector<Offset> origin_stack_;

    [[nodiscard]] Offset current_offset() const
    {
        return origin_stack_.empty() ? Offset{DX{0.f}, DY{0.f}} : origin_stack_.back();
    }

    [[nodiscard]] Point translated(Point p) const
    {
        auto o = current_offset();
        return Point{p.x + o.dx, p.y + o.dy};
    }

    [[nodiscard]] Rect translated(Rect r) const
    {
        return Rect{translated(r.origin), r.extent};
    }
};

} // namespace prism::render
