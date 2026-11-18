#include <prism/delegates/dropdown_delegates.hpp>

#include <prism/ui/popup_place.hpp>
#include <prism/ui/widget_node.hpp>

#include <functional>
#include <string>

namespace prism::ui {

namespace dropdown_detail {

void dropdown_record(DrawList& dl, WidgetNode& node,
                     const std::string& current_label,
                     const DropdownLabelResolver& resolver) {
    auto& vs = node_vs(node);
    auto& es = node.get_or_create<DropdownEditState>();
    Width w = delegate_detail::widget_w(node);

    auto& t = *node.theme;
    auto bg = es.open    ? t.surface_active
            : vs.hovered ? t.surface_hover
            : t.surface;
    dl.filled_rect(delegate_detail::make_rect(X{0}, Y{0}, w, dd_widget_h), bg);

    dl.text(current_label, delegate_detail::make_point(X{dd_padding.raw()}, Y{7}), dd_font_size, t.text);

    dl.text("\xe2\x96\xbe", delegate_detail::make_point(X{w.raw() - 20.f}, Y{7}), dd_font_size, t.text_muted);

    if (vs.focused)
        delegate_detail::draw_focus_ring(dl, w, dd_widget_h, t);

    node.overlay_draws.clear();
    if (es.open) {
        Height popup_h{static_cast<float>(resolver.count) * dd_option_h.raw()};

        // Below the box, above it when only that fits, else the roomier side, inside the window.
        // In its popup coordinates: natural size, below the box as it is drawn (in_popup).
        const float box_h = in_popup(node, Point{X{0}, Y{dd_widget_h.raw()}}).y.raw();
        Y popup_y{box_h};
        if (node.viewport_height.raw() > 0) {
            const float top = node.absolute_y.raw();
            popup_y = Y{place_on_axis(popup_h.raw(), top, top + box_h, node.viewport_height.raw()) - top};
        }

        node.overlay_draws.filled_rect(
            delegate_detail::make_rect(X{0}, popup_y, w, popup_h),
            t.popup_bg);
        node.overlay_draws.rect_outline(
            delegate_detail::make_rect(X{0}, popup_y, w, popup_h),
            t.popup_border, 1.0f);

        for (size_t i = 0; i < resolver.count; ++i) {
            Y y{popup_y.raw() + static_cast<float>(i) * dd_option_h.raw()};
            if (i == es.highlighted) {
                node.overlay_draws.filled_rect(
                    delegate_detail::make_rect(X{0}, y, w, dd_option_h),
                    t.popup_highlight);
            }
            node.overlay_draws.text(
                resolver.label_at(i),
                delegate_detail::make_point(X{dd_padding.raw()}, y + DY{6.f}), dd_font_size,
                i == es.highlighted ? t.text_on_primary
                                    : t.text);
        }

        es.popup_rect = delegate_detail::make_rect(X{0}, popup_y, w, popup_h);
    }
}

bool dropdown_handle_input(const InputEvent& ev, WidgetNode& node,
                           size_t current_index, size_t count,
                           std::function<void(size_t)> select) {
    auto& es = node.get_or_create<DropdownEditState>();
    bool changed = false;

    if (auto* mb = std::get_if<MouseButton>(&ev); mb && mb->pressed) {
        if (es.open) {
            const Point at = in_popup(node, mb->position);
            DY rel_y = at.y - es.popup_rect.origin.y;
            if (rel_y.raw() >= 0 && rel_y.raw() < static_cast<float>(count) * dd_option_h.raw()
                && at.x.raw() >= 0 && at.x.raw() < delegate_detail::widget_w(node).raw()) {
                size_t idx = static_cast<size_t>(rel_y.raw() / dd_option_h.raw());
                select(idx);
                changed = true;
            }
            es.open = false;
            node.dirty = true;
        } else {
            es.open = true;
            es.highlighted = current_index;
            Height popup_h{static_cast<float>(count) * dd_option_h.raw()};
            // Default: open below; record() may flip upward
            es.popup_rect = delegate_detail::make_rect(X{0}, in_popup(node, Point{X{0}, Y{dd_widget_h.raw()}}).y,
                                                       delegate_detail::widget_w(node), popup_h);
            node.dirty = true;
        }
    } else if (auto* kp = std::get_if<KeyPress>(&ev)) {
        if (es.open) {
            if (kp->key == keys::down) {
                es.highlighted = (es.highlighted + 1) % count;
                node.dirty = true;
            } else if (kp->key == keys::up) {
                es.highlighted = (es.highlighted + count - 1) % count;
                node.dirty = true;
            } else if (kp->key == keys::enter) {
                select(es.highlighted);
                es.open = false;
                node.dirty = true;
                changed = true;
            } else if (kp->key == keys::escape) {
                es.open = false;
                node.dirty = true;
            }
        } else {
            if (kp->key == keys::space || kp->key == keys::enter) {
                es.open = true;
                es.highlighted = current_index;
                node.dirty = true;
            } else if (kp->key == keys::down) {
                size_t next = (current_index + 1) % count;
                select(next);
                changed = true;
            } else if (kp->key == keys::up) {
                size_t prev = (current_index + count - 1) % count;
                select(prev);
                changed = true;
            }
        }
    }
    return changed;
}

} // namespace dropdown_detail

// --- Widget<Choice> method bodies ---

void Widget<Choice>::record(DrawList& dl, const Field<Choice>& field, WidgetNode& node) {
    const auto& c = field.get();
    auto* chosen = c.chosen();
    dropdown_detail::DropdownLabelResolver resolver{
        .label_at = [&c](size_t i) { return c.options[i]; },
        .count = c.options.size()
    };
    dropdown_detail::dropdown_record(dl, node, chosen ? *chosen : c.placeholder, resolver);
}

void Widget<Choice>::handle_input(Field<Choice>& field, const InputEvent& ev, WidgetNode& node) {
    auto c = field.get();
    if (c.options.empty()) return;   // nothing to open or step to
    size_t current = c.chosen() ? *c.selected : 0;
    dropdown_detail::dropdown_handle_input(ev, node, current, c.options.size(), [&](size_t idx) {
        c.selected = idx;
        field.set(c);
    });
}

} // namespace prism::ui
