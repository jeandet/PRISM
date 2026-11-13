#pragma once

#include <prism/input/hit_test.hpp>
#include <prism/input/input_event.hpp>
#include <prism/app/widget_tree.hpp>
#include <prism/ui/popup_place.hpp>

#include <variant>

namespace prism::app::widget_detail {
using namespace prism::core;
using namespace prism::input;
using namespace prism::ui;

// --- popup layer: while a popup is open it gets all input first ---

// The open popup's area in window coordinates (its owner stores it in its own coordinates).
inline std::optional<Rect> popup_area(const WidgetTree& tree, const SceneSnapshot& snap) {
    if (tree.open_content_popup()) return tree.popup_card();
    const WidgetNode* owner = tree.popup_node();
    if (!owner || !owner->popup_area) return std::nullopt;
    auto origin = find_widget_rect(snap, owner->id);
    if (!origin) return std::nullopt;
    Rect r = *owner->popup_area;
    r.origin = Point{X{r.origin.x.raw() + origin->origin.x.raw()}, Y{r.origin.y.raw() + origin->origin.y.raw()}};
    return r;
}

// Whether `pos` (window coordinates) is on the open popup: its area, or more of it outside
// (a menu's open submenus, WidgetNode::popup_more).
inline bool on_popup(const WidgetTree& tree, const SceneSnapshot& snap, Point pos) {
    auto area = popup_area(tree, snap);
    if (!area) return false;
    if (area->contains(pos)) return true;
    const WidgetNode* owner = tree.popup_node();
    if (!owner || !owner->popup_area) return false;
    DX dx = area->origin.x - owner->popup_area->origin.x;   // owner -> window
    DY dy = area->origin.y - owner->popup_area->origin.y;
    return std::ranges::any_of(owner->popup_more, [&](const Rect& r) {
        return Rect{Point{r.origin.x + dx, r.origin.y + dy}, r.extent}.contains(pos);
    });
}

inline void dispatch_to_popup(WidgetTree& tree, const SceneSnapshot& snap, const InputEvent& ev) {
    auto id = tree.popup_owner();
    auto rect = find_widget_rect(snap, id);
    tree.dispatch(id, rect ? localize_mouse(ev, *rect) : ev);
}

// Returns true if the popup took the event. A press outside closes a plain popup and is
// swallowed either way, so it never activates what is underneath.
// A popup holding widgets (ui::Popup): inside its card, ordinary routing (the content's widgets
// are hit-tested first, in the overlay). Outside, a modal popup swallows the press; a
// non-modal one is told to close (Escape: it clears its `open`). A drag that started inside
// keeps going outside.
inline bool route_content_popup_mouse(WidgetTree& tree, const SceneSnapshot& snap, const InputEvent& ev,
                                      Point pos, bool is_press) {
    auto area = popup_area(tree, snap);
    if (area && area->contains(pos)) return false;
    // Outside the card but on the overlay of a widget inside it (a dropdown's list hanging
    // below): ordinary routing, as inside.
    if (auto id = hit_test(snap, pos); id && *id != tree.popup_owner() && tree.within(*id, tree.popup_owner()))
        return false;
    if (tree.captured_id() != 0 && !is_press) return false;
    if (is_press && !tree.popup_node()->popup_modal) {
        tree.dispatch(tree.popup_owner(), KeyPress{keys::escape, 0});
        tree.close_popup();
    }
    if (is_press || std::holds_alternative<MouseMove>(ev)) tree.update_hover(std::nullopt);
    return true;
}

inline bool route_popup_mouse(WidgetTree& tree, const SceneSnapshot& snap, const InputEvent& ev, Point pos,
                              bool is_press) {
    if (tree.popup_owner() == 0) return false;
    if (tree.open_content_popup()) return route_content_popup_mouse(tree, snap, ev, pos, is_press);
    // The release of the click that opened the popup lands here, not in the normal routing:
    // let go of the pressed widget, or it would keep receiving every pointer move.
    if (auto* b = std::get_if<MouseButton>(&ev); b && !b->pressed && tree.captured_id() != 0)
        tree.set_pressed(tree.captured_id(), false);
    auto area = popup_area(tree, snap);
    if (!area && tree.popup_node() && !tree.popup_node()->popup_area) {   // closed by the app since the last frame
        tree.close_popup();
        return false;
    }
    bool inside = on_popup(tree, snap, pos);
    // The dismissing press is swallowed: a right click outside an open menu only closes it.
    if (is_press && !inside) {
        if (auto* owner = tree.popup_node(); !owner || !owner->popup_modal) tree.close_popup();
        return true;
    }
    if (!inside && std::holds_alternative<MouseMove>(ev) && tree.opens_menu(tree.popup_owner())) {   // menu bar: hover switches menus
        auto over = hit_test(snap, pos);
        if (over && *over != tree.popup_owner() && tree.opens_menu(*over)) {
            tree.switch_menu(*over);
            return true;
        }
    }
    tree.update_hover(inside ? std::optional<WidgetId>{tree.popup_owner()} : std::nullopt);
    dispatch_to_popup(tree, snap, ev);
    return true;
}

// --- dragging inside the app (doc/design/drag-and-docking.md) ---

// The widget under `p` that takes the dragged value: the one hit, or the nearest container
// above it that does.
inline WidgetId drop_target_at(const WidgetTree& tree, const SceneSnapshot& snap, Point p, const std::any& value) {
    for (auto id = hit_test(snap, p); id; id = tree.parent_of(*id))
        if (tree.takes(*id, value)) return *id;
    return tree.drop_area_at(p, value);   // empty space in a container that takes drops
}

// True if the move belongs to a drag: one starting now, or running.
inline bool route_drag_move(WidgetTree& tree, const SceneSnapshot& snap, Point p) {
    if (!tree.drag() || !tree.start_drag_at(p)) return false;
    tree.move_drag(p, drop_target_at(tree, snap, p, tree.drag()->value));
    tree.update_hover(std::nullopt);
    return true;
}

inline bool route_drag_release(WidgetTree& tree, const SceneSnapshot& snap, Point p) {
    if (!tree.drag()) return false;
    if (!tree.dragging()) {   // never moved far enough: an ordinary click
        tree.end_drag(std::nullopt);
        return false;
    }
    if (!Rect{Point{X{0}, Y{0}}, snap.viewport}.contains(p)) {   // let go outside the window
        tree.drop_outside();
        return true;
    }
    WidgetId target = drop_target_at(tree, snap, p, tree.drag()->value);
    tree.move_drag(p, target);
    auto rect = target ? find_widget_rect(snap, target) : std::nullopt;
    if (target && !rect) rect = tree.drop_area_rect(target);   // a container: its recorded area
    tree.end_drag(rect ? std::optional{DropPlace{Point{X{p.x.raw() - rect->origin.x.raw()}, Y{p.y.raw() - rect->origin.y.raw()}},
                                                 rect->extent}}
                       : std::nullopt);
    return true;
}

inline void route_mouse_move(WidgetTree& tree, const SceneSnapshot& snap,
                             const MouseMove& mm) {
    if (route_drag_move(tree, snap, mm.position)) return;
    if (route_popup_mouse(tree, snap, mm, mm.position, false)) return;
    auto hovered = hit_test(snap, mm.position);
    tree.update_hover(hovered);
    if (tree.in_scrollbar_drag()) {
        tree.update_scrollbar_drag(mm.position.y);
        return;
    }
    if (auto cid = tree.captured_id(); cid != 0) {
        auto rect = find_widget_rect(snap, cid);
        InputEvent ev = mm;
        tree.dispatch(cid, rect ? localize_mouse(ev, *rect) : ev);
    } else if (hovered) {
        auto rect = find_widget_rect(snap, *hovered);
        InputEvent ev = mm;
        tree.dispatch(*hovered, rect ? localize_mouse(ev, *rect) : ev);
    }
}

// A right press opens the context menu of the enclosing container, if one is declared,
// instead of reaching the widget under the pointer.
inline bool open_context_menu(WidgetTree& tree, const SceneSnapshot& snap, const InputEvent& ev,
                              const MouseButton& mb) {
    if (!mb.pressed || mb.button != buttons::right) return false;
    auto menu = tree.context_menu_for(hit_test(snap, mb.position), mb.position);
    if (!menu) return false;
    auto rect = find_widget_rect(snap, *menu);
    tree.dispatch(*menu, rect ? localize_mouse(ev, *rect) : ev);
    return true;
}

inline Rect clamp_into(Rect r, Size window) {
    float x = std::clamp(r.origin.x.raw(), 0.f, std::max(0.f, window.w.raw() - r.extent.w.raw()));
    float y = std::clamp(r.origin.y.raw(), 0.f, std::max(0.f, window.h.raw() - r.extent.h.raw()));
    return Rect{Point{X{x}, Y{y}}, r.extent};
}

using ui::place_popup;

inline void keep_popup_in_window(WidgetTree& tree, const SceneSnapshot& snap) {
    auto area = popup_area(tree, snap);
    if (!area || snap.viewport.w.raw() <= 0.f) return;
    const WidgetNode* owner = tree.popup_node();
    Rect inside = clamp_into(*area, snap.viewport);
    if (owner->popup_opened_from) {
        DX ox = area->origin.x - owner->popup_area->origin.x;   // owner -> window
        DY oy = area->origin.y - owner->popup_area->origin.y;
        Rect anchor{Point{owner->popup_opened_from->origin.x + ox, owner->popup_opened_from->origin.y + oy},
                    owner->popup_opened_from->extent};
        inside = place_popup(area->extent, anchor, snap.viewport);
    }
    if (inside.origin == area->origin) return;
    DX dx = inside.origin.x - area->origin.x;
    DY dy = inside.origin.y - area->origin.y;
    Rect local = *tree.popup_node()->popup_area;
    tree.move_popup(Rect{Point{local.origin.x + dx, local.origin.y + dy}, local.extent});
}

inline void route_mouse_button(WidgetTree& tree, const SceneSnapshot& snap,
                               const InputEvent& ev, const MouseButton& mb) {
    if (!mb.pressed && mb.button == buttons::left && route_drag_release(tree, snap, mb.position)) return;
    if (route_popup_mouse(tree, snap, ev, mb.position, mb.pressed)) return;
    if (open_context_menu(tree, snap, ev, mb)) return;
    if (!mb.pressed && tree.in_scrollbar_drag()) {
        tree.end_scrollbar_drag();
        tree.set_pressed(tree.captured_id(), false);
        return;
    }
    if (mb.pressed) {
        if (auto oid = hit_test_scrollbar(snap, mb.position)) {
            tree.begin_scrollbar_drag(*oid, mb.position.y);
            if (tree.in_scrollbar_drag()) return;
        }
    }
    auto id = hit_test(snap, mb.position);
    if (!mb.pressed && tree.captured_id() != 0) {
        auto cid = tree.captured_id();
        auto rect = find_widget_rect(snap, cid);
        tree.dispatch(cid, rect ? localize_mouse(ev, *rect) : ev);   // still pressed: a click lands
        tree.set_pressed(cid, false);
    } else if (id) {
        tree.set_pressed(*id, mb.pressed);
        auto rect = find_widget_rect(snap, *id);
        if (mb.pressed) {
            if (*id != tree.focused_id())
                tree.close_overlays();
            tree.set_focused(*id);
            if (mb.button == buttons::left && rect) tree.arm_drag(*id, mb.position, *rect);
        }
        tree.dispatch(*id, rect ? localize_mouse(ev, *rect) : ev);
    } else if (mb.pressed) {
        tree.close_overlays();
        tree.clear_focus();
    }
}

inline void route_mouse_scroll(WidgetTree& tree, const SceneSnapshot& snap,
                               const MouseScroll& ms) {
    if (tree.popup_owner() != 0) {   // inside: the popup; outside: nothing scrolls under it
        if (!on_popup(tree, snap, ms.position)) return;
        if (!tree.open_content_popup()) return dispatch_to_popup(tree, snap, ms);
        // a popup holding widgets: its own scroll views scroll, as in the window
    }
    constexpr float wheel_multiplier = 60.f;
    auto id = hit_test(snap, ms.position);
    if (id) {
        auto rect = find_widget_rect(snap, *id);
        InputEvent ev = ms;
        tree.dispatch(*id, rect ? localize_mouse(ev, *rect) : ev);
        WidgetId stop = tree.open_content_popup() ? tree.popup_owner() : 0;   // stays in the popup
        tree.scroll_at(*id, DY{-ms.dy.raw() * wheel_multiplier}, stop);   // wheel down: further down
    }
}

// Alt+letter opens the menu marked with that letter (or switches to it from another open
// menu); not while a modal popup has the keyboard.
inline bool open_menu_by_mnemonic(WidgetTree& tree, const KeyPress& kp) {
    if (!(kp.mods & mods::alt) || (kp.mods & (mods::ctrl | mods::gui))) return false;
    if (auto* owner = tree.popup_node(); owner && owner->popup_modal) return false;
    auto menu = tree.menu_for_key(kp.key);
    if (!menu) return false;
    tree.switch_menu(*menu);
    return true;
}

inline void route_key_press(WidgetTree& tree, const InputEvent& ev,
                            const KeyPress& kp) {
    constexpr float page_delta = 200.f;
    tree.swallow_next_text_input(false);   // only the text event right after a popup's key
    if (tree.dragging()) {   // the drag has the keyboard: Escape cancels it, nothing else acts
        if (kp.key == keys::escape) tree.end_drag(std::nullopt);
        return;
    }
    if (open_menu_by_mnemonic(tree, kp)) return;
    if (tree.open_content_popup() && kp.key == keys::escape) {   // a popup holding widgets
        // An open list inside it (a dropdown) closes first; then the popup itself.
        WidgetId f = tree.focused_id();
        tree.dispatch(f != 0 && tree.has_open_overlay(f) ? f : tree.popup_owner(), ev);
        return;
    }
    if (tree.popup_owner() != 0 && !tree.open_content_popup()) {   // keys belong to the popup; escape closes a plain one
        auto bar_step = kp.key == keys::right ? 1 : kp.key == keys::left ? -1 : 0;
        auto neighbour = bar_step ? tree.menu_neighbour(tree.popup_owner(), bar_step) : std::nullopt;
        auto* owner = tree.popup_node();
        if (owner && !owner->popup_area) {   // closed by the app since the last frame
            tree.close_popup();
            return route_key_press(tree, ev, kp);
        }
        bool menu_keeps = owner && menu_detail::keeps_key(*owner, kp.key);   // a submenu to open or close
        if (kp.key == keys::escape && !menu_keeps && (!owner || !owner->popup_modal)) tree.close_popup();
        else if (neighbour && !menu_keeps) tree.switch_menu(*neighbour);
        else if (!bar_step || menu_keeps || !tree.opens_menu(tree.popup_owner())) tree.dispatch(tree.popup_owner(), ev);
        tree.swallow_next_text_input(menu_detail::is_mnemonic_key(kp.key));   // only a letter types
        return;
    }
    if (tree.popup_owner() == 0 && tree.focused_keeps(kp)) return tree.dispatch(tree.focused_id(), ev);
    if (tree.popup_owner() == 0 && tree.trigger_shortcut(kp)) return;   // not while a dialog is up
    if (kp.key == keys::tab) {
        if (kp.mods & mods::shift) tree.focus_prev();
        else tree.focus_next();
    } else if (kp.key == keys::page_up && tree.focused_id() != 0) {
        tree.scroll_at(tree.focused_id(), DY{-page_delta});
    } else if (kp.key == keys::page_down && tree.focused_id() != 0) {
        tree.scroll_at(tree.focused_id(), DY{page_delta});
    } else if (tree.focused_id() != 0) {
        tree.dispatch(tree.focused_id(), ev);
    }
}

inline void route_text_input(WidgetTree& tree, const InputEvent& ev) {
    if (tree.swallows_text_input() && std::holds_alternative<TextInput>(ev)) {
        tree.swallow_next_text_input(false);
        return;
    }
    WidgetId target = tree.popup_owner() != 0 && !tree.open_content_popup() ? tree.popup_owner() : tree.focused_id();
    if (target != 0) tree.dispatch(target, ev);
}

// Releases go to the focused widget only. Tab and page keys act on press; their release is
// delivered like any other key, never acted on twice.
inline void route_key_release(WidgetTree& tree, const InputEvent& ev) {
    if (tree.focused_id() != 0)
        tree.dispatch(tree.focused_id(), ev);
}

// Top-level dispatcher: switches on the event variant and routes to the
// per-kind handlers above. `snap` may be null — mouse routing hit-tests
// against a snapshot; key/text routing doesn't need one.
inline void route_tooltip(WidgetTree& tree, const InputEvent& ev) {
    if (std::holds_alternative<MouseLeave>(ev)) {
        tree.hover_tooltip(0, {}, TooltipState::clock::now());
        return;
    }
    if (auto* mm = std::get_if<MouseMove>(&ev)) {
        bool busy = tree.popup_owner() != 0 || tree.captured_id() != 0;   // a popup, or a drag
        WidgetId over = busy ? 0 : tree.hovered_id();
        tree.hover_tooltip(over, mm->position, TooltipState::clock::now());
        return;
    }
    auto* mb = std::get_if<MouseButton>(&ev);
    bool typing = std::holds_alternative<KeyPress>(ev) || std::holds_alternative<TextInput>(ev)
               || std::holds_alternative<Paste>(ev) || std::holds_alternative<Composition>(ev);
    if ((mb && mb->pressed) || std::holds_alternative<MouseScroll>(ev) || typing)
        tree.dismiss_tooltip();
}

// A drop goes to the widget under it. Dropped text is typed there, as a paste is: a text
// field or text area inserts it (at its caret). Dropped files reach the widget too; the app
// sees them through AppContext::on_file_drop.
// simplify: dropped text goes in at the caret, not where it was dropped; moving the caret to
// the drop point needs each text widget to map a point to an offset on a drop.
inline void route_drop(WidgetTree& tree, const SceneSnapshot& snap, const InputEvent& ev) {
    auto* file = std::get_if<FileDrop>(&ev);
    auto* text = std::get_if<TextDrop>(&ev);
    if (!file && !text) return;
    auto id = hit_test(snap, file ? file->position : text->position);
    if (!id) return;
    if (file) {
        auto rect = find_widget_rect(snap, *id);
        tree.dispatch(*id, rect ? localize_mouse(ev, *rect) : ev);
        return;
    }
    tree.set_focused(*id);
    tree.dispatch(*id, Paste{text->text});
}

inline void route_event(WidgetTree& tree, const SceneSnapshot* snap,
                        const InputEvent& ev) {
    tree.set_input_frame(snap ? snap->version : UINT64_MAX);
    tree.refresh_popup_owner();
    if (snap) {
        if (auto* mm = std::get_if<MouseMove>(&ev))
            route_mouse_move(tree, *snap, *mm);
        if (auto* mb = std::get_if<MouseButton>(&ev))
            route_mouse_button(tree, *snap, ev, *mb);
        if (auto* ms = std::get_if<MouseScroll>(&ev))
            route_mouse_scroll(tree, *snap, *ms);
    }
    if (std::holds_alternative<MouseLeave>(ev)) tree.update_hover(std::nullopt);
    if (snap) route_drop(tree, *snap, ev);
    if (auto* kp = std::get_if<KeyPress>(&ev))
        route_key_press(tree, ev, *kp);
    if (std::get_if<TextInput>(&ev) || std::get_if<Paste>(&ev) || std::get_if<Composition>(&ev))
        route_text_input(tree, ev);
    if (std::get_if<KeyRelease>(&ev))
        route_key_release(tree, ev);
    if (snap) keep_popup_in_window(tree, *snap);
    tree.release_closed_popup();   // a widget inside a popup may have closed it (a dialog's button)
    route_tooltip(tree, ev);
}

} // namespace prism::app::widget_detail
