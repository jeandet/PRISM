#include <prism/app/widget_tree.hpp>

#include <prism/app/block_outline.hpp>
#include <prism/app/drag_draw.hpp>
#include <prism/app/layout_writeback.hpp>
#include <prism/app/popup_layout.hpp>
#include <prism/app/table_paint.hpp>
#include <prism/input/hit_test.hpp>
#include <prism/core/list.hpp>
#include <prism/ui/dock.hpp>
#include <prism/ui/popup_place.hpp>
#include <prism/ui/table_ops.hpp>
#include <prism/ui/tree.hpp>

#include <algorithm>
#include <any>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <utility>

namespace prism::app {

void WidgetTree::set_text_measurer(const TextMeasurer* m) {
    if (theme_.measurer == m) return;
    theme_.measurer = m;
    mark_all_dirty_impl(root_);
}

void WidgetTree::set_theme(Theme t) {
    t.measurer = theme_.measurer;
    t.clipboard = theme_.clipboard;
    t.url_opener = theme_.url_opener;
    theme_ = t;
    mark_all_dirty_impl(root_);
    tooltip_dirty_ = true;
}

void WidgetTree::scroll_at(WidgetId target, DY delta, WidgetId stop) {
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

void WidgetTree::scroll_to(WidgetId id, DY offset) {
    auto it = index_.find(id);
    if (it == index_.end()) return;
    auto sv = get_scroll_view(*it->second);
    if (!sv) return;
    DY max_off = max_scroll(sv->content_h, sv->viewport_h);
    sv->offset = DY{std::clamp(offset.raw(), 0.f, max_off.raw())};
    sv->show_ticks = 30;
    set_dirty(id);
}

void WidgetTree::scroll_row_into_view(WidgetId container_id, size_t row_index, Height row_h) {
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

void WidgetTree::dispatch(WidgetId id, const InputEvent& ev) {
    if (auto it = index_.find(id); it != index_.end() && is_enabled(*it->second)) {
        it->second->on_input.emit(ev);
        track_popup(*it->second);
    }
}

void WidgetTree::close_popup() {
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

[[nodiscard]] std::optional<WidgetId> WidgetTree::menu_neighbour(WidgetId from, int dir) const {
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

[[nodiscard]] const WidgetNode& WidgetTree::container_at(const WidgetNode& n, Point p) const {
    for (const auto& c : n.children)
        if (c.is_container && Rect{Point{c.absolute_x, c.absolute_y}, c.arranged_extent}.contains(p))
            return container_at(c, p);
    return n;
}

[[nodiscard]] std::optional<WidgetId> WidgetTree::context_menu_for(std::optional<WidgetId> hit, Point p) const {
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

void WidgetTree::refresh_popup_owner() {
    auto it = index_.find(popup_owner());
    if (popup_owner() == 0 || it == index_.end()) return;
    if (it->second->dirty && it->second->record) it->second->record(*it->second);
}

void WidgetTree::release_closed_popup() {
    refresh_popup_owner();
    auto it = index_.find(popup_owner());
    if (popup_owner() != 0 && it != index_.end() && !it->second->popup_area) close_popup();
}

[[nodiscard]] std::optional<WidgetId> WidgetTree::menu_for_key(std::int32_t key) const {
    if (key == 0) return std::nullopt;
    for (WidgetId id : leaf_ids()) {
        auto it = index_.find(id);
        if (it == index_.end()) continue;
        const WidgetNode& n = *it->second;
        if (n.opens_menu && n.mnemonic_key && n.mnemonic_key() == key) return id;
    }
    return std::nullopt;
}

void WidgetTree::drop_closed_popups() {
    while (!popup_stack_.empty()) {
        auto it = index_.find(popup_stack_.back());
        if (it != index_.end() && it->second->popup_area) return;
        popup_stack_.pop_back();
    }
}

void WidgetTree::adopt_popup_in(WidgetNode& node) {
    if (node.layout_reusable) return;   // no popup in a kept subtree (link_layout_cache)
    if (node.popup_area && node.id != 0 && !in_popup_stack(node.id) && adopts(node)) {
        if (!nested_in_top(node.id)) close_popup();   // a modal popup over a plain one
        popup_stack_.push_back(node.id);
    }
    for (auto& c : node.children) adopt_popup_in(c);
}

void WidgetTree::track_popup(const WidgetNode& n) {
    if (n.popup_area) {
        if (in_popup_stack(n.id)) return;
        while (!popup_stack_.empty() && !within(n.id, popup_stack_.back())) popup_stack_.pop_back();
        popup_stack_.push_back(n.id);
    } else if (popup_owner() == n.id) {
        popup_stack_.pop_back();
    }
}

void WidgetTree::flip_visual_flag(WidgetId id, bool WidgetVisualState::* flag, bool value) {
    if (id == 0) return;
    if (auto it = index_.find(id); it != index_.end() && (!value || is_enabled(*it->second))) {
        it->second->visual_state.*flag = value;
        it->second->dirty = true;
    }
}

void WidgetTree::update_hover(std::optional<WidgetId> id) {
    WidgetId new_id = id.value_or(0);
    if (new_id == hovered_id_) return;
    flip_visual_flag(hovered_id_, &WidgetVisualState::hovered, false);
    hovered_id_ = new_id;
    flip_visual_flag(hovered_id_, &WidgetVisualState::hovered, true);
}

[[nodiscard]] std::optional<TooltipState::clock::time_point> WidgetTree::next_wake() const {
    auto wake = tooltip_wake();
    auto earliest = [&](auto& self, const WidgetNode& n) -> void {
        if (n.redraw_at && (!wake || *n.redraw_at < *wake)) wake = n.redraw_at;
        for (auto& c : n.children) self(self, c);
    };
    earliest(earliest, root_);
    return wake;
}

void WidgetTree::tick(TooltipState::clock::time_point now) {
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

[[nodiscard]] CursorShape WidgetTree::desired_cursor() const {
    if (dragging()) return drag_->target != 0 ? CursorShape::Default : CursorShape::NotAllowed;
    WidgetId active = captured_id_ != 0 ? captured_id_ : hovered_id_;
    if (active == 0) return CursorShape::Default;
    auto it = index_.find(active);
    return it != index_.end() && is_enabled(*it->second) ? it->second->visual_state.cursor : CursorShape::Default;
}

void WidgetTree::begin_scrollbar_drag(WidgetId id, Y mouse_y) {
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
    captured_button_ = input::buttons::left;
}

void WidgetTree::update_scrollbar_drag(Y mouse_y) {
    if (scrollbar_drag_.scroll_id == 0) return;
    if (auto offset = scrollbar_drag_offset(scrollbar_drag_, mouse_y))
        scroll_to(scrollbar_drag_.scroll_id, *offset);
}

void WidgetTree::begin_split_drag(WidgetId container_id, size_t handle_index, float pos) {
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

void WidgetTree::update_split_drag(float pos) {
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

void WidgetTree::end_split_drag() {
    if (auto it = index_.find(split_drag_.container_id); it != index_.end() && it->second->split_changed)
        if (auto* ss = std::any_cast<SplitState>(&it->second->edit_state))
            it->second->split_changed(ss->pane_sizes);
    split_drag_ = {};
}

void WidgetTree::set_focused(WidgetId id) {
    if (id == focused_id_ || !enabled(id)) return;
    if (std::find(focus_order_.begin(), focus_order_.end(), id) == focus_order_.end()) return;
    flip_visual_flag(focused_id_, &WidgetVisualState::focused, false);
    focused_id_ = id;
    flip_visual_flag(focused_id_, &WidgetVisualState::focused, true);
}

[[nodiscard]] std::vector<WidgetId> WidgetTree::focus_ring() const {
    const WidgetNode* p = open_content_popup();
    std::vector<WidgetId> ring;
    const auto tabs_to = [this](WidgetId id) { return index_.at(id)->focus_policy == FocusPolicy::tab_and_click; };
    for (WidgetId id : focus_order_)
        if ((!p || within(id, p->id)) && enabled(id) && tabs_to(id)) ring.push_back(id);
    return ring;
}

// The first of `order` after `from` (before it when `dir` < 0) that is in `ring`, wrapping
// around; from a widget not in `order`, the ring's first (last).
static WidgetId next_in_ring(const std::vector<WidgetId>& order, const std::vector<WidgetId>& ring,
                             WidgetId from, int dir) {
    const auto at = std::ranges::find(order, from);
    if (at == order.end()) return dir > 0 ? ring.front() : ring.back();
    const auto n = static_cast<std::ptrdiff_t>(order.size());
    const auto start = at - order.begin();
    for (std::ptrdiff_t k = 1; k <= n; ++k) {
        const WidgetId id = order[static_cast<std::size_t>(((start + dir * k) % n + n) % n)];
        if (std::ranges::contains(ring, id)) return id;
    }
    return ring.front();
}

// From a widget focused by a click but not by Tab (a board), Tab goes on from its place in the
// tree: the focus order lists content built later (a board's) after the rest.
void WidgetTree::step_focus(int dir) {
    auto ring = focus_ring();
    if (ring.empty()) return;
    auto it = std::ranges::find(ring, focused_id_);
    if (it == ring.end()) {
        std::vector<WidgetId> in_tree;
        widget_detail::for_each_node(root_, [&](const WidgetNode& n) { in_tree.push_back(n.id); });
        return set_focused(next_in_ring(in_tree, ring, focused_id_, dir));
    }
    auto i = static_cast<std::ptrdiff_t>(it - ring.begin()) + dir;
    auto n = static_cast<std::ptrdiff_t>(ring.size());
    set_focused(ring[static_cast<std::size_t>((i % n + n) % n)]);
}

bool WidgetTree::start_drag_at(Point p) {
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

[[nodiscard]] bool WidgetTree::takes(WidgetId id, const std::any& value) const {
    auto it = index_.find(id);
    if (it == index_.end() || !is_enabled(*it->second)) return false;
    const auto& n = *it->second;
    if (n.drag.accepts) return n.drag.accepts(value);
    return value.type() == typeid(std::string) && edits_text(n);
}

[[nodiscard]] bool WidgetTree::takes_at(WidgetId id, const std::any& value, Point p) const {
    auto it = index_.find(id);
    if (it == index_.end() || !it->second->drag.takes_at) return true;
    auto r = drop_area_rect(id);
    if (!r) return true;
    return it->second->drag.takes_at(value, DropPlace{Point{X{p.x.raw() - r->origin.x.raw()}, Y{p.y.raw() - r->origin.y.raw()}}, r->extent});
}

[[nodiscard]] WidgetId WidgetTree::claiming_drop_area(Point p, const std::any& value) const {
    for (auto& [id, r, visible] : drop_areas_) {
        auto it = index_.find(id);
        if (it == index_.end() || !it->second->drag.claims || !visible.contains(p) || !takes(id, value)) continue;
        Point at{X{p.x.raw() - r.origin.x.raw()}, Y{p.y.raw() - r.origin.y.raw()}};
        if (it->second->drag.claims(DropPlace{at, r.extent})) return id;
    }
    return 0;
}

void WidgetTree::drop_outside() {
    if (!dragging()) return drag_.reset();
    auto d = std::move(*drag_);
    drag_.reset();
    tooltip_dirty_ = true;
    if (auto it = index_.find(d.source); it != index_.end() && it->second->drag.outside) it->second->drag.outside(d.value);
}

void WidgetTree::end_drag(std::optional<DropPlace> place) {
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
    if (it->second->drag.slot) place->slot = it->second->drag.slot(*it->second, place->at);
    if (it->second->drag.drop) it->second->drag.drop(d.value, *place);
    else if (d.value.type() == typeid(std::string)) {   // a text widget: typed in, as a drop from the OS
        set_focused(d.target);
        dispatch(d.target, Paste{std::any_cast<const std::string&>(d.value)});
    }
}

[[nodiscard]] bool WidgetTree::within(WidgetId id, WidgetId ancestor) const {
    for (WidgetId cur = id; cur != 0;) {
        if (cur == ancestor) return true;
        auto it = parent_map_.find(cur);
        cur = it == parent_map_.end() ? 0 : it->second;
    }
    return false;
}

[[nodiscard]] const WidgetNode* WidgetTree::open_content_popup() const {
    auto it = index_.find(popup_owner());
    if (popup_owner() == 0 || it == index_.end()) return nullptr;
    return it->second->popup_builder && !it->second->children.empty() ? it->second : nullptr;
}

[[nodiscard]] std::unique_ptr<SceneSnapshot> WidgetTree::build_snapshot(float w, float h, uint64_t version) {
    last_built_version_ = version;
    auto build_start = std::chrono::steady_clock::now();
    auto window_colors = WindowColors{theme_.canvas_bg, theme_.chrome_bg, theme_.chrome_border,
                                      theme_.chrome_text, theme_.chrome_icon, theme_.chrome_close};
    // Counted before refresh_dirty() runs record() -- record() never touches the
    // dirty flag itself (only clear_dirty(), called by the caller after publish,
    // does), so this reflects exactly what changed since the last publish.
    assert(widget_detail::marks_consistent(root_) && "a widget node was put in the tree without adopt()");
    std::size_t dirty_widgets = count_dirty(root_);
    refresh_dirty(root_);
    materialize_all_virtual_lists(root_);

    std::optional<PopupLayout> popup;   // the open popup's content, laid out by do_layout
    bool viewport_moved = false;   // a list's viewport, which its rows were materialized for
    std::size_t layout_kept = 0;
    // The first layout of a publish keeps what it can of the last publish's; a second one
    // (a viewport or a wrapped label changed) builds everything again.
    bool keep_layout = last_layout_.has_value();
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
        layout_kept = 0;   // a later pass builds everything: only the last pass's count holds
        LayoutCacheCursor cursor{keep_layout ? &*last_layout_ : nullptr, 0, &layout_kept};
        keep_layout = false;
        bool root_splits = splits(root_);
        for (auto& c : root_.children) {
            link_layout_cache(c, cursor);
            if (root_splits) c.layout_reusable = false;
        }
        layout.children.reserve(root_.children.size());
        for (auto& c : root_.children)
            build_layout(c, layout, /*keep=*/true);

        layout_measure(layout, LayoutAxis::Vertical);
        layout_arrange(layout, {Point{X{0}, Y{0}}, Size{Width{w}, Height{h}}});
        viewport_moved = update_scroll_state(layout);
        viewport_moved |= update_split_state(layout);   // panes resized: the layout is stale
        drop_areas_.clear();
        collect_drop_areas(layout, DY{0}, std::nullopt, drop_areas_);
        popup = layout_popup(root_, layout, Size{Width{w}, Height{h}}, theme_);
        popup_card_ = popup ? std::optional{popup->card} : std::nullopt;
        if (popup) viewport_moved |= popup->viewport_moved;
        return layout;
    };

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
    // A split's first layout uses its stored sizes, then fits them to its space; one nested in
    // another can only fit once its parent has. Each level takes a pass, hence the loop.
    constexpr int kMaxSettlePasses = 4;
    for (int pass = 0; viewport_moved && pass < kMaxSettlePasses; ++pass) {
        materialize_all_virtual_lists(root_);
        layout = do_layout();
        ++layout_passes;
    }

    // A widget whose height follows its width (a wrapped label) learns its height only
    // once placed: lay out again with it, so it shows right in this frame, not the next.
    if (update_canvas_bounds(layout, Size{Width{w}, Height{h}})) {
        layout = do_layout();
        ++layout_passes;
        update_canvas_bounds(layout, Size{Width{w}, Height{h}});
    }
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
    if (show_blocks_) draw_block_outlines(root_, *snap);
    if (popup_owner() != 0) tooltip_ = tooltip_dismissed(tooltip_);   // e.g. a dialog the app opened
    if (auto text = tooltip_of(tooltip_.target); tooltip_.shown && !text.empty())
        draw_tooltip(snap->overlay, text, tooltip_.pointer, snap->viewport, theme_);
    if (dragging()) draw_drag(*snap);

    snap->dirty_widget_count = dirty_widgets;
    snap->layout_pass_count = layout_passes;
    snap->layout_nodes_reused = layout_kept;
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
    last_layout_ = std::move(layout);
    return snap;
}

void WidgetTree::collect_drains(const Node& node) {
    if (node.drain_fn)
        drain_callbacks_.push_back(node.drain_fn);
    if (node.bind_waker_fn)
        waker_binders_.push_back(node.bind_waker_fn);
    for (const auto& child : node.children)
        collect_drains(child);
}

WidgetNode WidgetTree::build_widget_node(Node& node) {
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
    wn.layer_places = node.layer_places;
    wn.board_places = node.board_places;
    wn.board_under = node.board_under;
    wn.board_over = node.board_over;
    wn.keep_key = node.keep_key;
#ifdef PRISM_DEBUG_TOOLS_ENABLED
    wn.debug_name = node.debug_name;
#endif
    wn.block = node.block;
    build_widget_kind(node, wn);
    if (!node.enablers.empty()) make_disableable(wn, node.enablers);
    if (node.split_get) restore_split(node, wn);
    return wn;
}

void WidgetTree::restore_split(const Node& node, WidgetNode& wn) {
    wn.split_changed = node.split_set;
    auto sizes = node.split_get();
    auto panes = std::ranges::count_if(node.children, [](const Node& c) { return c.layout_kind != LayoutKind::Handle; });
    bool usable = std::ssize(sizes) == panes && std::ranges::all_of(sizes, [](float s) { return s > 0.f; });
    if (usable) wn.edit_state = SplitState{.engaged = true, .pane_sizes = std::move(sizes)};
}

void WidgetTree::build_widget_kind(Node& node, WidgetNode& wn) {
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
            vls->follow_tail = node.vlist_follow_tail;
            if (node.vlist_bind_row) vls->bind_row = node.vlist_bind_row;
            if (node.vlist_unbind_row) vls->unbind_row = node.vlist_unbind_row;
            wn.edit_state = vls;
            // Opt-in: vb.tree() and vb.list(...).selection() make the container itself take
            // the focus and the keys.
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

void WidgetTree::connect_on_change_dirty(Node& node, WidgetNode& wn) {
    auto id = wn.id;
    if (node.on_change) {
        wn.connections.push_back(
            node.on_change([this, id]() { set_dirty(id); })
        );
    }
}

void WidgetTree::connect_deps_dirty(Node& node, WidgetNode& wn) {
    auto id = wn.id;
    for (auto& dep : node.dependencies) {
        wn.connections.push_back(
            dep([this, id]() { set_dirty(id); })
        );
    }
}

void WidgetTree::draw_drag(SceneSnapshot& snap) const {
    const auto source = index_.find(drag_->source);
    if (source != index_.end() && source->second->drag.hint_targets) draw_drop_hints(snap);
    std::optional<Rect> target_rect;
    for (auto& [id, rect] : snap.geometry)
        if (id == drag_->target && id != 0) target_rect = rect;
    if (!target_rect) target_rect = drop_area_rect(drag_->target);
    auto target = index_.find(drag_->target);
    if (target_rect && target != index_.end()) {
        const auto& role = target->second->drag;
        const Point local{X{drag_->pointer.x.raw() - target_rect->origin.x.raw()},
                          Y{drag_->pointer.y.raw() - target_rect->origin.y.raw()}};
        if (!role.slot) {
            drag_draw::drop_marker(snap.overlay, *target_rect, drag_->pointer, role, theme_.accent);
        } else {
            const Rect m = role.slot_marker(*target->second, role.slot(*target->second, local));
            snap.overlay.filled_rect(Rect{Point{target_rect->origin.x + DX{m.origin.x.raw()},
                                                target_rect->origin.y + DY{m.origin.y.raw()}}, m.extent},
                                     theme_.accent);
        }
    }
    if (source != index_.end()) draw_dragged(snap, *source->second);
    if (target_rect && target != index_.end() && target->second->drag.zones)
        drag_draw::drop_guide(snap.overlay, *target_rect, drag_->pointer, target->second->drag, theme_.accent);
    else if (auto guide = guide_under_pointer())   // a group taking this only on its guide: show it
        drag_draw::drop_guide(snap.overlay, guide->second, drag_->pointer, index_.at(guide->first)->drag, theme_.accent,
                              /*buttons_only=*/true);
    if (auto* outer = edge_drop_container())
        if (auto rect = drop_area_rect(outer->id))
            drag_draw::edge_guide(snap.overlay, *rect, drag_->pointer, theme_.accent);
}

// What follows the pointer: the source's trail, or else the source (or the part dragged) faded.
void WidgetTree::draw_dragged(SceneSnapshot& snap, const WidgetNode& source) const {
    if (source.drag.trail) return source.drag.trail(snap.overlay, drag_->value, drag_->pointer);
    Point press{X{drag_->grab.dx.raw()}, Y{drag_->grab.dy.raw()}};
    auto area = source.drag.ghost_area ? source.drag.ghost_area(source, press) : std::nullopt;
    auto ghost = drag_draw::ghost(source.draws, area, drag_->pointer - drag_->grab);
    snap.overlay.commands.insert(snap.overlay.commands.end(), ghost.commands.begin(), ghost.commands.end());
}

// Every widget and drop area taking the dragged value, softly marked; not one taking it only
// at some places in it (DragRole::takes_at), which would be marked whole. Only their visible
// parts: the overlay is not clipped.
void WidgetTree::draw_drop_hints(SceneSnapshot& snap) const {
    const auto hinted = [&](WidgetId id) { return takes(id, drag_->value) && !index_.at(id)->drag.takes_at; };
    for (std::size_t i = 0; i < snap.geometry.size(); ++i)
        if (const WidgetId id = snap.geometry[i].first; id != 0 && hinted(id))
            if (const Rect seen = input::hit_area(snap, i); seen.extent.w.raw() > 0 && seen.extent.h.raw() > 0)
                drag_draw::hint(snap.overlay, seen, theme_.accent);
    for (auto& [id, rect, visible] : drop_areas_)
        if (hinted(id)) drag_draw::hint(snap.overlay, visible, theme_.accent);
}

// The innermost dock group under the pointer that takes the dragged value, but elsewhere in it
// (a floating card: on its guide only), so the guide shows where.
[[nodiscard]] std::optional<std::pair<WidgetId, Rect>> WidgetTree::guide_under_pointer() const {
    for (auto it = drop_areas_.rbegin(); it != drop_areas_.rend(); ++it) {
        auto n = index_.find(it->id);
        if (n != index_.end() && n->second->drag.zones && it->visible.contains(drag_->pointer) && takes(it->id, drag_->value)
            && !takes_at(it->id, drag_->value, drag_->pointer))
            return std::pair{it->id, it->rect};
    }
    return std::nullopt;
}

[[nodiscard]] const WidgetNode* WidgetTree::edge_drop_container() const {
    for (WidgetId id = drag_->target; id != 0;) {
        auto it = index_.find(id);
        if (it == index_.end()) return nullptr;
        if (it->second->drag.edge_zones) return it->second;
        auto up = parent_map_.find(id);
        id = up == parent_map_.end() ? 0 : up->second;
    }
    return nullptr;
}

void WidgetTree::connect_enablers(Node& node, WidgetNode& wn) {
    for (auto& e : node.enablers)
        wn.connections.push_back(e.subscribe([this, id = wn.id] {
            auto it = index_.find(id);
            if (it == index_.end()) return;
            // Its rows (a list's, a table's) draw with it: all of them redraw.
            widget_detail::for_each_node(*it->second, [](WidgetNode& n) { n.dirty = true; });
            if (!enabled(id)) release_disabled(*it->second);
        }));
}

void WidgetTree::release_disabled(WidgetNode& w) {
    widget_detail::for_each_node(w, [this](WidgetNode& n) {
        if (focused_id_ == n.id) clear_focus();
        if (captured_id_ == n.id) captured_id_ = 0;
        n.visual_state.hovered = n.visual_state.pressed = false;
        while (std::ranges::find(popup_stack_, n.id) != popup_stack_.end()) close_popup();
    });
}

bool WidgetTree::trigger_shortcut_in(WidgetNode& n, const KeyPress& kp) {
    if (!is_enabled(n)) return false;
    if (n.try_shortcut && n.try_shortcut(kp, n)) return true;
    return std::ranges::any_of(n.children, [&](WidgetNode& c) { return trigger_shortcut_in(c, kp); });
}

[[nodiscard]] bool WidgetTree::edits_text(const WidgetNode& n) {
    if (!n.access) return false;
    switch (n.access().role) {
    case Role::TextInput: case Role::MultilineTextInput: case Role::PasswordInput:
    case Role::ComboBox: case Role::SpinButton:
        return true;
    default:
        return false;
    }
}

void WidgetTree::connect_dirty(Node& node, WidgetNode& wn) {
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

void WidgetTree::build_index(WidgetNode& node) {
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

void WidgetTree::unindex_node(WidgetId id) {
    index_.erase(id);
    parent_map_.erase(id);
    std::erase(focus_order_, id);
}

void WidgetTree::park_kept(WidgetNode& node) {
    if (!node.keep_key.empty()) {
        unindex_ids(node);
        node.mark->up = nullptr;   // parked: its changes flag nothing until it is put back
        auto key = node.keep_key;
        parked_.insert_or_assign(std::move(key), std::move(node));
        return;
    }
    for (auto& c : node.children) park_kept(c);
}

void WidgetTree::unindex_subtree(WidgetNode& node) {
    unindex_node(node.id);
    node.connections.clear();
    for (auto& c : node.children)
        unindex_subtree(c);
}

void WidgetTree::materialize_popup(WidgetNode& node) {
    bool built = !node.children.empty();
    bool rebuild = std::exchange(node.popup_rebuild, false);   // consumed even when not built yet
    if (built && node.popup_content_open && rebuild) {
        close_popup_content(node);
        built = false;
    }
    if (node.popup_content_open && !built) open_popup_content(node);
    else if (!node.popup_content_open && built) close_popup_content(node);
}

void WidgetTree::open_popup_content(WidgetNode& node) {
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
    adopt(node, node.children.back());
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

void WidgetTree::close_popup_content(WidgetNode& node) {
    for (auto& c : node.children) unindex_subtree(c);
    node.children.clear();
    node.popup_area.reset();
    if (!index_.contains(focused_id_)) focused_id_ = 0;   // it was inside the popup
    set_focused(std::exchange(node.focus_before_popup, 0));
}

void WidgetTree::materialize_tabs(WidgetNode& node) {
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

        for (auto& c : content_wn.children) {
            adopt(content_wn, c);
            build_index(c);
        }
    }
    content_wn.dirty = true;
}

void WidgetTree::materialize_virtual_list(WidgetNode& node) {
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
        adopt(node, c);
        index_[c.id] = &c;
    }

    vls->visible_start = new_start;
    vls->visible_end = new_end;
}

void WidgetTree::recycle_table_rows(WidgetNode& node, TableState& ts) {
    for (auto it = node.children.rbegin(); it != node.children.rend(); ++it) {
        unindex_node(it->id);
        ts.pool.push_back(std::move(*it));
    }
    node.children.clear();
}

void WidgetTree::wire_table_input(WidgetNode& node, TableState* ts) {
    if (node.table_input_wired) return;
    node.table_input_wired = true;
    auto node_id = node.id;
    // A re-sort moves the rows, whether a header click or the app set it.
    node.connections.push_back(
        ts->sort.on_change().connect([ts, this](const auto&) { ts->shifted_at = next_frame_version(); }));
    node.keeps_keys = [ts](const KeyPress&) { return ts->editing.has_value(); };
    // Columns shown or moved, by a header drag or by the app.
    node.connections.push_back(columns_field(*ts).on_change().connect([this, node_id](const auto&) { set_dirty(node_id); }));
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

void WidgetTree::materialize_table(WidgetNode& node) {
    auto* ts = get_table_state(node);
    if (!ts || !ts->source.row_count) return;

    if (ts->row_height.raw() <= 0.f)
        ts->row_height = Height{24.f};

    refresh_order(*ts);
    auto shown = static_cast<float>(shown_columns(*ts).size());
    ts->auto_column_w = shown > 0 ? std::max(120.f, ts->viewport_w.raw() / shown) : 120.f;
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
    for (auto& c : node.children) adopt(node, c);

    ts->visible_start = new_start;
    ts->visible_end = new_end;

    render_table_header(node, *ts, edges, theme_);
    wire_table_input(node, ts);
}

void WidgetTree::materialize_content(WidgetNode& node) {
    if (node.content_built && !(node.content_stale && node.content_stale())) return;
    node.content_built = true;
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
    for (auto& c : node.children) {
        adopt(node, c);
        build_index(c);
    }
    node.dirty = true;
}

void WidgetTree::materialize_all_virtual_lists(WidgetNode& node) {
    if (!node.mark->builds && !builds_children(node)) return;   // nothing below builds children
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

} // namespace prism::app
