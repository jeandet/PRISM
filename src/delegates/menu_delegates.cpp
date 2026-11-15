#include <prism/delegates/menu_delegates.hpp>

#include <prism/core/utf8.hpp>
#include <prism/ui/widget_node.hpp>

#include <algorithm>
#include <any>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace prism::ui {

namespace menu_detail {

float row_top(const Items& items, std::size_t i) {
    float y = 0.f;
    for (std::size_t k = 0; k < i && k < items.size(); ++k) y += row_h(items[k]).raw();
    return y;
}

float shortcut_column_w(const Items& items, const TextMeasurer* m) {
    float w = widest(items, m, [](const MenuItem& i) {
        return opens_submenu(i) ? std::string(submenu_arrow) : shortcut_text(i.shortcut);
    });
    return w > 0.f ? shortcut_gap + w : 0.f;
}

Size popup_size(const Items& items, const WidgetNode& n, float min_w) {
    const auto* m = node_theme(n).measurer;
    float labels = widest(items, m, [](const MenuItem& i) { return parse_mnemonic(i.text).text; });
    return Size{Width{std::max(label_offset(items) + labels + shortcut_column_w(items, m) + pad, min_w)},
                Height{row_top(items, items.size())}};
}

const Items& panel_items(const Items& items, const Path& path, std::size_t level) {
    const Items* at = &items;
    for (std::size_t k = 0; k < level; ++k) at = &(*at)[path[k]].items;
    return *at;
}

std::vector<Rect> panels(const Items& items, const WidgetNode& node, const Path& path) {
    std::vector<Rect> out{*node.popup_area};
    for (std::size_t level = 1; level < path.size(); ++level) {
        const Rect& up = out.back();
        const auto& above = panel_items(items, path, level - 1);
        Point at{up.origin.x + DX{up.extent.w.raw()}, up.origin.y + DY{row_top(above, path[level - 1])}};
        out.push_back(Rect{at, popup_size(panel_items(items, path, level), node, 0.f)});
    }
    return out;
}

std::optional<Hit> hit(const Items& items, const WidgetNode& node, const Path& path, Point p) {
    auto rects = panels(items, node, path);
    for (std::size_t level = rects.size(); level-- > 0;) {
        if (!rects[level].contains(p)) continue;
        const auto& its = panel_items(items, path, level);
        float y = p.y.raw() - rects[level].origin.y.raw();
        for (std::size_t i = 0; i < its.size(); ++i) {
            float top = row_top(its, i);
            if (y >= top && y < top + row_h(its[i]).raw())
                return Hit{level, selectable(its[i]) ? std::optional{i} : std::nullopt};
        }
        return Hit{level, std::nullopt};
    }
    return std::nullopt;
}

std::optional<std::size_t> step(const Items& items, std::size_t from, int dir) {
    for (auto i = static_cast<std::ptrdiff_t>(from) + dir; i >= 0 && i < std::ssize(items); i += dir)
        if (selectable(items[static_cast<std::size_t>(i)])) return static_cast<std::size_t>(i);
    return std::nullopt;
}

std::size_t first_selectable(const Items& items) {
    if (!items.empty() && selectable(items[0])) return 0;
    return step(items, 0, +1).value_or(0);
}

std::optional<std::size_t> item_for_key(const Items& items, std::int32_t key) {
    for (std::size_t i = 0; i < items.size(); ++i)
        if (key != 0 && selectable(items[i]) && mnemonic_key(items[i].text) == key) return i;
    return std::nullopt;
}

std::optional<Path> path_for_shortcut(const Items& items, const KeyPress& kp) {
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (!selectable(items[i])) continue;
        if (opens_submenu(items[i])) {
            if (auto sub = path_for_shortcut(items[i].items, kp)) {
                sub->insert(sub->begin(), i);
                return sub;
            }
        } else if (matches(items[i].shortcut, kp)) {
            return Path{i};
        }
    }
    return std::nullopt;
}

void sync_panels(const Items& items, WidgetNode& node) {
    auto& es = node.get_or_create<MenuEditState>();
    if (es.path.empty()) es.path = {first_selectable(items)};
    const Items* at = &items;
    for (std::size_t level = 0; level < es.path.size(); ++level) {
        if (at->empty()) { es.path.resize(level); break; }
        es.path[level] = std::min(es.path[level], at->size() - 1);
        if (level + 1 < es.path.size()) {
            const auto& opener = (*at)[es.path[level]];
            if (!selectable(opener) || !opens_submenu(opener)) { es.path.resize(level + 1); break; }
            at = &opener.items;
        }
    }
    if (es.path.empty()) es.path = {0};
    const auto& last = panel_items(items, es.path, es.path.size() - 1);
    es.opener_highlighted = es.path.back() < last.size() && selectable(last[es.path.back()])
                         && opens_submenu(last[es.path.back()]);
    auto rects = panels(items, node, es.path);
    node.popup_more.assign(rects.begin() + 1, rects.end());
}

void open_at(const Items& items, WidgetNode& node, Rect anchor, float min_w) {
    Point below{anchor.origin.x, anchor.origin.y + DY{anchor.extent.h.raw()}};
    node.popup_area = Rect{below, popup_size(items, node, min_w)};
    node.popup_opened_from = anchor;
    node.get_or_create<MenuEditState>() = MenuEditState{.path = {first_selectable(items)}};
    sync_panels(items, node);
    node.dirty = true;
}

void close(WidgetNode& node) {
    node.popup_area.reset();
    node.popup_opened_from.reset();
    node.popup_more.clear();
    node.overlay_draws.clear();
    node.dirty = true;
}

void highlight(const Items& items, WidgetNode& node, std::size_t level, std::size_t i) {
    auto& path = node.get_or_create<MenuEditState>().path;
    path.resize(level + 1);
    path[level] = i;
    const auto& item = panel_items(items, path, level)[i];
    if (opens_submenu(item)) path.push_back(first_selectable(item.items));
    sync_panels(items, node);
    node.dirty = true;
}

void draw_label(DrawList& dl, std::string_view label, Point at, Color color, const TextMeasurer* m) {
    auto mn = parse_mnemonic(label);
    dl.text(mn.text, at, font_size, color);
    if (mnemonic_key(label) == 0) return;   // no key can trigger it: do not promise one
    std::string_view text = mn.text;
    float x0 = text_width(m, text.substr(0, mn.underline), font_size);
    float x1 = text_width(m, text.substr(0, core::utf8::next(text, mn.underline)), font_size);
    Y under = at.y + DY{font_size + 2.f};
    dl.line(Point{at.x + DX{x0}, under}, Point{at.x + DX{x1}, under}, color, 1.f);
}

void draw_item(DrawList& dl, const Items& items, std::size_t i, Rect row, bool highlighted, const Theme& t) {
    const MenuItem& item = items[i];
    if (item.separator) {
        Y mid = row.origin.y + DY{row.extent.h.raw() / 2};
        dl.line(Point{row.origin.x + DX{pad}, mid}, Point{row.origin.x + DX{row.extent.w.raw() - pad}, mid}, t.popup_border, 1.f);
        return;
    }
    if (highlighted) dl.filled_rect(row, t.popup_highlight);
    Color color = !item.enabled ? t.text_placeholder : highlighted ? t.text_on_primary : t.text;
    X slot = row.origin.x + DX{pad};
    Y icon_y = row.origin.y + DY{(item_h.raw() - icon_size) / 2};
    if (item.icon) dl.image(item.icon, Rect{Point{slot, icon_y}, Size{Width{icon_size}, Height{icon_size}}}, icon_tint(item.icon, color));
    else if (item.checked.value_or(false)) dl.text("\xe2\x9c\x93", Point{slot + DX{2.f}, row.origin.y + DY{6.f}}, font_size, color);
    draw_label(dl, item.text, Point{row.origin.x + DX{label_offset(items)}, row.origin.y + DY{6.f}}, color, t.measurer);
    auto right = opens_submenu(item) ? std::string(submenu_arrow) : shortcut_text(item.shortcut);
    if (!right.empty()) {
        float w = text_width(t.measurer, right, font_size);
        dl.text(right, Point{row.origin.x + DX{row.extent.w.raw() - pad - w}, row.origin.y + DY{6.f}}, font_size,
                item.enabled && !highlighted ? t.text_muted : color);
    }
}

void draw_panel(DrawList& dl, const Items& items, Rect area, std::size_t highlighted, const Theme& t) {
    dl.filled_rect(area, t.popup_bg);
    dl.rect_outline(area, t.popup_border, 1.f);
    for (std::size_t i = 0; i < items.size(); ++i) {
        Rect row{Point{area.origin.x, area.origin.y + DY{row_top(items, i)}}, Size{area.extent.w, row_h(items[i])}};
        draw_item(dl, items, i, row, i == highlighted && selectable(items[i]), t);
    }
}

void draw_popup(const Items& items, WidgetNode& node) {
    sync_panels(items, node);
    const auto& path = node.get_or_create<MenuEditState>().path;
    auto rects = panels(items, node, path);
    for (std::size_t level = 0; level < rects.size(); ++level)
        draw_panel(node.overlay_draws, panel_items(items, path, level), rects[level], path[level], *node.theme);
}

bool heading_for_submenu(const std::vector<Rect>& rects, const Hit& h, Point from, Point to) {
    if (rects.size() < 2 || h.level != rects.size() - 2) return false;
    const Rect& sub = rects.back();
    float ax = from.x.raw(), ay = from.y.raw();
    float bx = sub.origin.x.raw(), by = sub.origin.y.raw();
    float cy = by + sub.extent.h.raw();
    auto side = [&](float x1, float y1, float x2, float y2) {
        return (x2 - x1) * (to.y.raw() - y1) - (y2 - y1) * (to.x.raw() - x1);
    };
    float d1 = side(ax, ay, bx, by), d2 = side(bx, by, bx, cy), d3 = side(bx, cy, ax, ay);
    bool has_neg = d1 < 0 || d2 < 0 || d3 < 0, has_pos = d1 > 0 || d2 > 0 || d3 > 0;
    return !(has_neg && has_pos);
}

bool keeps_key(const WidgetNode& node, std::int32_t key) {
    auto* es = std::any_cast<MenuEditState>(&node.edit_state);
    if (!es) return false;
    if (key == keys::right) return es->opener_highlighted;
    if (key == keys::left || key == keys::escape) return es->path.size() > 1;
    return false;
}
} // namespace menu_detail

void Widget<Menu>::record(DrawList& dl, const Field<Menu>& field, WidgetNode& node) {
    using namespace menu_detail;
    const auto& m = field.get();
    auto& vs = node_vs(node);
    auto& t = *node.theme;
    Width w = delegate_detail::widget_w(node);
    bool is_open = node.popup_area.has_value();
    dl.filled_rect(delegate_detail::make_rect(X{0}, Y{0}, w, title_h),
                   is_open ? t.surface_active : vs.hovered ? t.surface_hover : t.surface);
    draw_label(dl, m.title, delegate_detail::make_point(X{pad}, Y{7}), t.text, t.measurer);
    if (vs.focused) delegate_detail::draw_focus_ring(dl, w, title_h, t);
    node.overlay_draws.clear();
    if (is_open) draw_popup(m.items, node);
}

void Widget<Menu>::handle_input(Field<Menu>& field, const InputEvent& ev, WidgetNode& node) {
    using namespace menu_detail;
    const auto& m = field.get();
    auto open = [&] {
        Width w = delegate_detail::widget_w(node);
        open_at(m.items, node, Rect{Point{X{0}, Y{0}}, Size{w, title_h}}, w.raw());
    };
    if (node.popup_area && open_popup_input(field, ev, node)) return;
    if (auto* mb = std::get_if<MouseButton>(&ev); mb && mb->pressed && mb->button == buttons::left) {
        if (node.popup_area) close(node);   // the title again: toggle closed
        else open();
    } else if (auto* kp = std::get_if<KeyPress>(&ev); kp && !node.popup_area) {
        if (kp->key == keys::enter || kp->key == keys::space || kp->key == keys::down) open();
    }
}

void Widget<ContextMenu>::handle_input(Field<ContextMenu>& field, const InputEvent& ev, WidgetNode& node) {
    using namespace menu_detail;
    if (node.popup_area) {
        open_popup_input(field, ev, node);
        return;
    }
    if (auto* mb = std::get_if<MouseButton>(&ev); mb && mb->pressed && mb->button == buttons::right)
        open_at(field.get().items, node, Rect{mb->position, Size{Width{0}, Height{0}}}, 120.f);
}

} // namespace prism::ui