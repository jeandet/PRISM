#include <prism/delegates/tabs_delegates.hpp>

#include <prism/ui/widget_node.hpp>

namespace prism::ui {

namespace tabs_detail {

void tabs_record(DrawList& dl, WidgetNode& node,
                 size_t selected, const std::vector<std::string>& names) {
    auto& vs = node_vs(node);
    auto& es = node.get_or_create<TabBarEditState>();
    auto& t = *node.theme;

    auto tab_w = [&](const std::string& name) {
        return tab_padding * 2.f + Width{text_width(t.measurer, name, tab_font_size)};
    };
    Width total_w{0};
    for (auto& name : names)
        total_w += tab_w(name);

    dl.filled_rect(delegate_detail::make_rect(X{0}, Y{0}, total_w, tab_h), t.tab_bar_bg);

    es.header_x_ranges.clear();
    es.header_x_ranges.reserve(names.size());
    X x{0};
    for (size_t i = 0; i < names.size(); ++i) {
        Width w = tab_w(names[i]);
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

        if (is_selected)
            dl.filled_rect(delegate_detail::make_rect(x, Y{(tab_h - tab_accent_h).raw()}, w, tab_accent_h),
                           t.tab_accent);
        x += DX{w.raw()};
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
        if (hover != es.hovered_tab) {
            es.hovered_tab = hover;
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
