#include <prism/render/view_transform.hpp>

#include <variant>

namespace prism::render {

namespace {

float about(float origin, float v, float scale) { return origin + scale * (v - origin); }

void scale_point(core::Point& p, const ViewTransform& t) { p = t.apply(p); }
void scale_rect(core::Rect& r, const ViewTransform& t) { r = t.apply(r); }

void scale_cmd(DrawCmd& cmd, const ViewTransform& t) {
    std::visit([&t](auto& c) {
        if constexpr (requires { c.rect; }) scale_rect(c.rect, t);
        if constexpr (requires { c.center; }) scale_point(c.center, t);
        if constexpr (requires { c.from; c.to; }) { scale_point(c.from, t); scale_point(c.to, t); }
        if constexpr (requires { c.points; }) for (auto& p : c.points) scale_point(p, t);
        if constexpr (requires { c.origin; }) scale_point(c.origin, t);
        if constexpr (requires { c.radius; }) c.radius *= t.scale;
        if constexpr (requires { c.thickness; }) c.thickness *= t.scale;
        if constexpr (requires { c.size; }) c.size *= t.scale;
    }, cmd);
}

} // namespace

core::Point ViewTransform::apply(core::Point q) const {
    return {core::X{about(origin.x.raw(), q.x.raw(), scale)}, core::Y{about(origin.y.raw(), q.y.raw(), scale)}};
}

core::Rect ViewTransform::apply(core::Rect r) const {
    return {apply(r.origin), {core::Width{r.extent.w.raw() * scale}, core::Height{r.extent.h.raw() * scale}}};
}

void scale_draw_list(DrawList& dl, const ViewTransform& t) {
    for (auto& cmd : dl.commands) scale_cmd(cmd, t);
}

} // namespace prism::render
