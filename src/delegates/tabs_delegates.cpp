#include <prism/delegates/tabs_delegates.hpp>

#include <prism/ui/widget_node.hpp>

namespace prism::ui {

namespace tabs_detail {

namespace {
constexpr float close_w = 16.f, close_gap = 4.f;   // a close button, and the space before it

bool has_close(const TabBarEditState& es, size_t i) { return es.close && es.closable && es.closable(i); }

// A close button: an x of two lines, on a hover shade when the pointer is on it.
void draw_close(DrawList& dl, X x0, bool hovered, Color ink, const Theme& t) {
    const float cx = x0.raw() + close_w / 2.f, cy = tab_h.raw() / 2.f, arm = 3.5f;
    if (hovered) dl.rounded_rect(Rect{Point{x0, Y{cy - close_w / 2.f}}, Size{Width{close_w}, Height{close_w}}}, t.surface_hover, 3.f);
    dl.line(Point{X{cx - arm}, Y{cy - arm}}, Point{X{cx + arm}, Y{cy + arm}}, ink, 1.5f);
    dl.line(Point{X{cx - arm}, Y{cy + arm}}, Point{X{cx + arm}, Y{cy - arm}}, ink, 1.5f);
}
constexpr float detach_w = 20.f, detach_margin = 6.f;

// A detach button: a window with a smaller one stepping out of its corner.
void draw_detach(DrawList& dl, X x0, Color ink) {
    const float y0 = (tab_h.raw() - 10.f) / 2.f;
    dl.rect_outline(Rect{Point{x0 + DX{3.f}, Y{y0 + 3.f}}, Size{Width{9.f}, Height{9.f}}}, ink, 1.f);
    dl.rect_outline(Rect{Point{x0 + DX{8.f}, Y{y0 - 1.f}}, Size{Width{9.f}, Height{9.f}}}, ink, 1.f);
}
} // namespace

void tabs_record(DrawList& dl, WidgetNode& node,
                 size_t selected, const std::vector<std::string>& names) {
    auto& vs = node_vs(node);
    auto& es = node.get_or_create<TabBarEditState>();
    auto& t = *node.theme;

    std::vector<Width> widths;
    for (size_t i = 0; i < names.size(); ++i)
        widths.push_back(tab_padding * 2.f + Width{text_width(t.measurer, names[i], tab_font_size)}
                         + Width{has_close(es, i) ? close_w + close_gap - tab_padding.raw() / 2.f : 0.f});
    Width total_w{0};
    for (auto w : widths)
        total_w += w;

    dl.filled_rect(delegate_detail::make_rect(X{0}, Y{0}, total_w, tab_h), t.tab_bar_bg);

    es.header_x_ranges.clear();
    es.header_x_ranges.reserve(names.size());
    es.close_x_ranges.assign(names.size(), {X{0}, X{0}});
    X x{0};
    for (size_t i = 0; i < names.size(); ++i) {
        Width w = widths[i];
        es.header_x_ranges.push_back({x, x + DX{w.raw()}});

        bool is_selected = (i == selected);
        bool is_hovered = es.hovered_tab.has_value() && es.hovered_tab.value() == i;

        auto bg = is_selected ? t.tab_active_bg
                : is_hovered  ? t.surface_hover
                :               t.tab_bar_bg;
        dl.filled_rect(delegate_detail::make_rect(x, Y{0}, w, tab_h), bg);

        auto text_color = is_selected ? t.tab_text_active
                                      : t.tab_text;
        dl.text(names[i], delegate_detail::make_point(x + DX{tab_padding.raw()}, Y{8}), tab_font_size, text_color);

        if (has_close(es, i)) {
            const X cx0 = x + DX{w.raw() - tab_padding.raw() / 2.f - close_w};
            es.close_x_ranges[i] = {cx0, cx0 + DX{close_w}};
            draw_close(dl, cx0, es.close_hovered && is_hovered, text_color, t);
        }

        if (is_selected)
            dl.filled_rect(delegate_detail::make_rect(x, Y{(tab_h - tab_accent_h).raw()}, w, tab_accent_h),
                           t.tab_accent);
        x += DX{w.raw()};
    }

    if (es.detach) {
        const X x0{std::max(total_w.raw(), delegate_detail::widget_w(node).raw() - detach_w - detach_margin)};
        es.detach_x_range = {x0, x0 + DX{detach_w}};
        draw_detach(dl, x0, t.tab_text);
    }

    if (vs.focused)
        delegate_detail::draw_focus_ring(dl, total_w, tab_h, t);
}

std::optional<size_t> tab_at(const WidgetNode& bar, X x) {
    const auto& ranges = bar.peek<TabBarEditState>().header_x_ranges;
    for (size_t i = 0; i < ranges.size(); ++i)
        if (x >= ranges[i].first && x < ranges[i].second) return i;
    return std::nullopt;
}

size_t insertion_at(const WidgetNode& bar, X x) {
    const auto& ranges = bar.peek<TabBarEditState>().header_x_ranges;
    for (size_t i = 0; i < ranges.size(); ++i)
        if (x.raw() < (ranges[i].first.raw() + ranges[i].second.raw()) / 2.f) return i;
    return ranges.size();
}

Rect insertion_marker(const WidgetNode& bar, size_t index) {
    constexpr float w = 2.f;
    const auto& ranges = bar.peek<TabBarEditState>().header_x_ranges;
    const float x = ranges.empty() ? 0.f : index < ranges.size() ? ranges[index].first.raw() : ranges.back().second.raw();
    return Rect{Point{X{x - w / 2.f}, Y{0}}, Size{Width{w}, tab_h}};
}

bool tabs_handle_input(const InputEvent& ev, WidgetNode& node,
                       size_t selected, size_t count,
                       std::function<void(size_t)> select) {
    auto& es = node.get_or_create<TabBarEditState>();

    if (auto* mb = std::get_if<MouseButton>(&ev); mb && mb->pressed) {
        X mx = mb->position.x;
        for (size_t i = 0; i < es.header_x_ranges.size(); ++i) {   // a close button, or a middle click
            auto [x0, x1] = es.header_x_ranges[i];
            auto [c0, c1] = es.close_x_ranges.size() > i ? es.close_x_ranges[i] : std::pair{X{0}, X{0}};
            bool on_button = mb->button == buttons::left && mx >= c0 && mx < c1;
            bool middle = mb->button == buttons::middle && mx >= x0 && mx < x1;
            if ((on_button || middle) && has_close(es, i)) {
                es.close(i);
                node.dirty = true;
                return true;
            }
        }
        if (mb->button != buttons::left) return false;
        if (es.detach && mx >= es.detach_x_range.first && mx < es.detach_x_range.second && count > 0) {
            es.detach(selected);
            return true;
        }
        if (auto i = tab_at(node, mx); i && mb->clicks == 2 && es.detach) {
            es.detach(*i);
            return true;
        }
        for (size_t i = 0; i < es.header_x_ranges.size(); ++i) {
            auto [x0, x1] = es.header_x_ranges[i];
            if (mx >= x0 && mx < x1 && i != selected) {
                select(i);
                return true;
            }
        }
    } else if (auto* mm = std::get_if<MouseMove>(&ev)) {
        std::optional<size_t> hover;
        X mx = mm->position.x;
        for (size_t i = 0; i < es.header_x_ranges.size(); ++i) {
            auto [x0, x1] = es.header_x_ranges[i];
            if (mx >= x0 && mx < x1) { hover = i; break; }
        }
        bool on_close = hover && es.close_x_ranges.size() > *hover && mx >= es.close_x_ranges[*hover].first
                        && mx < es.close_x_ranges[*hover].second;
        if (hover != es.hovered_tab || on_close != es.close_hovered) {
            es.hovered_tab = hover;
            es.close_hovered = on_close;
            node.dirty = true;
        }
    } else if (auto* kp = std::get_if<KeyPress>(&ev)) {
        if (kp->key == keys::right) {
            select((selected + 1) % count);
            return true;
        } else if (kp->key == keys::left) {
            select((selected + count - 1) % count);
            return true;
        }
    }
    return false;
}

} // namespace tabs_detail

void Widget<TabBar<>>::record(DrawList& dl, const Field<TabBar<>>& field,
                                       WidgetNode& node) {
    if (node.tab_names && !node.tab_names->empty())
        tabs_detail::tabs_record(dl, node, field.get().selected, *node.tab_names);
}

void Widget<TabBar<>>::handle_input(Field<TabBar<>>& field, const InputEvent& ev,
                                              WidgetNode& node) {
    size_t count = node.tab_names ? node.tab_names->size() : 0;
    if (count == 0) return;
    tabs_detail::tabs_handle_input(ev, node, field.get().selected, count,
        [&](size_t idx) {
            auto tb = field.get();
            tb.selected = idx;
            field.set(tb);
        });
}

} // namespace prism::ui
