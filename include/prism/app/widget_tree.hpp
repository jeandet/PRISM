#pragma once

#include <prism/delegates/dropdown_delegates.hpp>
#include <prism/delegates/menu_delegates.hpp>
#include <prism/delegates/dialog_delegates.hpp>
#include <prism/delegates/form_delegates.hpp>
#include <prism/delegates/tabs_delegates.hpp>
#include <prism/ui/layout.hpp>
#include <prism/core/list.hpp>
#include <prism/ui/table_ops.hpp>
#include <prism/ui/tree.hpp>
#include <prism/delegates/text_delegates.hpp>
#include <prism/core/traits.hpp>
#include <prism/ui/widget_node.hpp>
#include <prism/app/widget_tree_layout.hpp>
#include <prism/app/widget_tree_traversal.hpp>
#include <prism/app/table_input.hpp>
#include <prism/ui/popup_place.hpp>
#include <prism/ui/tooltip.hpp>
#if __cpp_impl_reflection
#include <prism/core/reflect.hpp>
#include <prism/core/reflect_annotations.hpp>
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
        if (auto it = index_.find(id); it != index_.end()) {
            it->second->on_input.emit(ev);
            track_popup(*it->second);
        }
    }

    // The widget whose popup is open (0 = none). One popup at a time.
    [[nodiscard]] WidgetId popup_owner() const { return popup_owner_; }
    [[nodiscard]] const WidgetNode* popup_node() const {
        auto it = index_.find(popup_owner_);
        return it != index_.end() ? it->second : nullptr;
    }
    void close_popup() {
        if (auto it = index_.find(popup_owner_); it != index_.end()) {
            WidgetNode& n = *it->second;
            n.popup_area.reset();
            n.popup_opened_from.reset();
            n.overlay_draws.clear();
            n.edit_state.reset();   // the owner's open/highlight state, as close_overlays does
            n.dirty = true;
        }
        popup_owner_ = 0;
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
        auto it = index_.find(popup_owner_);
        if (popup_owner_ == 0 || it == index_.end()) return;
        if (it->second->dirty && it->second->record) it->second->record(*it->second);
    }

    // Moves the open popup (owner coordinates), e.g. to keep it inside the window.
    void move_popup(Rect area) {
        if (auto it = index_.find(popup_owner_); it != index_.end() && it->second->popup_area) {
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
        if (auto it = index_.find(popup_owner_); popup_owner_ != 0 && (it == index_.end() || !it->second->popup_area))
            popup_owner_ = 0;
        adopt_popup_in(node);
    }

    void adopt_popup_in(WidgetNode& node) {
        const WidgetNode* owner = popup_node();
        if (owner && owner->popup_modal) return;
        if (node.popup_area && node.id != 0 && node.id != popup_owner_ && (!owner || node.popup_modal)) {
            if (owner) close_popup();
            popup_owner_ = node.id;
            if (node.popup_modal) return;
        }
        for (auto& c : node.children) adopt_popup_in(c);
    }

    // id == 0 ("no widget") is a no-op, letting callers skip their own zero checks.
    void track_popup(const WidgetNode& n) {
        if (n.popup_area) popup_owner_ = n.id;
        else if (popup_owner_ == n.id) popup_owner_ = 0;
    }

    void flip_visual_flag(WidgetId id, bool WidgetVisualState::* flag, bool value) {
        if (id == 0) return;
        if (auto it = index_.find(id); it != index_.end()) {
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
        captured_id_ = pressed ? id : 0;
    }

    [[nodiscard]] WidgetId captured_id() const { return captured_id_; }

    [[nodiscard]] CursorShape desired_cursor() const {
        WidgetId active = captured_id_ != 0 ? captured_id_ : hovered_id_;
        if (active == 0) return CursorShape::Default;
        auto it = index_.find(active);
        return it != index_.end() ? it->second->visual_state.cursor : CursorShape::Default;
    }

    struct ScrollbarDrag {
        WidgetId scroll_id = 0;
        Y anchor_y{0};       // mouse Y at drag start (absolute)
        DY anchor_offset{0}; // scroll offset at drag start
        Height viewport_h{0};
        Height content_h{0};
        Height thumb_h{0};
    };

    struct SplitDrag {
        WidgetId container_id = 0;
        size_t handle_index = 0;
        float anchor = 0.f;
        float orig_before = 0.f;
        float orig_after = 0.f;
    };

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
        auto& d = scrollbar_drag_;
        Height track_range = d.viewport_h - d.thumb_h;
        if (track_range.raw() <= 0) return;
        DY max_off = max_scroll(d.content_h, d.viewport_h);
        DY dy_pixels = mouse_y - d.anchor_y;
        float new_offset = std::clamp(
            d.anchor_offset.raw() + dy_pixels.raw() * max_off.raw() / track_range.raw(),
            0.f, max_off.raw());
        scroll_to(d.scroll_id, DY{new_offset});
    }

    void end_scrollbar_drag() {
        scrollbar_drag_ = {};
    }

    [[nodiscard]] bool in_scrollbar_drag() const { return scrollbar_drag_.scroll_id != 0; }

    void begin_split_drag(WidgetId container_id, size_t handle_index, float pos) {
        auto it = index_.find(container_id);
        if (it == index_.end()) return;
        auto& container_wn = *it->second;
        auto& ss = container_wn.get_or_create<SplitState>();
        bool vertical = (container_wn.layout_kind == LayoutKind::Column);
        if (!ss.engaged) {
            ss.pane_sizes.clear();
            for (auto& child : container_wn.children) {
                if (child.layout_kind == LayoutKind::Handle) continue;
                ss.pane_sizes.push_back(vertical ? child.arranged_extent.h.raw()
                                                  : child.arranged_extent.w.raw());
            }
            ss.engaged = true;
        }
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
        float delta = pos - split_drag_.anchor;
        float before = split_drag_.orig_before + delta;
        float after = split_drag_.orig_after - delta;
        if (before < splitter::min_pane_size_px) {
            after -= (splitter::min_pane_size_px - before);
            before = splitter::min_pane_size_px;
        }
        if (after < splitter::min_pane_size_px) {
            before -= (splitter::min_pane_size_px - after);
            after = splitter::min_pane_size_px;
        }
        ss->pane_sizes[split_drag_.handle_index] = before;
        ss->pane_sizes[split_drag_.handle_index + 1] = after;
        set_dirty(split_drag_.container_id);
    }

    void end_split_drag() {
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
        if (id == focused_id_) return;
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
        if (!p) return focus_order_;
        std::vector<WidgetId> ring;
        for (WidgetId id : focus_order_)
            if (within(id, p->id)) ring.push_back(id);
        return ring;
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
        auto it = index_.find(popup_owner_);
        if (popup_owner_ == 0 || it == index_.end()) return nullptr;
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
            popup = layout_popup(layout, Size{Width{w}, Height{h}});
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
        snap->viewport = Size{Width{w}, Height{h}};
        layout_flatten(layout, *snap);
        resolve_clips(*snap);
        if (popup) flatten_popup(*popup, *snap);
        if (highlight_id_) {
            for (auto& [id, rect] : snap->geometry) {
                if (id == *highlight_id_) {
                    snap->overlay.rect_outline(rect, Color::rgba(255, 140, 0), 2.0f);
                    break;
                }
            }
        }
        if (popup_owner_ != 0) tooltip_ = tooltip_dismissed(tooltip_);   // e.g. a dialog the app opened
        if (auto text = tooltip_of(tooltip_.target); tooltip_.shown && !text.empty())
            draw_tooltip(snap->overlay, text, tooltip_.pointer, snap->viewport, theme_);

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
    WidgetId popup_owner_ = 0;
    WidgetId hovered_id_ = 0;
    WidgetId focused_id_ = 0;
    WidgetId captured_id_ = 0;
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
        WidgetNode wn;
        wn.id = node.id;
        wn.layout_kind = node.layout_kind;
        wn.theme = &theme_;
        wn.name = node.name;
        wn.tooltip = node.tooltip;
        wn.popup_builder = node.popup_builder;
#ifdef PRISM_DEBUG_TOOLS_ENABLED
        wn.debug_name = node.debug_name;
#endif
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
        return wn;
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

    void connect_dirty(Node& node, WidgetNode& wn) {
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
                // every frame must not make every click stale.
                auto changed = [this, id, ts = node.table_state.get()](bool removal) {
                    if (ts) {
                        ts->order_stale = true;
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
        // model_name below.
        static constexpr std::string_view root_debug_name = std::meta::has_identifier(^^Model)
            ? std::meta::identifier_of(^^Model) : std::string_view{"<anonymous>"};
        root.debug_name = std::string(root_debug_name);
#endif
#endif

        if constexpr (requires(Model& m, ViewBuilder& vb) { m.view(vb); }) {
#if __cpp_impl_reflection
            // view()-driven placement (vb.widget(field)) only ever sees a Field<T>&, never the
            // member identifier that placed it -- unlike the whole-model reflection branch below,
            // which walks Model's own members and knows both. Recover the identifier here, keyed
            // by address, so ViewBuilder::apply_field_name can resolve it for whichever field
            // view() happens to place.
            static constexpr auto own_members = std::define_static_array(
                std::meta::nonstatic_data_members_of(
                    ^^Model, std::meta::access_context::unchecked()));
            template for (constexpr auto m : own_members) {
                auto& member = model.[:m:];
                using M = std::remove_cvref_t<decltype(member)>;
                if constexpr (is_field_v<M> || is_derived_v<M> || is_shared_v<M>) {
                    field_names_.emplace(static_cast<const void*>(&member),
                                         std::meta::identifier_of(m));
                    if constexpr (constexpr auto tip = core::extract_string_annotation<m, core::tooltip_t>(); !tip.empty())
                        field_tooltips_.emplace(static_cast<const void*>(&member), tip);
                }
            }
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

#if __cpp_impl_reflection
    template <typename Model>
    void check_unplaced_fields([[maybe_unused]] Model& model,
                               [[maybe_unused]] const std::set<const void*>& placed) {
#ifndef NDEBUG
        static constexpr auto members = std::define_static_array(
            std::meta::nonstatic_data_members_of(
                ^^Model, std::meta::access_context::unchecked()));
        // Model can be a class-template specialization reached through a deduced function-
        // template parameter (e.g. LeafSlot<M, ReadOnly>) -- on this reflection implementation
        // such a ^^Model reports has_identifier() == false, and identifier_of() would throw as
        // a consteval call. Fall back to a placeholder rather than crash the build; this is a
        // debug-only diagnostic string, not something callers parse. See the analogous
        // Shared<T>& -> Shared<T>* workaround in inspector.hpp for the same underlying bug.
        static constexpr std::string_view model_name = std::meta::has_identifier(^^Model)
            ? std::meta::identifier_of(^^Model) : std::string_view{"<anonymous>"};
        template for (constexpr auto m : members) {
            auto& member = model.[:m:];
            using M = std::remove_cvref_t<decltype(member)>;
            if constexpr (is_field_v<M> || is_derived_v<M> || is_shared_v<M>) {
                if (!placed.contains(&member)) {
                    std::fprintf(stderr, "[prism] warning: Field '%.*s' in %.*s not placed by view()\n",
                        static_cast<int>(std::meta::identifier_of(m).size()),
                        std::meta::identifier_of(m).data(),
                        static_cast<int>(model_name.size()),
                        model_name.data());
                }
            }
        }
#endif
    }
#endif // __cpp_impl_reflection

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

    void update_scroll_state(LayoutNode& layout_node) {
        if (layout_node.kind == LayoutNode::Kind::Scroll) {
            auto it = index_.find(layout_node.id);
            if (it != index_.end()) {
                auto& ss = ensure_scroll_state(*it->second);
                ss.viewport_h = layout_node.allocated.extent.h;
                ss.viewport_w = layout_node.allocated.extent.w;
                ss.content_h = layout_node.scroll_content_h;
            }
        }
        if (layout_node.kind == LayoutNode::Kind::VirtualList) {
            auto it = index_.find(layout_node.id);
            if (it != index_.end()) {
                if (auto* vls = get_vlist_state(*it->second))
                    vls->viewport_h = layout_node.allocated.extent.h;
            }
        }
        if (layout_node.kind == LayoutNode::Kind::Table) {
            auto it = index_.find(layout_node.id);
            if (it != index_.end()) {
                if (auto* ts = get_table_state(*it->second)) {
                    bool was_zero = ts->viewport_w.raw() == 0.f;
                    ts->viewport_h = Height{layout_node.allocated.extent.h.raw() - ts->row_height.raw()};
                    ts->viewport_w = layout_node.allocated.extent.w;
                    if (was_zero && ts->viewport_w.raw() > 0.f)
                        it->second->dirty = true;
                }
            }
        }
        auto it = index_.find(layout_node.id);
        if (it != index_.end()) {
            if (auto sv = get_scroll_view(*it->second)) {
                DY max_off = max_scroll(sv->content_h, sv->viewport_h);
                sv->offset = DY{std::clamp(sv->offset.raw(), 0.f, max_off.raw())};
                layout_node.scroll_offset = sv->offset;
                if (sv->show_ticks > 0) sv->show_ticks--;
            }
        }
        for (auto& child : layout_node.children)
            update_scroll_state(child);
    }

    void update_split_state(LayoutNode& layout_node) {
        bool is_split_container = (layout_node.kind == LayoutNode::Kind::Row
                                 || layout_node.kind == LayoutNode::Kind::Column)
                                 && !layout_node.split_sizes.empty();
        if (is_split_container) {
            auto it = index_.find(layout_node.id);
            if (it != index_.end()) {
                if (auto* ss = std::any_cast<SplitState>(&it->second->edit_state); ss && ss->engaged) {
                    bool vertical = (layout_node.kind == LayoutNode::Kind::Column);
                    float available = vertical ? layout_node.allocated.extent.h.raw()
                                                : layout_node.allocated.extent.w.raw();
                    float handle_total = 0.f;
                    for (auto& child : layout_node.children)
                        if (child.kind == LayoutNode::Kind::Handle)
                            handle_total += splitter::thickness_px;
                    float pane_total = 0.f;
                    for (float sz : ss->pane_sizes) pane_total += sz;
                    float target = available - handle_total;
                    if (pane_total > 0.f && std::abs(target - pane_total) > 0.5f) {
                        float scale = target / pane_total;
                        for (auto& sz : ss->pane_sizes) sz *= scale;
                    }
                }
            }
        }
        for (auto& child : layout_node.children)
            update_split_state(child);
    }

    void update_canvas_bounds(LayoutNode& layout_node, Size viewport) {
        // Propagate absolute position and the window size to all indexed widget nodes
        bool window_changed = false;
        if (layout_node.id != 0) {
            auto it = index_.find(layout_node.id);
            if (it != index_.end()) {
                auto& n = *it->second;
                window_changed = n.viewport_height != viewport.h || n.viewport_width != viewport.w
                              || n.absolute_x != layout_node.allocated.origin.x
                              || n.absolute_y != layout_node.allocated.origin.y;
                n.absolute_x = layout_node.allocated.origin.x;
                n.absolute_y = layout_node.allocated.origin.y;
                n.viewport_height = viewport.h;
                n.viewport_width = viewport.w;
                n.arranged_extent = layout_node.allocated.extent;
            }
        }

        // Re-record leaf/canvas/handle widgets after layout so delegates can use their
        // allocated size instead of a pre-layout guess -- but only when that size actually
        // differs from wn->canvas_bounds (the size wn->draws was last produced at, whether
        // by refresh_dirty()'s pre-layout call this same build_snapshot(), or by this same
        // re-record last frame). No widget here sizes its own record() output from its field
        // value independent of allocated size (bounding_box() never measures text/points by
        // content, see draw_list.hpp), so if the size didn't move, the currently-held draws
        // are still correct -- skipping avoids a second full record() for every leaf/canvas/
        // handle in the window on every publish, including ones untouched by whatever else
        // made the window dirty (see benchmarks/stall_latency.cpp's
        // untouched_sibling_record_calls, and tests/test_record_reuse.cpp).
        if (layout_node.kind == LayoutNode::Kind::Leaf ||
            layout_node.kind == LayoutNode::Kind::Canvas ||
            layout_node.kind == LayoutNode::Kind::Handle) {
            auto* wn = layout_node.widget;   // set by build_layout() for these three kinds
            // layout_node reads wn->draws in place (ui::content_draws), so a re-record here is
            // what layout_flatten() sees; overlay_draws is still a copy and is refreshed.
            // Popup anchors place their popup relative to the window, so they also re-record
            // when the window or their position in it changes.
            bool resized = wn && wn->canvas_bounds.extent != layout_node.allocated.extent;
            if (wn && wn->record && (resized || (window_changed && wn->popup_anchor))) {
                wn->canvas_bounds = Rect{
                    Point{X{0}, Y{0}},
                    layout_node.allocated.extent
                };
                wn->record(*wn);
                layout_node.overlay_draws = wn->overlay_draws;
            }
            return;
        }
        for (auto& child : layout_node.children)
            update_canvas_bounds(child, viewport);
    }

    void unindex_node(WidgetId id) {
        index_.erase(id);
        parent_map_.erase(id);
        std::erase(focus_order_, id);
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
        for (WidgetId id : focus_order_)
            if (within(id, node.id)) return set_focused(id);
        if (node.popup_modal) clear_focus();   // nothing to focus inside: keys reach nothing behind
    }

    void close_popup_content(WidgetNode& node) {
        for (auto& c : node.children) unindex_subtree(c);
        node.children.clear();
        node.popup_area.reset();
        if (!index_.contains(focused_id_)) focused_id_ = 0;   // it was inside the popup
        set_focused(std::exchange(node.focus_before_popup, 0));
    }

    struct PopupLayout {
        WidgetNode* anchor;
        LayoutNode content;
        Rect card;   // window coordinates
    };

    static constexpr float popup_pad = 16.f;      // between the card's edge and its content
    static constexpr float popup_margin = 24.f;   // kept free between the card and the window edge
    static constexpr float popup_min_w = 320.f;

    // The open popup's content, measured, given a card (a modal one centred, a non-modal one
    // next to its anchor, both within the window) and arranged in it. Also sets the anchor's
    // popup_area, which routing reads.
    std::optional<PopupLayout> layout_popup(const LayoutNode& main, Size window) {
        WidgetNode* anchor = find_open_popup(root_);
        popup_card_.reset();
        if (!anchor) return std::nullopt;
        PopupLayout p{anchor, {}, {}};
        auto& content_wn = anchor->children.front();
        p.content.kind = LayoutNode::Kind::Column;
        p.content.id = content_wn.id;
        p.content.theme = &theme_;
        for (auto& c : content_wn.children) build_layout(c, p.content);
        layout_measure(p.content, LayoutAxis::Vertical);
        float max_w = std::max(0.f, window.w.raw() - 2 * popup_margin);
        float max_h = std::max(0.f, window.h.raw() - 2 * popup_margin);
        Size size{Width{std::min(max_w, std::max(popup_min_w, p.content.hint.cross + 2 * popup_pad))},
                  Height{std::min(max_h, p.content.hint.preferred + 2 * popup_pad)}};
        // simplify: the anchor's position before any scroll area above it scrolls; a non-modal
        // popup declared inside a scrolled area opens off its anchor. Upgrade: offset by the
        // ancestors' scroll offsets.
        Point at = anchor_origin(main, anchor->id);
        p.card = anchor->popup_modal
            ? Rect{Point{X{(window.w.raw() - size.w.raw()) / 2}, Y{(window.h.raw() - size.h.raw()) / 2}}, size}
            : place_popup(size, Rect{at, Size{Width{0}, Height{0}}}, window);
        layout_arrange(p.content, Rect{Point{p.card.origin.x + DX{popup_pad}, p.card.origin.y + DY{popup_pad}},
                                       Size{size.w - Width{2 * popup_pad}, size.h - Height{2 * popup_pad}}});
        update_scroll_state(p.content);
        popup_card_ = p.card;
        anchor->popup_area = Rect{Point{X{p.card.origin.x.raw() - at.x.raw()}, Y{p.card.origin.y.raw() - at.y.raw()}}, size};
        return p;
    }

    static WidgetNode* find_open_popup(WidgetNode& node) {
        if (node.popup_builder && !node.children.empty()) return &node;
        for (auto& c : node.children)
            if (auto* p = find_open_popup(c)) return p;
        return nullptr;
    }

    static Point anchor_origin(const LayoutNode& node, WidgetId id) {
        if (node.id == id) return node.allocated.origin;
        for (auto& c : node.children) {
            Point p = anchor_origin(c, id);
            if (p.x.raw() != -1.f) return p;
        }
        return Point{X{-1.f}, Y{-1.f}};
    }

    // Into the overlay, which draws after and is hit-tested before the window's widgets:
    // a scrim behind a modal popup, the card, then the content clipped to the card.
    void flatten_popup(PopupLayout& p, SceneSnapshot& snap) {
        if (p.anchor->popup_modal) snap.overlay.filled_rect(Rect{Point{X{0}, Y{0}}, snap.viewport}, theme_.scrim);
        snap.overlay.rounded_rect(p.card, theme_.popup_bg, 8.f);
        snap.overlay.rounded_rect(p.card, theme_.popup_border, 8.f, 1.f);
        snap.overlay_geometry.push_back({p.anchor->id, p.card});   // the card's own background

        SceneSnapshot content;
        content.viewport = snap.viewport;
        auto add = [&](WidgetId id, Rect r, DrawList dl) {
            content.geometry.push_back({id, r});
            content.draw_lists.push_back(std::make_shared<DrawList>(std::move(dl)));
            content.z_order.push_back(static_cast<uint32_t>(content.geometry.size() - 1));
        };
        DrawList open_clip, close_clip;
        open_clip.clip_push(p.card.origin, p.card.extent);
        close_clip.clip_pop();
        add(0, Rect{}, std::move(open_clip));
        layout_flatten(p.content, content, p.card);
        add(0, Rect{}, std::move(close_clip));
        resolve_clips(content);   // nested clips (a scroll area) cut to the card

        for (auto idx : content.z_order)
            if (content.draw_lists[idx])
                for (const auto& cmd : content.draw_lists[idx]->commands) snap.overlay.commands.push_back(cmd);
        for (auto& [id, r] : content.geometry) {
            Rect visible = p.card.intersect(r);
            if (id != 0 && visible.extent.w.raw() > 0 && visible.extent.h.raw() > 0)
                snap.overlay_geometry.push_back({id, visible});
        }
        for (const auto& cmd : content.overlay.commands) snap.overlay.commands.push_back(cmd);
        for (auto& g : content.overlay_geometry) snap.overlay_geometry.push_back(g);
    }

    void materialize_tabs(WidgetNode& node) {
        auto* ts = get_tabs_state_ptr(node);
        if (!ts || !ts->get_selected) return;
        if (node.children.size() < 2) return;

        size_t selected = ts->get_selected();
        if (selected == ts->active_tab) return;
        ts->active_tab = selected;

        auto& content_wn = node.children[1];

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

    // Paints one row (recycled from the pool when possible): zebra/selection
    // background, per-cell clipped text, and column dividers.
    WidgetNode paint_table_row(TableState& ts, size_t view_pos, const std::vector<float>& edges) {
        WidgetNode wn;
        if (!ts.pool.empty()) {
            wn = std::move(ts.pool.back());
            ts.pool.pop_back();
        }
        wn.id = 0;  // cells are hit-test transparent; the table container handles input
        wn.dirty = true;
        wn.draws.clear();
        wn.theme = &theme_;

        size_t row = source_row(ts, view_pos);
        auto bg = is_selected(ts, row) ? theme_.table_selected
                : view_pos % 2 == 0    ? theme_.table_row_even
                                       : theme_.table_row_odd;
        float dx = -ts.scroll_x.raw();
        Width total_w{std::max(edges.back() + dx, ts.viewport_w.raw())};
        wn.draws.filled_rect(Rect{Point{X{0}, Y{0}}, Size{total_w, ts.row_height}}, bg);

        for (size_t c = 0; c < ts.column_count; ++c) {
            X cx{edges[c] + dx};
            Width cw{edges[c + 1] - edges[c]};
            wn.draws.clip_push(Point{cx, Y{0}}, Size{cw, ts.row_height});
            wn.draws.text(ts.source.cell_text(row, c), Point{X{4.f}, Y{4.f}}, 14.f, theme_.text);  // clip-relative
            wn.draws.clip_pop();
            if (c > 0)
                wn.draws.filled_rect(Rect{Point{cx, Y{0}}, Size{Width{1.f}, ts.row_height}},
                                     theme_.table_divider);
        }
        return wn;
    }

    static void draw_sort_arrow(DrawList& dl, X right, bool descending, Color c) {
        float x = right.raw() - 14.f, y = 12.f, w = 8.f, h = 5.f;
        if (descending)
            dl.filled_polygon({Point{X{x}, Y{y - h / 2}}, Point{X{x + w}, Y{y - h / 2}},
                               Point{X{x + w / 2}, Y{y + h / 2}}}, c);
        else
            dl.filled_polygon({Point{X{x + w / 2}, Y{y - h / 2}}, Point{X{x + w}, Y{y + h / 2}},
                               Point{X{x}, Y{y + h / 2}}}, c);
    }

    // Renders header cells into overlay_draws (picked up by table flatten).
    void render_table_header(WidgetNode& node, const TableState& ts, const std::vector<float>& edges) {
        node.overlay_draws.clear();
        auto sort = ts.sort.get();
        float dx = -ts.scroll_x.raw();
        for (size_t c = 0; c < ts.column_count; ++c) {
            X cx{edges[c] + dx};
            X right{edges[c + 1] + dx};
            Width cw{edges[c + 1] - edges[c]};
            node.overlay_draws.clip_push(Point{cx, Y{0}}, Size{cw, ts.row_height});  // origin moves to cx
            node.overlay_draws.text(std::string(ts.column_header(c)), Point{X{4.f}, Y{4.f}},
                                    14.f, theme_.table_header_text);
            if (sort && sort->column == c)
                draw_sort_arrow(node.overlay_draws, X{cw.raw()}, sort->descending, theme_.table_header_text);
            node.overlay_draws.clip_pop();
            node.overlay_draws.filled_rect(Rect{Point{right, Y{4.f}},
                                                Size{Width{1.f}, ts.row_height - Height{8.f}}},
                                           theme_.table_header_divider);
        }
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
            node.children.push_back(paint_table_row(*ts, i, edges));
        }

        ts->visible_start = new_start;
        ts->visible_end = new_end;

        render_table_header(node, *ts, edges);
        wire_table_input(node, ts);
    }

    void materialize_all_virtual_lists(WidgetNode& node) {
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
