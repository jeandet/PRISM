#pragma once

#include <prism/delegates/dropdown_delegates.hpp>
#include <prism/delegates/menu_delegates.hpp>
#include <prism/delegates/dialog_delegates.hpp>
#include <prism/delegates/form_delegates.hpp>
#include <prism/delegates/tabs_delegates.hpp>
#include <prism/app/drag_draw.hpp>
#include <prism/ui/dock.hpp>
#include <prism/ui/layout.hpp>
#include <prism/core/list.hpp>
#include <prism/ui/table_ops.hpp>
#include <prism/ui/tree.hpp>
#include <prism/delegates/text_delegates.hpp>
#include <prism/core/traits.hpp>
#include <prism/ui/widget_node.hpp>
#include <prism/app/widget_tree_layout.hpp>
#include <prism/app/widget_tree_traversal.hpp>
#include <prism/app/layout_writeback.hpp>
#include <prism/app/popup_layout.hpp>
#include <prism/app/handle_drag.hpp>
#include <prism/app/table_input.hpp>
#include <prism/app/table_paint.hpp>
#include <prism/ui/popup_place.hpp>
#include <prism/ui/tooltip.hpp>
#if __cpp_impl_reflection
#include <prism/core/reflect.hpp>
#include <prism/core/reflect_annotations.hpp>
#include <prism/app/widget_tree_reflect.hpp>
#endif
#include <prism/core/state.hpp>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <memory>
#include <set>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace prism::app {
using namespace prism::core;
using namespace prism::ui;
using namespace prism::app::widget_detail;

class ViewBuilder;

// Names T through a dependent type, so a member template can use a type that is only
// forward-declared at its definition. GCC defers the completeness check to instantiation
// on its own; clang (and so Emscripten) checks non-dependent types at definition time.
template <typename T, typename>
struct dependent_type { using type = T; };

// index_ stores raw pointers into the tree — built after construction;
// virtualized containers (VirtualList/Table/Tabs) mutate the tree and
// rebuild the index via reindex() so pointers stay valid.
using WindowId = std::uint32_t;   // as app/window.hpp declares it; that header is not needed here

class WidgetTree {
public:
    using ViewBuilder = app::ViewBuilder;
    friend class app::ViewBuilder;

    template <typename Model>
    explicit WidgetTree(Model& model) {
        auto node_tree = build_node_tree(model);
        collect_drains(node_tree);
        root_ = build_widget_node(node_tree);
        propagate_theme(root_);
        connect_dirty(node_tree, root_);
        build_index(root_);
        clear_dirty();
    }

    WidgetTree(const WidgetTree&) = delete;
    WidgetTree& operator=(const WidgetTree&) = delete;

    [[nodiscard]] WidgetNode& root() { return root_; }
    [[nodiscard]] const WidgetNode& root() const { return root_; }

    [[nodiscard]] size_t leaf_count() const { return count_leaves(root_); }
    [[nodiscard]] bool any_dirty() const { return tooltip_dirty_ || check_dirty(root_); }
    [[nodiscard]] std::size_t dirty_count() const { return count_dirty(root_); }

    void clear_dirty() {
        tooltip_dirty_ = false;
        clear_dirty_impl(root_);
    }

    void drain_shared() {
        for (auto& fn : drain_callbacks_)
            fn();
    }

    // Hands `waker` to every Shared<T>/Channel<T> this tree drains, so a set()/send() from any
    // thread wakes the logic thread instead of waiting for the next input or tick.
    void bind_waker(const std::function<void()>& waker) {
        for (auto& bind : waker_binders_)
            bind(waker);
    }

    const Theme& theme() const { return theme_; }

    // Every node points at theme_, so this reaches all widgets, including ones built later.
    // Everything re-records: widgets recorded before (with the estimate) must pick up real metrics.
    void set_clipboard(const Clipboard* c) { theme_.clipboard = c; }

    void set_text_measurer(const TextMeasurer* m) {
        if (theme_.measurer == m) return;
        theme_.measurer = m;
        mark_all_dirty_impl(root_);
    }

    // Switches every widget to `t` (ui::light_theme(), dark_theme(), or the app's own) and
    // repaints them; the backend's measurer and clipboard stay.
    void set_theme(Theme t) {
        t.measurer = theme_.measurer;
        t.clipboard = theme_.clipboard;
        theme_ = t;
        mark_all_dirty_impl(root_);
        tooltip_dirty_ = true;
    }

    void close_overlays() { close_overlays_impl(root_); }

    // Scrolls the nearest scroll view at or above `target`; at its end the wheel passes to the
    // next one up, but never past `stop` (a popup: the window behind it does not scroll).
    void scroll_at(WidgetId target, DY delta, WidgetId stop = 0) {
        WidgetId current = target;
        while (current != 0 && current != stop) {
            auto it = index_.find(current);
            if (it != index_.end()) {
                auto sv = get_scroll_view(*it->second);
                if (sv) {
                    DY max_off = max_scroll(sv->content_h, sv->viewport_h);
                    DY new_off{std::clamp(sv->offset.raw() + delta.raw(), 0.f, max_off.raw())};

                    if (std::abs(new_off.raw() - sv->offset.raw()) < 0.001f) {
                        if (sv->event_policy == ScrollEventPolicy::BubbleAtBounds) {
                            auto pit = parent_map_.find(current);
                            current = (pit != parent_map_.end()) ? pit->second : 0;
                            continue;
                        }
                        return;
                    }

                    sv->offset = new_off;
                    sv->show_ticks = 30;
                    set_dirty(current);
                    return;
                }
            }
            auto pit = parent_map_.find(current);
            current = (pit != parent_map_.end()) ? pit->second : 0;
        }
    }

    void scroll_to(WidgetId id, DY offset) {
        auto it = index_.find(id);
        if (it == index_.end()) return;
        auto sv = get_scroll_view(*it->second);
        if (!sv) return;
        DY max_off = max_scroll(sv->content_h, sv->viewport_h);
        sv->offset = DY{std::clamp(offset.raw(), 0.f, max_off.raw())};
        sv->show_ticks = 30;
        set_dirty(id);
    }

    void scroll_row_into_view(WidgetId container_id, size_t row_index, Height row_h) {
        auto it = index_.find(container_id);
        if (it == index_.end()) return;
        auto sv = get_scroll_view(*it->second);
        if (!sv) return;

        DY row_top{static_cast<float>(row_index) * row_h.raw()};
        DY max_off = max_scroll(sv->content_h, sv->viewport_h);
        DY new_offset = reveal_row_offset(row_top, row_h, sv->offset, sv->viewport_h, max_off);
        if (new_offset.raw() != sv->offset.raw())
            scroll_to(container_id, new_offset);
    }

    [[nodiscard]] std::vector<WidgetId> leaf_ids() const {
        std::vector<WidgetId> ids;
        collect_leaf_ids(root_, ids);
        return ids;
    }

    void dispatch(WidgetId id, const InputEvent& ev) {
        if (auto it = index_.find(id); it != index_.end() && is_enabled(*it->second)) {
            it->second->on_input.emit(ev);
            track_popup(*it->second);
        }
    }

    // The widget whose popup is on top (0 = none): it gets the input. Popups stack: one opened
    // inside another (a menu in a dialog) goes on top, and closing it returns to the one below.
    [[nodiscard]] WidgetId popup_owner() const { return popup_stack_.empty() ? 0 : popup_stack_.back(); }
    [[nodiscard]] const WidgetNode* popup_node() const {
        auto it = index_.find(popup_owner());
        return it != index_.end() ? it->second : nullptr;
    }
    void close_popup() {
        if (auto it = index_.find(popup_owner()); it != index_.end()) {
            WidgetNode& n = *it->second;
            n.popup_area.reset();
            n.popup_opened_from.reset();
            n.popup_more.clear();
            n.overlay_draws.clear();
            n.edit_state.reset();   // the owner's open/highlight state, as close_overlays does
            n.dirty = true;
        }
        if (!popup_stack_.empty()) popup_stack_.pop_back();
    }

    // The menu next to `from` in its menu bar (its sibling menus), dir = +1 or -1.
    [[nodiscard]] std::optional<WidgetId> menu_neighbour(WidgetId from, int dir) const {
        auto parent = parent_map_.find(from);
        if (parent == parent_map_.end()) return std::nullopt;
        auto pit = index_.find(parent->second);
        if (pit == index_.end()) return std::nullopt;
        std::vector<WidgetId> bar;
        for (const auto& c : pit->second->children)
            if (c.opens_menu) bar.push_back(c.id);
        auto at = std::ranges::find(bar, from);
        if (at == bar.end()) return std::nullopt;
        auto i = (at - bar.begin()) + dir;
        if (i < 0 || i >= static_cast<std::ptrdiff_t>(bar.size())) return std::nullopt;
        return bar[static_cast<size_t>(i)];
    }

    [[nodiscard]] bool opens_menu(WidgetId id) const {
        auto it = index_.find(id);
        return it != index_.end() && it->second->opens_menu;
    }

    // The innermost container under `p` (containers have no hit geometry of their own).
    [[nodiscard]] const WidgetNode& container_at(const WidgetNode& n, Point p) const {
        for (const auto& c : n.children)
            if (c.is_container && Rect{Point{c.absolute_x, c.absolute_y}, c.arranged_extent}.contains(p))
                return container_at(c, p);
        return n;
    }

    // The context menu for a right click at `p` on `hit`: declared in the nearest enclosing
    // container. With no widget hit, the walk starts at the container under the pointer.
    [[nodiscard]] std::optional<WidgetId> context_menu_for(std::optional<WidgetId> hit, Point p) const {
        WidgetId current = hit.value_or(container_at(root_, p).id);
        while (true) {
            if (auto it = index_.find(current); it != index_.end())
                for (const auto& c : it->second->children)
                    if (c.context_menu) return c.id;
            if (current == root_.id) return std::nullopt;
            auto parent = parent_map_.find(current);
            current = parent != parent_map_.end() ? parent->second : root_.id;
        }
    }

    // Re-records the popup owner if the app changed it since the last frame (e.g. closed a
    // dialog), so routing acts on the popup as it is now, not as last drawn.
    void refresh_popup_owner() {
        auto it = index_.find(popup_owner());
        if (popup_owner() == 0 || it == index_.end()) return;
        if (it->second->dirty && it->second->record) it->second->record(*it->second);
    }

    // After an event: a popup that closed during it (a dialog's button, the app) lets go of the
    // input now, not at the next frame. The event itself still met the popup, as it was drawn.
    void release_closed_popup() {
        refresh_popup_owner();
        auto it = index_.find(popup_owner());
        if (popup_owner() != 0 && it != index_.end() && !it->second->popup_area) close_popup();
    }

    // Moves the open popup (owner coordinates), e.g. to keep it inside the window.
    void move_popup(Rect area) {
        if (auto it = index_.find(popup_owner()); it != index_.end() && it->second->popup_area) {
            it->second->popup_area = area;
            it->second->dirty = true;
        }
    }

    // The menu whose title's mnemonic is `key` (Alt+key opens it), if any.
    // Two menus with the same letter: the first in layout order, as in Qt.
    [[nodiscard]] std::optional<WidgetId> menu_for_key(std::int32_t key) const {
        if (key == 0) return std::nullopt;
        for (WidgetId id : leaf_ids()) {
            auto it = index_.find(id);
            if (it == index_.end()) continue;
            const WidgetNode& n = *it->second;
            if (n.opens_menu && n.mnemonic_key && n.mnemonic_key() == key) return id;
        }
        return std::nullopt;
    }

    // Frame versions, for input hit-tested against an older frame than the latest (see
    // WindowEvent::shown): routing records the event's frame; a change that moves content
    // records the first frame that shows it (next_frame_version).
    void set_input_frame(std::uint64_t version) { input_frame_ = version; }
    [[nodiscard]] std::uint64_t input_frame() const { return input_frame_; }
    [[nodiscard]] std::uint64_t next_frame_version() const { return last_built_version_ + 1; }

    // Text input that follows a key press a popup used (SDL sends both for a letter): dropped,
    // or a letter that chose a menu item would also be typed into the focused field.
    void swallow_next_text_input(bool on) { swallow_text_ = on; }
    [[nodiscard]] bool swallows_text_input() const { return swallow_text_; }

    // Closes the open menu and opens `to` (menus open on Down), focused.
    void switch_menu(WidgetId to) {
        close_popup();
        set_focused(to);
        dispatch(to, KeyPress{keys::down, 0});
    }

    // A popup can also open or close without input, e.g. a dialog the app opens by setting
    // its field. Picks the first open one when none is tracked; drops one that closed.
    // simplify: walks the tree each frame while no popup is open; keep a list of popup
    // anchors if trees grow large enough for this to show in profiles.
    // A modal popup takes over from a plain one (a dialog opening over an open menu).
    void adopt_popup(WidgetNode& node) {
        drop_closed_popups();
        adopt_popup_in(node);
    }

    // Popups that closed since the last frame leave the top of the stack.
    void drop_closed_popups() {
        while (!popup_stack_.empty()) {
            auto it = index_.find(popup_stack_.back());
            if (it != index_.end() && it->second->popup_area) return;
            popup_stack_.pop_back();
        }
    }

    void adopt_popup_in(WidgetNode& node) {
        if (node.popup_area && node.id != 0 && !in_popup_stack(node.id) && adopts(node)) {
            if (!nested_in_top(node.id)) close_popup();   // a modal popup over a plain one
            popup_stack_.push_back(node.id);
        }
        for (auto& c : node.children) adopt_popup_in(c);
    }

    // Whether an open popup the tree does not track yet goes on top: the first one, one opened
    // inside the top popup (a menu in a dialog), or a modal one over a plain popup.
    [[nodiscard]] bool adopts(const WidgetNode& node) const {
        const WidgetNode* top = popup_node();
        return !top || nested_in_top(node.id) || (node.popup_modal && !top->popup_modal);
    }

    [[nodiscard]] bool nested_in_top(WidgetId id) const {
        return !popup_stack_.empty() && id != popup_stack_.back() && within(id, popup_stack_.back());
    }

    [[nodiscard]] bool in_popup_stack(WidgetId id) const {
        return std::ranges::find(popup_stack_, id) != popup_stack_.end();
    }

    // After a widget handled an event: a popup it opened goes on top (above the popups it is
    // inside of, replacing any others); one it closed leaves. id == 0 is a no-op.
    void track_popup(const WidgetNode& n) {
        if (n.popup_area) {
            if (in_popup_stack(n.id)) return;
            while (!popup_stack_.empty() && !within(n.id, popup_stack_.back())) popup_stack_.pop_back();
            popup_stack_.push_back(n.id);
        } else if (popup_owner() == n.id) {
            popup_stack_.pop_back();
        }
    }

    void flip_visual_flag(WidgetId id, bool WidgetVisualState::* flag, bool value) {
        if (id == 0) return;
        if (auto it = index_.find(id); it != index_.end() && (!value || is_enabled(*it->second))) {
            it->second->visual_state.*flag = value;
            it->second->dirty = true;
        }
    }

    void update_hover(std::optional<WidgetId> id) {
        WidgetId new_id = id.value_or(0);
        if (new_id == hovered_id_) return;
        flip_visual_flag(hovered_id_, &WidgetVisualState::hovered, false);
        hovered_id_ = new_id;
        flip_visual_flag(hovered_id_, &WidgetVisualState::hovered, true);
    }

    // Tooltips (ui/tooltip.hpp): routing reports hover and dismissal, the app calls
    // tick_tooltip() at tooltip_wake(), and build_snapshot() draws a shown one.
    [[nodiscard]] std::string_view tooltip_of(WidgetId id) const {
        auto it = index_.find(id);
        return it == index_.end() ? std::string_view{} : std::string_view{it->second->tooltip};
    }
    void hover_tooltip(WidgetId id, Point at, TooltipState::clock::time_point now) {
        set_tooltip(tooltip_hovered(tooltip_, id, !tooltip_of(id).empty(), at, now));
    }
    void dismiss_tooltip() { set_tooltip(tooltip_dismissed(tooltip_)); }
    void tick_tooltip(TooltipState::clock::time_point now) { set_tooltip(tooltip_ticked(tooltip_, now)); }
    [[nodiscard]] std::optional<TooltipState::clock::time_point> tooltip_wake() const {
        return ui::tooltip_wake(tooltip_);
    }

    // When the tree next needs a frame with no input: a pending tooltip, or an animating widget
    // (busy progress). The app arms its timer with it and calls tick() then.
    [[nodiscard]] std::optional<TooltipState::clock::time_point> next_wake() const {
        auto wake = tooltip_wake();
        auto earliest = [&](auto& self, const WidgetNode& n) -> void {
            if (n.redraw_at && (!wake || *n.redraw_at < *wake)) wake = n.redraw_at;
            for (auto& c : n.children) self(self, c);
        };
        earliest(earliest, root_);
        return wake;
    }

    // Runs what is due at `now`: shows a tooltip, redraws animating widgets.
    void tick(TooltipState::clock::time_point now) {
        tick_tooltip(now);
        auto due = [&](auto& self, WidgetNode& n) -> void {
            if (n.redraw_at && *n.redraw_at <= now) {   // its record() sets the next one
                n.dirty = true;
                n.redraw_at.reset();
            }
            for (auto& c : n.children) self(self, c);
        };
        due(due, root_);
    }

    void set_pressed(WidgetId id, bool pressed) {
        flip_visual_flag(id, &WidgetVisualState::pressed, pressed);
        captured_id_ = pressed && enabled(id) ? id : 0;
    }

    [[nodiscard]] WidgetId captured_id() const { return captured_id_; }

    [[nodiscard]] CursorShape desired_cursor() const {
        if (dragging()) return drag_->target != 0 ? CursorShape::Default : CursorShape::NotAllowed;
        WidgetId active = captured_id_ != 0 ? captured_id_ : hovered_id_;
        if (active == 0) return CursorShape::Default;
        auto it = index_.find(active);
        return it != index_.end() && is_enabled(*it->second) ? it->second->visual_state.cursor : CursorShape::Default;
    }

    using ScrollbarDrag = widget_detail::ScrollbarDrag;
    using SplitDrag = widget_detail::SplitDrag;

    void begin_scrollbar_drag(WidgetId id, Y mouse_y) {
        auto it = index_.find(id);
        if (it == index_.end()) return;
        auto sv = get_scroll_view(*it->second);
        if (!sv || sv->content_h.raw() <= sv->viewport_h.raw()) return;
        scrollbar_drag_ = ScrollbarDrag{
            .scroll_id = id,
            .anchor_y = mouse_y,
            .anchor_offset = sv->offset,
            .viewport_h = sv->viewport_h,
            .content_h = sv->content_h,
            .thumb_h = scrollbar::thumb_height(sv->viewport_h, sv->content_h),
        };
        captured_id_ = id;
    }

    void update_scrollbar_drag(Y mouse_y) {
        if (scrollbar_drag_.scroll_id == 0) return;
        if (auto offset = scrollbar_drag_offset(scrollbar_drag_, mouse_y))
            scroll_to(scrollbar_drag_.scroll_id, *offset);
    }

    void end_scrollbar_drag() {
        scrollbar_drag_ = {};
    }

    [[nodiscard]] bool in_scrollbar_drag() const { return scrollbar_drag_.scroll_id != 0; }

    void begin_split_drag(WidgetId container_id, size_t handle_index, float pos) {
        auto it = index_.find(container_id);
        if (it == index_.end()) return;
        auto& ss = engage_split(*it->second);
        if (handle_index + 1 >= ss.pane_sizes.size()) return;
        split_drag_ = SplitDrag{
            .container_id = container_id,
            .handle_index = handle_index,
            .anchor = pos,
            .orig_before = ss.pane_sizes[handle_index],
            .orig_after = ss.pane_sizes[handle_index + 1],
        };
    }

    void update_split_drag(float pos) {
        if (split_drag_.container_id == 0) return;
        auto it = index_.find(split_drag_.container_id);
        if (it == index_.end()) return;
        auto* ss = std::any_cast<SplitState>(&it->second->edit_state);
        if (!ss) return;
        auto [before, after] = split_drag_sizes(split_drag_, pos);
        ss->pane_sizes[split_drag_.handle_index] = before;
        ss->pane_sizes[split_drag_.handle_index + 1] = after;
        set_dirty(split_drag_.container_id);
    }

    void end_split_drag() {
        if (auto it = index_.find(split_drag_.container_id); it != index_.end() && it->second->split_changed)
            if (auto* ss = std::any_cast<SplitState>(&it->second->edit_state))
                it->second->split_changed(ss->pane_sizes);
        split_drag_ = {};
    }

    [[nodiscard]] bool in_split_drag() const { return split_drag_.container_id != 0; }

    [[nodiscard]] Connection connect_input(WidgetId id, std::function<void(const InputEvent&)> cb) {
        if (auto it = index_.find(id); it != index_.end())
            return it->second->on_input.connect(std::move(cb));
        return {};
    }

    [[nodiscard]] WidgetId hovered_id() const { return hovered_id_; }

    [[nodiscard]] WidgetId focused_id() const { return focused_id_; }

    [[nodiscard]] const std::vector<WidgetId>& focus_order() const { return focus_order_; }

    void set_focused(WidgetId id) {
        if (id == focused_id_ || !enabled(id)) return;
        if (std::find(focus_order_.begin(), focus_order_.end(), id) == focus_order_.end()) return;
        flip_visual_flag(focused_id_, &WidgetVisualState::focused, false);
        focused_id_ = id;
        flip_visual_flag(focused_id_, &WidgetVisualState::focused, true);
    }

    void clear_focus() {
        flip_visual_flag(focused_id_, &WidgetVisualState::focused, false);
        focused_id_ = 0;
    }

    // What Tab cycles through: the open popup's content if a popup holds widgets (it keeps the
    // focus inside), else every focusable widget.
    [[nodiscard]] std::vector<WidgetId> focus_ring() const {
        const WidgetNode* p = open_content_popup();
        std::vector<WidgetId> ring;
        for (WidgetId id : focus_order_)
            if ((!p || within(id, p->id)) && enabled(id)) ring.push_back(id);
        return ring;
    }

    [[nodiscard]] bool enabled(WidgetId id) const {
        auto it = index_.find(id);
        return it == index_.end() || is_enabled(*it->second);
    }

    void focus_next() { step_focus(+1); }
    void focus_prev() { step_focus(-1); }

    void step_focus(int dir) {
        auto ring = focus_ring();
        if (ring.empty()) return;
        auto it = std::ranges::find(ring, focused_id_);
        if (it == ring.end()) return set_focused(dir > 0 ? ring.front() : ring.back());
        auto i = static_cast<std::ptrdiff_t>(it - ring.begin()) + dir;
        auto n = static_cast<std::ptrdiff_t>(ring.size());
        set_focused(ring[static_cast<std::size_t>((i % n + n) % n)]);
    }

    // --- windows of the app's own, asked for by widgets (a dock's floating panels) ---
    // AppContext installs the opener; without one (tests, the browser) nothing opens.
    struct WindowRequest {
        std::string title;
        std::function<void(ViewBuilder&)> build;   // what the window shows
        std::function<void(WindowId)> on_opened;   // its id, or 0 if it could not open
        std::function<void()> on_closed;           // the user closed it
    };
    using WindowOpener = std::function<void(WindowRequest)>;
    void set_window_opener(WindowOpener open, std::function<void(WindowId)> close) {
        window_opener_ = std::move(open);
        window_closer_ = std::move(close);
    }
    [[nodiscard]] bool can_open_windows() const { return static_cast<bool>(window_opener_); }
    // The window opens later, outside the current frame; on_opened says how it went.
    void request_window(WindowRequest r) {
        if (window_opener_) window_opener_(std::move(r));
        else if (r.on_opened) r.on_opened(0);
    }
    void close_window(WindowId id) {
        if (window_closer_) window_closer_(id);
    }

    // --- dragging inside the app (doc/design/drag-and-docking.md) ---
    // A press on a source arms a drag; moving past drag_threshold starts it (the source gives
    // its value); from then the tree owns the pointer until the release or Escape.
    static constexpr float drag_threshold = 4.f;
    struct DragState {
        WidgetId source = 0;
        Point press{};
        Offset grab{};          // the pointer within the source, kept while it follows
        std::any value;         // empty until the drag starts
        Point pointer{};
        WidgetId target = 0;    // the widget under the pointer that takes the value, or 0
    };

    void arm_drag(WidgetId id, Point at, Rect source_rect) {
        auto it = index_.find(id);
        if (it == index_.end() || !it->second->drag.source || !is_enabled(*it->second)) return;
        drag_ = DragState{.source = id, .press = at, .grab = at - source_rect.origin, .value = {}, .pointer = at, .target = 0};
    }
    [[nodiscard]] bool dragging() const { return drag_ && drag_->value.has_value(); }
    [[nodiscard]] const DragState* drag() const { return drag_ ? &*drag_ : nullptr; }

    // Starts the drag once the pointer has moved far enough; false while it has not, or if the
    // source has nothing to drag (the press then stays an ordinary one).
    bool start_drag_at(Point p) {
        if (!drag_ || dragging()) return dragging();
        float dx = p.x.raw() - drag_->press.x.raw(), dy = p.y.raw() - drag_->press.y.raw();
        if (dx * dx + dy * dy < drag_threshold * drag_threshold) return false;
        auto it = index_.find(drag_->source);
        Point local{X{drag_->grab.dx.raw()}, Y{drag_->grab.dy.raw()}};   // where it was pressed, in it
        drag_->value = it != index_.end() ? it->second->drag.source(*it->second, local) : std::any{};
        if (!drag_->value.has_value()) {
            drag_.reset();
            return false;
        }
        set_pressed(drag_->source, false);   // the source lets go: no click on release
        tooltip_dirty_ = true;
        return true;
    }

    // Whether widget `id` takes the dragged value: a drop target of its type, or a text widget
    // taking a string as typing.
    [[nodiscard]] bool takes(WidgetId id, const std::any& value) const {
        auto it = index_.find(id);
        if (it == index_.end() || !is_enabled(*it->second)) return false;
        const auto& n = *it->second;
        if (n.drag.accepts) return n.drag.accepts(value);
        return value.type() == typeid(std::string) && edits_text(n);
    }

    // The innermost container taking drops whose area holds `p` and that takes `value`.
    [[nodiscard]] WidgetId drop_area_at(Point p, const std::any& value) const {
        for (auto it = drop_areas_.rbegin(); it != drop_areas_.rend(); ++it)
            if (it->second.contains(p) && takes(it->first, value)) return it->first;
        return 0;
    }
    [[nodiscard]] std::optional<Rect> drop_area_rect(WidgetId id) const {
        for (auto& [i, r] : drop_areas_)
            if (i == id) return r;
        return std::nullopt;
    }

    void move_drag(Point p, WidgetId target) {
        drag_->pointer = p;
        drag_->target = target;
        tooltip_dirty_ = true;   // the ghost and the outline are redrawn
    }

    // The drag was let go outside the window: its source decides (a dock tab floats its panel).
    void drop_outside() {
        if (!dragging()) return drag_.reset();
        auto d = std::move(*drag_);
        drag_.reset();
        tooltip_dirty_ = true;
        if (auto it = index_.find(d.source); it != index_.end() && it->second->drag.outside) it->second->drag.outside(d.value);
    }

    // Ends the drag: drops on the target (at `place`), or nothing.
    void end_drag(std::optional<DropPlace> place) {
        if (!dragging()) {
            drag_.reset();
            return;
        }
        auto d = std::move(*drag_);
        drag_.reset();
        tooltip_dirty_ = true;
        if (!place || d.target == 0) return;
        auto it = index_.find(d.target);
        if (it == index_.end()) return;
        if (it->second->drag.drop) it->second->drag.drop(d.value, *place);
        else if (d.value.type() == typeid(std::string)) {   // a text widget: typed in, as a drop from the OS
            set_focused(d.target);
            dispatch(d.target, Paste{std::any_cast<const std::string&>(d.value)});
        }
    }

    // Chooses the menu item whose shortcut `kp` is, unless the focused text widget keeps the
    // key (Ctrl+C copies there). Disabled menus and items never match.
    // simplify: every menu in the tree matches, also one in a tab that is not shown; scope
    // shortcuts to visible widgets when two tabs give the same shortcut different meanings.
    bool trigger_shortcut(const KeyPress& kp) {
        if (focused_edits_text() && text_widget_keeps(kp)) return false;
        return trigger_shortcut_in(root_, kp);
    }

    // Whether the widget shows its own overlay (an open dropdown list): it takes Escape first.
    [[nodiscard]] bool has_open_overlay(WidgetId id) const {
        auto it = index_.find(id);
        return it != index_.end() && widget_detail::has_open_overlay(*it->second);
    }

    [[nodiscard]] std::optional<WidgetId> parent_of(WidgetId id) const {
        auto it = parent_map_.find(id);
        return it != parent_map_.end() ? std::optional{it->second} : std::nullopt;
    }

    // Whether `id` is `ancestor` or below it.
    [[nodiscard]] bool within(WidgetId id, WidgetId ancestor) const {
        for (WidgetId cur = id; cur != 0;) {
            if (cur == ancestor) return true;
            auto it = parent_map_.find(cur);
            cur = it == parent_map_.end() ? 0 : it->second;
        }
        return false;
    }

    // That popup's card, in window coordinates, as last laid out. Routing reads it directly:
    // the anchor may be scrolled out of view, and then has no geometry to offset from.
    [[nodiscard]] std::optional<Rect> popup_card() const { return popup_card_; }

    // The open popup whose content is widgets (ui::Popup), if any.
    [[nodiscard]] const WidgetNode* open_content_popup() const {
        auto it = index_.find(popup_owner());
        if (popup_owner() == 0 || it == index_.end()) return nullptr;
        return it->second->popup_builder && !it->second->children.empty() ? it->second : nullptr;
    }

    void mark_dirty_by_id(WidgetId id) { set_dirty(id); }

    void set_debug_highlight(std::optional<WidgetId> id) {
        highlight_id_ = id;
        mark_dirty_by_id(root_.id);
    }

    [[nodiscard]] std::unique_ptr<SceneSnapshot> build_snapshot(float w, float h, uint64_t version) {
        last_built_version_ = version;
        auto build_start = std::chrono::steady_clock::now();
        auto window_colors = WindowColors{theme_.canvas_bg, theme_.chrome_bg, theme_.chrome_border,
                                          theme_.chrome_text, theme_.chrome_icon, theme_.chrome_close};
        // Counted before refresh_dirty() runs record() -- record() never touches the
        // dirty flag itself (only clear_dirty(), called by the caller after publish,
        // does), so this reflects exactly what changed since the last publish.
        std::size_t dirty_widgets = count_dirty(root_);
        refresh_dirty(root_);
        materialize_all_virtual_lists(root_);

        std::optional<PopupLayout> popup;   // the open popup's content, laid out by do_layout
        auto do_layout = [&] {
            LayoutNode layout;
            assert(root_.layout_kind != LayoutKind::Spacer);
            layout.kind = (root_.layout_kind == LayoutKind::Row)
                ? LayoutNode::Kind::Row : LayoutNode::Kind::Column;
            layout.id = root_.id;
            layout.source = &root_;
            layout.theme = &theme_;
            // ViewBuilder::finalize() hoists a lone top-level Row/Column into
            // root_ itself (see its id-adoption comment), so this root layout
            // node is built by hand here rather than via build_layout()'s
            // Row/Column branch -- it needs the same split_sizes read as that
            // branch, or an engaged split on the sole top-level container
            // would silently never apply.
            if (auto* ss = std::any_cast<SplitState>(&root_.edit_state); ss && ss->engaged)
                layout.split_sizes = ss->pane_sizes;
            for (auto& c : root_.children)
                build_layout(c, layout);

            layout_measure(layout, LayoutAxis::Vertical);
            layout_arrange(layout, {Point{X{0}, Y{0}}, Size{Width{w}, Height{h}}});
            update_scroll_state(layout);
            update_split_state(layout);
            drop_areas_.clear();
            collect_drop_areas(layout, DY{0}, drop_areas_);
            popup = layout_popup(root_, layout, Size{Width{w}, Height{h}}, theme_);
            popup_card_ = popup ? std::optional{popup->card} : std::nullopt;
            return layout;
        };

        std::vector<std::pair<WidgetId, Height>> viewports_before;
        collect_viewport_heights(root_, viewports_before);

        LayoutNode layout = do_layout();
        std::size_t layout_passes = 1;

        // Tables and virtual lists depend on viewport sizes set by update_scroll_state,
        // itself only known after this arrange pass -- on the first frame (or any real
        // resize) they were materialized against a stale/zero viewport, producing wrong
        // cell positions. Re-materialize and re-layout only when a viewport actually
        // changed, not on any unrelated dirty flag elsewhere in the tree: every publish
        // that touches a VirtualList/Table freshly rebinds its visible rows (marking them
        // dirty regardless of whether their viewport moved), so gating on check_dirty()
        // reran this whole second pass on every single publish for any app using one.
        std::vector<std::pair<WidgetId, Height>> viewports_after;
        collect_viewport_heights(root_, viewports_after);
        if (viewports_before != viewports_after) {
            materialize_all_virtual_lists(root_);
            layout = do_layout();
            ++layout_passes;
        }

        update_canvas_bounds(layout, Size{Width{w}, Height{h}});
        if (popup) update_canvas_bounds(popup->content, Size{Width{w}, Height{h}});
        adopt_popup(root_);

        auto snap = std::make_unique<SceneSnapshot>();
        snap->version = version;
        snap->window = window_colors;
        snap->viewport = Size{Width{w}, Height{h}};
        layout_flatten(layout, *snap);
        resolve_clips(*snap);
        if (popup) flatten_popup(*popup, *snap, theme_);
        if (highlight_id_) {
            for (auto& [id, rect] : snap->geometry) {
                if (id == *highlight_id_) {
                    snap->overlay.rect_outline(rect, Color::rgba(255, 140, 0), 2.0f);
                    break;
                }
            }
        }
        if (popup_owner() != 0) tooltip_ = tooltip_dismissed(tooltip_);   // e.g. a dialog the app opened
        if (auto text = tooltip_of(tooltip_.target); tooltip_.shown && !text.empty())
            draw_tooltip(snap->overlay, text, tooltip_.pointer, snap->viewport, theme_);
        if (dragging()) draw_drag(*snap);

        snap->dirty_widget_count = dirty_widgets;
        snap->layout_pass_count = layout_passes;
        snap->draw_command_count = snap->overlay.size();
        snap->approx_bytes = snap->overlay.approx_bytes();
        for (auto& dl : snap->draw_lists) {
            snap->draw_command_count += dl->size();
            snap->approx_bytes += dl->approx_bytes();
        }
        const auto build_end = std::chrono::steady_clock::now();
        snap->build_time_ms = std::chrono::duration<double, std::milli>(
            build_end - build_start).count();
        snap->built_at = build_end;
        return snap;
    }

private:
    WidgetNode root_;
    Theme theme_;
#if __cpp_impl_reflection
    // Address -> reflected member identifier, accumulated across every build_node_tree() call
    // (the top-level model and every nested component()) -- lives as long as the tree itself, so
    // tab()'s lazily-materialized content builders (which run well after the initial tree build)
    // can still resolve names. See ViewBuilder::apply_field_name.
    std::unordered_map<const void*, std::string_view> field_names_;   // also accessible names
#endif
    // Address -> tooltip, from [[=core::tooltip<...>]] or ViewBuilder::tooltip_for(); applied
    // when the field is placed, however it is placed.
    std::unordered_map<const void*, std::string> field_tooltips_;
    WidgetId next_id_ = 1;
    std::vector<WidgetId> popup_stack_;   // open popups, bottom to top (see popup_owner())
    WidgetId hovered_id_ = 0;
    WidgetId focused_id_ = 0;
    WidgetId captured_id_ = 0;
    std::optional<DragState> drag_;
    std::vector<std::pair<WidgetId, Rect>> drop_areas_;   // containers that take drops, outermost first
    std::unordered_map<std::string, WidgetNode> parked_;   // kept subtrees, by keep_key (park_kept)
    WindowOpener window_opener_;
    std::function<void(WindowId)> window_closer_;
    std::optional<WidgetId> highlight_id_;
    ScrollbarDrag scrollbar_drag_;
    SplitDrag split_drag_;
    std::vector<WidgetId> focus_order_;
    std::unordered_map<WidgetId, WidgetNode*> index_;
    TooltipState tooltip_;
    bool tooltip_dirty_ = false;
    std::optional<Rect> popup_card_;   // see popup_card()
    bool swallow_text_ = false;
    std::uint64_t last_built_version_ = 0;
    std::uint64_t input_frame_ = UINT64_MAX;   // none known: as new as it gets   // shown or hidden since the last publish

    void set_tooltip(TooltipState s) {
        if (s.shown != tooltip_.shown) tooltip_dirty_ = true;
        tooltip_ = s;
    }
    std::unordered_map<WidgetId, WidgetId> parent_map_;
    std::vector<std::function<void()>> drain_callbacks_;
    std::vector<std::function<void(std::function<void()>)>> waker_binders_;

    void collect_drains(const Node& node) {
        if (node.drain_fn)
            drain_callbacks_.push_back(node.drain_fn);
        if (node.bind_waker_fn)
            waker_binders_.push_back(node.bind_waker_fn);
        for (const auto& child : node.children)
            collect_drains(child);
    }

    // --- Node → WidgetNode conversion ---

    WidgetNode build_widget_node(Node& node) {
        if (auto kept = parked_.find(node.keep_key); !node.keep_key.empty() && kept != parked_.end()) {
            WidgetNode wn = std::move(kept->second);   // the widgets built before, state and all
            parked_.erase(kept);
            wn.reused = true;
            mark_all_dirty_impl(wn);   // redrawn at their new size
            return wn;
        }
        WidgetNode wn;
        wn.id = node.id;
        wn.layout_kind = node.layout_kind;
        wn.spacer_size = node.spacer_size;
        wn.theme = &theme_;
        wn.name = node.name;
        wn.tooltip = node.tooltip;
        wn.frame = node.frame;
        wn.focus_on_open = node.focus_on_open;
        wn.popup_builder = node.popup_builder;
        wn.drag = node.drag;
        wn.row_drag = node.row_drag;
        wn.build_content = node.build_content;
        wn.content_stale = node.content_stale;
        wn.keep_key = node.keep_key;
#ifdef PRISM_DEBUG_TOOLS_ENABLED
        wn.debug_name = node.debug_name;
#endif
        build_widget_kind(node, wn);
        if (!node.enablers.empty()) make_disableable(wn, node.enablers);
        if (node.split_get) restore_split(node, wn);
        return wn;
    }

    // Starts the panes at their remembered sizes; sizes for another number of panes (an older
    // layout) are ignored.
    static void restore_split(const Node& node, WidgetNode& wn) {
        wn.split_changed = node.split_set;
        auto sizes = node.split_get();
        auto panes = std::ranges::count_if(node.children, [](const Node& c) { return c.layout_kind != LayoutKind::Handle; });
        bool usable = std::ssize(sizes) == panes && std::ranges::all_of(sizes, [](float s) { return s > 0.f; });
        if (usable) wn.edit_state = SplitState{.engaged = true, .pane_sizes = std::move(sizes)};
    }

    void build_widget_kind(Node& node, WidgetNode& wn) {
        if (node.is_leaf) {
            wn.is_container = false;
            if (node.build_widget)
                node.build_widget(wn);
            if (node.layout_kind == LayoutKind::Canvas) {
                wn.canvas_min_width = node.canvas_min_width;
                wn.canvas_min_height = node.canvas_min_height;
            }
        } else {
            wn.is_container = true;
            if (node.layout_kind == LayoutKind::Scroll) {
                ScrollState ss;
                ss.scrollbar = node.scroll_bar_policy;
                ss.event_policy = node.scroll_event_policy;
                wn.edit_state = ss;
                if (node.build_widget)
                    node.build_widget(wn);  // Field<ScrollArea> overrides
            } else if (node.layout_kind == LayoutKind::VirtualList) {
                auto vls = std::make_shared<VirtualListState>();
                vls->item_count = ItemCount{node.vlist_item_count};
                vls->event_policy = node.scroll_event_policy;
                if (node.vlist_bind_row) vls->bind_row = node.vlist_bind_row;
                if (node.vlist_unbind_row) vls->unbind_row = node.vlist_unbind_row;
                wn.edit_state = vls;
                // Opt-in only: no existing .list() caller sets build_widget, so this is a no-op
                // for every VirtualList container except the one ViewBuilder::tree() builds below.
                if (node.build_widget)
                    node.build_widget(wn);
            } else if (node.layout_kind == LayoutKind::Table && node.table_state) {
                node.table_state->event_policy = node.scroll_event_policy;
                wn.edit_state = node.table_state;
                wn.focus_policy = FocusPolicy::tab_and_click;
            } else if (node.layout_kind == LayoutKind::Tabs && node.tabs_state) {
                wn.edit_state = node.tabs_state;
            }
            if (node.layout_kind != LayoutKind::VirtualList
                && node.layout_kind != LayoutKind::Table) {
                for (auto& child : node.children)
                    wn.children.push_back(build_widget_node(child));
            }
        }
    }

    void connect_on_change_dirty(Node& node, WidgetNode& wn) {
        auto id = wn.id;
        if (node.on_change) {
            wn.connections.push_back(
                node.on_change([this, id]() { set_dirty(id); })
            );
        }
    }

    void connect_deps_dirty(Node& node, WidgetNode& wn) {
        auto id = wn.id;
        for (auto& dep : node.dependencies) {
            wn.connections.push_back(
                dep([this, id]() { set_dirty(id); })
            );
        }
    }

    // While dragging: a marker on the target under the pointer, and the dragged widget (or the
    // part dragged) following the pointer where it was grabbed (app/drag_draw.hpp).
    void draw_drag(SceneSnapshot& snap) const {
        std::optional<Rect> target_rect;
        for (auto& [id, rect] : snap.geometry)
            if (id == drag_->target && id != 0) target_rect = rect;
        if (!target_rect) target_rect = drop_area_rect(drag_->target);
        if (auto t = index_.find(drag_->target); target_rect && t != index_.end())
            drag_draw::drop_marker(snap.overlay, *target_rect, drag_->pointer, t->second->drag, theme_.accent);
        auto it = index_.find(drag_->source);
        if (it == index_.end()) return;
        Point press{X{drag_->grab.dx.raw()}, Y{drag_->grab.dy.raw()}};
        auto area = it->second->drag.ghost_area ? it->second->drag.ghost_area(*it->second, press) : std::nullopt;
        auto ghost = drag_draw::ghost(it->second->draws, area, drag_->pointer - drag_->grab);
        snap.overlay.commands.insert(snap.overlay.commands.end(), ghost.commands.begin(), ghost.commands.end());
    }

    // A widget whose condition changed redraws (faded or not); one just disabled lets go of the
    // focus, hover, press and popup it held.
    void connect_enablers(Node& node, WidgetNode& wn) {
        for (auto& e : node.enablers)
            wn.connections.push_back(e.subscribe([this, id = wn.id] {
                auto it = index_.find(id);
                if (it == index_.end()) return;
                // Its rows (a list's, a table's) draw with it: all of them redraw.
                widget_detail::for_each_node(*it->second, [](WidgetNode& n) { n.dirty = true; });
                if (!enabled(id)) release_disabled(*it->second);
            }));
    }

    // Lets go of the focus, hover, press and popups held by `w` or anything below it.
    void release_disabled(WidgetNode& w) {
        widget_detail::for_each_node(w, [this](WidgetNode& n) {
            if (focused_id_ == n.id) clear_focus();
            if (captured_id_ == n.id) captured_id_ = 0;
            n.visual_state.hovered = n.visual_state.pressed = false;
            while (std::ranges::find(popup_stack_, n.id) != popup_stack_.end()) close_popup();
        });
    }

    bool trigger_shortcut_in(WidgetNode& n, const KeyPress& kp) {
        if (!is_enabled(n)) return false;
        if (n.try_shortcut && n.try_shortcut(kp, n)) return true;
        return std::ranges::any_of(n.children, [&](WidgetNode& c) { return trigger_shortcut_in(c, kp); });
    }

    [[nodiscard]] bool focused_edits_text() const {
        auto it = index_.find(focused_id_);
        return it != index_.end() && edits_text(*it->second);
    }

    [[nodiscard]] static bool edits_text(const WidgetNode& n) {
        if (!n.access) return false;
        switch (n.access().role) {
        case Role::TextInput: case Role::MultilineTextInput: case Role::PasswordInput:
        case Role::ComboBox: case Role::SpinButton:
            return true;
        default:
            return false;
        }
    }

    void connect_dirty(Node& node, WidgetNode& wn) {
        if (std::exchange(wn.reused, false)) return;   // put back: its subscriptions came with it
        connect_enablers(node, wn);
        if (node.is_leaf) {
            connect_on_change_dirty(node, wn);
            connect_deps_dirty(node, wn);
        } else {
            // Scroll containers with Field<ScrollArea> have their own on_change
            connect_on_change_dirty(node, wn);

            // Table: connect List<T> signals (RowStorage) and depends_on (ColumnStorage)
            if (node.layout_kind == LayoutKind::Table) {
                auto id = wn.id;
                // Rows move on a removal, or on any change while sorted (it may re-sort); a cell
                // update or an append to an unsorted table moves none, and a live feed updating
                // every frame must not make every click stale. A table tracking its rows knows
                // which changed (TableState::rows_tracked): only a removal makes it re-sort all.
                auto changed = [this, id, ts = node.table_state.get()](bool removal) {
                    if (ts) {
                        if (removal || !ts->rows_tracked) ts->order_stale = true;
                        if (removal || ts->sort.get()) ts->shifted_at = next_frame_version();
                    }
                    set_dirty(id);
                };
                auto data_changed = [changed] { changed(false); };
                if (node.vlist_on_insert) wn.connections.push_back(node.vlist_on_insert(0, data_changed));
                if (node.vlist_on_update) wn.connections.push_back(node.vlist_on_update(0, data_changed));
                if (node.vlist_on_remove) wn.connections.push_back(node.vlist_on_remove(0, [changed] { changed(true); }));
                for (auto& dep : node.dependencies)
                    wn.connections.push_back(dep(data_changed));
                return;
            }

            // Virtual list: connect List<T> signals
            if (node.layout_kind == LayoutKind::VirtualList) {
                auto id = wn.id;
                if (node.vlist_on_insert) {
                    wn.connections.push_back(
                        node.vlist_on_insert(0, [this, id]() {
                            auto it = index_.find(id);
                            if (it != index_.end()) {
                                if (auto* vls = get_vlist_state(*it->second))
                                    vls->item_count = ItemCount{vls->item_count.raw() + 1};
                            }
                            set_dirty(id);
                        })
                    );
                }
                if (node.vlist_on_remove) {
                    wn.connections.push_back(
                        node.vlist_on_remove(0, [this, id]() {
                            auto it = index_.find(id);
                            if (it != index_.end()) {
                                if (auto* vls = get_vlist_state(*it->second)) {
                                    if (vls->item_count.raw() > 0)
                                        vls->item_count = ItemCount{vls->item_count.raw() - 1};
                                }
                            }
                            set_dirty(id);
                        })
                    );
                }
                if (node.vlist_on_update) {
                    wn.connections.push_back(
                        node.vlist_on_update(0, [this, id]() {
                            set_dirty(id);
                        })
                    );
                }
                return; // no child nodes to recurse into
            }

            if (node.layout_kind == LayoutKind::Tabs) {
                if (node.children.size() >= 1 && wn.children.size() >= 1)
                    connect_dirty(node.children[0], wn.children[0]);
                auto id = wn.id;
                if (node.children.size() >= 1 && node.children[0].on_change) {
                    wn.connections.push_back(
                        node.children[0].on_change([this, id]() { set_dirty(id); })
                    );
                }
                return;
            }

            assert(node.children.size() == wn.children.size());
            for (size_t i = 0; i < node.children.size(); ++i)
                connect_dirty(node.children[i], wn.children[i]);
        }
    }

    // --- Node tree construction ---

    template <typename Model>
    Node build_node_tree(Model& model) {
        Node root;
        root.id = next_id_++;
        root.is_leaf = false;
#ifdef PRISM_DEBUG_TOOLS_ENABLED
#if __cpp_impl_reflection
        // Every component() nesting call (PRISM's primary composition mechanism) wraps its
        // sub-tree in a root Node via this function. Without this, that wrapper's debug_name
        // stays empty and its layout_kind default (LayoutKind::Default) leaks through as the
        // row's displayed name -- see the analogous fallback comment on check_unplaced_fields'
        // model_name (widget_tree_reflect.hpp).
        static constexpr std::string_view root_debug_name = std::meta::has_identifier(^^Model)
            ? std::meta::identifier_of(^^Model) : std::string_view{"<anonymous>"};
        root.debug_name = std::string(root_debug_name);
#endif
#endif

        if constexpr (requires(Model& m, ViewBuilder& vb) { m.view(vb); }) {
#if __cpp_impl_reflection
            record_field_names(model, field_names_, field_tooltips_);
#endif
            typename dependent_type<ViewBuilder, Model>::type vb{*this, root};
            model.view(vb);
            // vb.placed()/vb.finalize() are non-dependent calls on a non-dependent local
            // object; called directly here they'd require ViewBuilder complete at this point
            // in the file, which the forward-declared split (Steps 2-4) can't provide. Routing
            // them through a generic lambda makes the call dependent on the lambda's own
            // deduced template parameter, deferring the completeness check to instantiation
            // time -- the same mechanism model.view(vb) above already relies on.
            [&](auto& built) {
#if __cpp_impl_reflection
                check_unplaced_fields(model, built.placed());
#endif
                built.finalize();
            }(vb);
            if constexpr (requires(Model& m) { { m.drain() } -> std::same_as<void>; })
                root.drain_fn = [&model] { model.drain(); };
        }
#if __cpp_impl_reflection
        else {
            static constexpr auto members = std::define_static_array(
                std::meta::nonstatic_data_members_of(
                    ^^Model, std::meta::access_context::unchecked()));

            template for (constexpr auto m : members) {
                auto& member = model.[:m:];
                using M = std::remove_cvref_t<decltype(member)>;

                if constexpr (is_state_v<M>) {
                    // invisible observable — no widget
                } else if constexpr (is_channel_v<M>) {
                    // invisible observable — no widget, but still drained every tick since
                    // (unlike State<T>) it delivers cross-thread, not synchronously on send()
                    drain_callbacks_.push_back([&member] { member.drain_notifications(); });
                    waker_binders_.push_back(
                        [&member](std::function<void()> w) { member.set_waker(std::move(w)); });
                } else if constexpr (is_field_v<M>) {
                    auto leaf = node_leaf(member, next_id_);
                    leaf.name = std::string(std::meta::identifier_of(m));
                    leaf.tooltip = std::string(core::extract_string_annotation<m, core::tooltip_t>());
#ifdef PRISM_DEBUG_TOOLS_ENABLED
                    leaf.debug_name = leaf.name;
#endif
                    root.children.push_back(std::move(leaf));
                } else if constexpr (is_derived_v<M>) {
                    using Inner = std::remove_cvref_t<decltype(member.get())>;
                    root.children.push_back(node_readonly_leaf<Inner>(member, next_id_));
                    constexpr auto tip = core::extract_string_annotation<m, core::tooltip_t>();
                    root.children.back().tooltip = std::string(tip);
                } else if constexpr (is_shared_v<M>) {
                    using Inner = std::remove_cvref_t<decltype(member.get())>;
                    auto node = node_readonly_leaf<Inner>(member, next_id_);
                    node.drain_fn = [&member] { member.drain_notifications(); };
                    node.bind_waker_fn = [&member](std::function<void()> w) { member.set_waker(std::move(w)); };
                    constexpr auto tip = core::extract_string_annotation<m, core::tooltip_t>();
                    node.tooltip = std::string(tip);
                    root.children.push_back(std::move(node));
                } else if constexpr (is_component_v<M>) {
                    root.children.push_back(build_node_tree(member));
                }
            }
        }
#endif // __cpp_impl_reflection

        return root;
    }

    // --- WidgetNode tree utilities ---

    void build_index(WidgetNode& node) {
        index_[node.id] = &node;
        if (node.wire) {
            node.wire(node);
            node.wire = nullptr;
        }
        if (node.focus_policy != FocusPolicy::none &&
            (!node.is_container || node.layout_kind == LayoutKind::Table ||
             node.layout_kind == LayoutKind::VirtualList))
            focus_order_.push_back(node.id);
        for (auto& c : node.children) {
            parent_map_[c.id] = node.id;
            build_index(c);
        }
    }

    void propagate_theme(WidgetNode& node) {
        node.theme = &theme_;
        for (auto& child : node.children)
            propagate_theme(child);
    }

    void set_dirty(WidgetId id) {
        if (auto it = index_.find(id); it != index_.end())
            it->second->dirty = true;
    }

    void unindex_node(WidgetId id) {
        index_.erase(id);
        parent_map_.erase(id);
        std::erase(focus_order_, id);
    }

    // Takes the subtrees to keep (WidgetNode::keep_key) out of `node`, which is about to be
    // dropped: out of the index, but still wired and subscribed, ready to be put back.
    void park_kept(WidgetNode& node) {
        if (!node.keep_key.empty()) {
            unindex_ids(node);
            auto key = node.keep_key;
            parked_.insert_or_assign(std::move(key), std::move(node));
            return;
        }
        for (auto& c : node.children) park_kept(c);
    }
    void unindex_ids(WidgetNode& node) {
        unindex_node(node.id);
        for (auto& c : node.children) unindex_ids(c);
    }

    void unindex_subtree(WidgetNode& node) {
        unindex_node(node.id);
        node.connections.clear();
        for (auto& c : node.children)
            unindex_subtree(c);
    }

    // --- popups that hold widgets (doc/design/popup-content.md) ---

    // Builds the content when the popup opens (focusing its first field, remembering what had
    // the focus) and drops it when it closes (giving the focus back).
    void materialize_popup(WidgetNode& node) {
        bool built = !node.children.empty();
        bool rebuild = std::exchange(node.popup_rebuild, false);   // consumed even when not built yet
        if (built && node.popup_content_open && rebuild) {
            close_popup_content(node);
            built = false;
        }
        if (node.popup_content_open && !built) open_popup_content(node);
        else if (!node.popup_content_open && built) close_popup_content(node);
    }

    void open_popup_content(WidgetNode& node) {
        Node content;
        content.id = next_id_++;
        content.is_leaf = false;
        content.layout_kind = LayoutKind::Column;
        node.popup_builder(content);
        auto wn = build_widget_node(content);
        propagate_theme(wn);
        connect_dirty(content, wn);
        parent_map_[wn.id] = node.id;
        node.children.push_back(std::move(wn));
        build_index(node.children.back());
        widget_detail::refresh_dirty(node.children.back());
        node.focus_before_popup = focused_id_;
        auto inside = [&](WidgetId id) { return within(id, node.id); };
        auto asks = [&](WidgetId id) { auto it = index_.find(id); return it != index_.end() && it->second->focus_on_open; };
        if (auto it = std::ranges::find_if(focus_order_, [&](WidgetId id) { return inside(id) && asks(id); });
            it != focus_order_.end()) return set_focused(*it);
        for (WidgetId id : focus_order_)
            if (inside(id)) return set_focused(id);
        if (node.popup_modal) clear_focus();   // nothing to focus inside: keys reach nothing behind
    }

    void close_popup_content(WidgetNode& node) {
        for (auto& c : node.children) unindex_subtree(c);
        node.children.clear();
        node.popup_area.reset();
        if (!index_.contains(focused_id_)) focused_id_ = 0;   // it was inside the popup
        set_focused(std::exchange(node.focus_before_popup, 0));
    }

    void materialize_tabs(WidgetNode& node) {
        auto* ts = get_tabs_state_ptr(node);
        if (!ts || !ts->get_selected) return;
        if (node.children.size() < 2) return;

        size_t selected = ts->get_selected();
        if (selected == ts->active_tab) return;
        ts->active_tab = selected;

        auto& content_wn = node.children[1];

        for (auto& c : content_wn.children) park_kept(c);
        for (auto& c : content_wn.children)
            unindex_subtree(c);
        content_wn.children.clear();

        if (selected < ts->tab_node_builders.size()) {
            Node content_node;
            content_node.id = next_id_++;
            content_node.is_leaf = false;
            content_node.layout_kind = LayoutKind::Column;

            ts->tab_node_builders[selected](content_node);

            for (auto& child_node : content_node.children) {
                auto child_wn = build_widget_node(child_node);
                propagate_theme(child_wn);
                connect_dirty(child_node, child_wn);
                parent_map_[child_wn.id] = content_wn.id;
                content_wn.children.push_back(std::move(child_wn));
            }

            for (auto& c : content_wn.children)
                build_index(c);
        }
        content_wn.dirty = true;
    }

    void materialize_virtual_list(WidgetNode& node) {
        auto* vls = get_vlist_state(node);
        if (!vls || !vls->bind_row) return;

        // Measure item height from first item if not yet known
        if (vls->item_height.raw() <= 0.f && vls->item_count.raw() > 0) {
            WidgetNode probe;
            probe.id = next_id_++;
            probe.theme = &theme_;
            vls->bind_row(probe, 0);
            auto bb = probe.draws.bounding_box();
            vls->item_height = bb.extent.h;
            if (vls->unbind_row) vls->unbind_row(probe);
        }

        auto [new_start, new_end] = compute_visible_range(
            vls->item_count, vls->item_height, vls->scroll_offset,
            vls->viewport_h, vls->overscan);

        // Unbind all current children -> pool
        for (auto it = node.children.rbegin(); it != node.children.rend(); ++it) {
            unindex_node(it->id);
            if (vls->unbind_row) vls->unbind_row(*it);
            vls->pool.push_back(std::move(*it));
        }
        node.children.clear();

        // Bind children for visible range (wire after push to avoid dangling SenderHub pointers)
        size_t range_size = new_end.raw() - new_start.raw();
        node.children.reserve(range_size);
        for (size_t i = new_start.raw(); i < new_end.raw(); ++i) {
            WidgetNode wn;
            if (!vls->pool.empty()) {
                wn = std::move(vls->pool.back());
                vls->pool.pop_back();
            } else {
                wn.id = next_id_++;
            }
            wn.theme = &theme_;
            vls->bind_row(wn, i);
            if (node.enabled) make_disableable(wn, node.enabled);   // rows follow the list's flag
            wn.drag = node.row_drag ? node.row_drag(i) : DragRole{};
            parent_map_[wn.id] = node.id;
            if (wn.focus_policy != FocusPolicy::none)
                focus_order_.push_back(wn.id);
            node.children.push_back(std::move(wn));
        }

        // Wire and fix index pointers after all children are in final positions
        for (auto& c : node.children) {
            if (c.wire) {
                c.wire(c);
                c.wire = nullptr;
            }
            index_[c.id] = &c;
        }

        vls->visible_start = new_start;
        vls->visible_end = new_end;
    }

    // Moves every materialized row back into the row pool for reuse.
    void recycle_table_rows(WidgetNode& node, TableState& ts) {
        for (auto it = node.children.rbegin(); it != node.children.rend(); ++it) {
            unindex_node(it->id);
            ts.pool.push_back(std::move(*it));
        }
        node.children.clear();
    }

    // A press on the body hit-tested against a frame from before the rows moved: it would act
    // on whichever row has since taken that place.
    bool stale_body_press(const TableState& ts, const InputEvent& ev) const {
        auto* mb = std::get_if<MouseButton>(&ev);
        return mb && mb->pressed && !table_input_detail::in_header(ts, mb->position) && input_frame() < ts.shifted_at;
    }

    // Wires the input handler for selection, sorting, resizing and scrolling (once per node).
    void wire_table_input(WidgetNode& node, TableState* ts) {
        if (node.table_input_wired) return;
        node.table_input_wired = true;
        auto node_id = node.id;
        // A re-sort moves the rows, whether a header click or the app set it.
        node.connections.push_back(
            ts->sort.on_change().connect([ts, this](const auto&) { ts->shifted_at = next_frame_version(); }));
        node.connections.push_back(
            node.on_input.connect([ts, node_id, this](const InputEvent& ev) {
                auto it = index_.find(node_id);
                if (it == index_.end()) return;
                if (stale_body_press(*ts, ev)) return;
                auto result = handle_table_input(*ts, ev);
                if (result.cursor) it->second->visual_state.cursor = *result.cursor;
                if (result.repaint) it->second->dirty = true;
            })
        );
    }

    void materialize_table(WidgetNode& node) {
        auto* ts = get_table_state(node);
        if (!ts || !ts->source.row_count) return;

        if (ts->row_height.raw() <= 0.f)
            ts->row_height = Height{24.f};

        refresh_order(*ts);
        ts->auto_column_w = ts->column_count > 0
            ? std::max(120.f, ts->viewport_w.raw() / static_cast<float>(ts->column_count))
            : 120.f;
        ts->scroll_x = DX{std::min(ts->scroll_x.raw(), max_scroll_x(*ts))};
        auto edges = column_edges(*ts);

        size_t total_rows = ts->row_count();
        auto [new_start, new_end] = compute_visible_range(
            ItemCount{total_rows}, ts->row_height, ts->scroll_y,
            ts->viewport_h, ts->overscan);

        recycle_table_rows(node, *ts);

        node.children.reserve(new_end.raw() - new_start.raw());
        for (size_t i = new_start.raw(); i < new_end.raw(); ++i) {
            // id=0 is shared by every row (hit-test transparent, see paint_table_row);
            // registering it in index_/parent_map_ would alias to whichever
            // row was inserted last, so it's deliberately skipped here.
            node.children.push_back(paint_table_row(*ts, i, edges, theme_));
        }

        ts->visible_start = new_start;
        ts->visible_end = new_end;

        render_table_header(node, *ts, edges, theme_);
        wire_table_input(node, ts);
    }

    // A container built while the app runs (a dock): its content again from its builder, on the
    // first frame and whenever it is stale.
    void materialize_content(WidgetNode& node) {
        if (!node.children.empty() && !(node.content_stale && node.content_stale())) return;
        for (auto& c : node.children) park_kept(c);
        for (auto& c : node.children) unindex_subtree(c);
        node.children.clear();
        Node content;
        content.id = next_id_++;
        content.is_leaf = false;
        content.layout_kind = LayoutKind::Column;
        node.build_content(content);
        for (auto& child_node : content.children) {
            auto child_wn = build_widget_node(child_node);
            propagate_theme(child_wn);
            connect_dirty(child_node, child_wn);
            parent_map_[child_wn.id] = node.id;
            node.children.push_back(std::move(child_wn));
        }
        for (auto& c : node.children) build_index(c);
        node.dirty = true;
    }

    void materialize_all_virtual_lists(WidgetNode& node) {
        if (node.build_content) materialize_content(node);
        if (node.popup_builder) materialize_popup(node);
        if (node.layout_kind == LayoutKind::VirtualList)
            materialize_virtual_list(node);
        else if (node.layout_kind == LayoutKind::Table)
            materialize_table(node);
        else if (node.layout_kind == LayoutKind::Tabs)
            materialize_tabs(node);
        for (auto& c : node.children)
            materialize_all_virtual_lists(c);
    }

};

} // namespace prism::app

#include <prism/app/view_builder.hpp>
