#pragma once

#include <prism/delegates/color_delegates.hpp>
#include <prism/delegates/date_delegates.hpp>
#include <prism/delegates/time_delegates.hpp>
#include <prism/delegates/dropdown_delegates.hpp>
#include <prism/delegates/menu_delegates.hpp>
#include <prism/delegates/dialog_delegates.hpp>
#include <prism/delegates/form_delegates.hpp>
#include <prism/delegates/tabs_delegates.hpp>
#include <prism/ui/layout.hpp>
#include <prism/delegates/text_delegates.hpp>
#include <prism/core/traits.hpp>
#include <prism/ui/widget_node.hpp>
#include <prism/app/widget_tree_traversal.hpp>
#include <prism/app/handle_drag.hpp>
#include <prism/app/table_input.hpp>
#include <prism/ui/tooltip.hpp>
#if __cpp_impl_reflection
#include <prism/core/reflect.hpp>
#include <prism/core/reflect_annotations.hpp>
#include <prism/app/widget_tree_reflect.hpp>
#endif
#include <prism/core/state.hpp>

#include <algorithm>
#include <memory>
#include <set>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace prism::app {

class AppContext;   // a block's attach() takes one; the tree only stores the call
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
        for (auto& c : root_.children) adopt(root_, c);
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

    // Runs each block's attach(ctx) once, the first time it is called after the block was
    // built: a block built later (a window opened, a page shown) is attached on the next call.
    void attach_blocks(AppContext& ctx) {
        auto due = std::exchange(unattached_, {});   // an attach() may build more blocks
        for (auto& [key, attach] : due) attach(ctx);
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
    void set_url_opener(const UrlOpener* o) { theme_.url_opener = o; }

    void set_text_measurer(const TextMeasurer* m);

    // Switches every widget to `t` (ui::light_theme(), dark_theme(), or the app's own) and
    // repaints them; the backend's measurer and clipboard stay.
    void set_theme(Theme t);

    void close_overlays() { close_overlays_impl(root_); }

    // Scrolls the nearest scroll view at or above `target`; at its end the wheel passes to the
    // next one up, but never past `stop` (a popup: the window behind it does not scroll).
    void scroll_at(WidgetId target, DY delta, WidgetId stop = 0);

    void scroll_to(WidgetId id, DY offset);

    void scroll_row_into_view(WidgetId container_id, size_t row_index, Height row_h);

    [[nodiscard]] std::vector<WidgetId> leaf_ids() const {
        std::vector<WidgetId> ids;
        collect_leaf_ids(root_, ids);
        return ids;
    }

    void dispatch(WidgetId id, const InputEvent& ev);

    // The widget whose popup is on top (0 = none): it gets the input. Popups stack: one opened
    // inside another (a menu in a dialog) goes on top, and closing it returns to the one below.
    [[nodiscard]] WidgetId popup_owner() const { return popup_stack_.empty() ? 0 : popup_stack_.back(); }
    [[nodiscard]] const WidgetNode* popup_node() const {
        auto it = index_.find(popup_owner());
        return it != index_.end() ? it->second : nullptr;
    }
    void close_popup();

    // The menu next to `from` in its menu bar (its sibling menus), dir = +1 or -1.
    [[nodiscard]] std::optional<WidgetId> menu_neighbour(WidgetId from, int dir) const;

    [[nodiscard]] bool opens_menu(WidgetId id) const {
        auto it = index_.find(id);
        return it != index_.end() && it->second->opens_menu;
    }

    // The innermost container under `p` (containers have no hit geometry of their own).
    [[nodiscard]] const WidgetNode& container_at(const WidgetNode& n, Point p) const;

    // The context menu for a right click at `p` on `hit`: declared in the nearest enclosing
    // container. With no widget hit, the walk starts at the container under the pointer.
    [[nodiscard]] std::optional<WidgetId> context_menu_for(std::optional<WidgetId> hit, Point p) const;

    // Re-records the popup owner if the app changed it since the last frame (e.g. closed a
    // dialog), so routing acts on the popup as it is now, not as last drawn.
    void refresh_popup_owner();

    // After an event: a popup that closed during it (a dialog's button, the app) lets go of the
    // input now, not at the next frame. The event itself still met the popup, as it was drawn.
    void release_closed_popup();

    // Moves the open popup (owner coordinates), e.g. to keep it inside the window.
    void move_popup(Rect area) {
        if (auto it = index_.find(popup_owner()); it != index_.end() && it->second->popup_area) {
            it->second->popup_area = area;
            it->second->dirty = true;
        }
    }

    // The menu whose title's mnemonic is `key` (Alt+key opens it), if any.
    // Two menus with the same letter: the first in layout order, as in Qt.
    [[nodiscard]] std::optional<WidgetId> menu_for_key(std::int32_t key) const;

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
    void drop_closed_popups();

    void adopt_popup_in(WidgetNode& node);

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
    void track_popup(const WidgetNode& n);

    void flip_visual_flag(WidgetId id, bool WidgetVisualState::* flag, bool value);

    void update_hover(std::optional<WidgetId> id);

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
    [[nodiscard]] std::optional<TooltipState::clock::time_point> next_wake() const;

    // Runs what is due at `now`: shows a tooltip, redraws animating widgets.
    void tick(TooltipState::clock::time_point now);

    // `button`: the one pressing it, which alone lets go of the capture (see captured_button).
    void set_pressed(WidgetId id, bool pressed, std::uint8_t button = input::buttons::left) {
        flip_visual_flag(id, &WidgetVisualState::pressed, pressed);
        captured_id_ = pressed && enabled(id) ? id : 0;
        captured_button_ = button;
    }

    [[nodiscard]] WidgetId captured_id() const { return captured_id_; }
    [[nodiscard]] std::uint8_t captured_button() const { return captured_button_; }

    [[nodiscard]] CursorShape desired_cursor() const;

    using ScrollbarDrag = widget_detail::ScrollbarDrag;
    using SplitDrag = widget_detail::SplitDrag;

    void begin_scrollbar_drag(WidgetId id, Y mouse_y);

    void update_scrollbar_drag(Y mouse_y);

    void end_scrollbar_drag() {
        scrollbar_drag_ = {};
    }

    [[nodiscard]] bool in_scrollbar_drag() const { return scrollbar_drag_.scroll_id != 0; }

    void begin_split_drag(WidgetId container_id, size_t handle_index, float pos);

    void update_split_drag(float pos);

    void end_split_drag();

    [[nodiscard]] bool in_split_drag() const { return split_drag_.container_id != 0; }

    [[nodiscard]] Connection connect_input(WidgetId id, std::function<void(const InputEvent&)> cb) {
        if (auto it = index_.find(id); it != index_.end())
            return it->second->on_input.connect(std::move(cb));
        return {};
    }

    [[nodiscard]] WidgetId hovered_id() const { return hovered_id_; }

    [[nodiscard]] WidgetId focused_id() const { return focused_id_; }

    [[nodiscard]] const std::vector<WidgetId>& focus_order() const { return focus_order_; }

    void set_focused(WidgetId id);

    void clear_focus() {
        flip_visual_flag(focused_id_, &WidgetVisualState::focused, false);
        focused_id_ = 0;
    }

    // What Tab cycles through: the open popup's content if a popup holds widgets (it keeps the
    // focus inside), else every focusable widget.
    [[nodiscard]] std::vector<WidgetId> focus_ring() const;

    [[nodiscard]] bool enabled(WidgetId id) const {
        auto it = index_.find(id);
        return it == index_.end() || is_enabled(*it->second);
    }

    void focus_next() { step_focus(+1); }
    void focus_prev() { step_focus(-1); }

    void step_focus(int dir);

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

    // `source_rect`: where the source is drawn, at `scale` (a zoomed board's card).
    void arm_drag(WidgetId id, Point at, Rect source_rect, float scale = 1.f) {
        auto it = index_.find(id);
        if (it == index_.end() || !it->second->drag.source || !is_enabled(*it->second)) return;
        const Offset d = at - source_rect.origin;
        drag_ = DragState{.source = id, .press = at, .grab = Offset{DX{d.dx.raw() / scale}, DY{d.dy.raw() / scale}},
                          .value = {}, .pointer = at, .target = 0};
    }
    [[nodiscard]] bool dragging() const { return drag_ && drag_->value.has_value(); }
    [[nodiscard]] const DragState* drag() const { return drag_ ? &*drag_ : nullptr; }

    // Starts the drag once the pointer has moved far enough; false while it has not, or if the
    // source has nothing to drag (the press then stays an ordinary one).
    bool start_drag_at(Point p);

    // Whether widget `id` takes the dragged value: a drop target of its type, or a text widget
    // taking a string as typing.
    [[nodiscard]] bool takes(WidgetId id, const std::any& value) const;

    // The innermost container taking drops whose area holds `p` and that takes `value`.
    [[nodiscard]] WidgetId drop_area_at(Point p, const std::any& value) const {
        for (auto it = drop_areas_.rbegin(); it != drop_areas_.rend(); ++it)
            if (it->visible.contains(p) && takes(it->id, value) && takes_at(it->id, value, p)) return it->id;
        return 0;
    }
    // Whether target `id` takes `value` at `p` (DragRole::takes_at), when it says where.
    [[nodiscard]] bool takes_at(WidgetId id, const std::any& value, Point p) const;
    // The container that claims `p` by a rule of its own -- a dock's edge buttons, which lie over
    // its groups -- before whatever lies under it takes the drop.
    [[nodiscard]] WidgetId claiming_drop_area(Point p, const std::any& value) const;
    [[nodiscard]] std::optional<Rect> drop_area_rect(WidgetId id) const {
        const DropArea* a = drop_area(id);
        return a ? std::optional{a->rect} : std::nullopt;
    }
    [[nodiscard]] const DropArea* drop_area(WidgetId id) const {
        for (auto& a : drop_areas_)
            if (a.id == id) return &a;
        return nullptr;
    }

    void move_drag(Point p, WidgetId target) {
        drag_->pointer = p;
        drag_->target = target;
        tooltip_dirty_ = true;   // the ghost and the outline are redrawn
    }

    // The drag was let go outside the window: its source decides (a dock tab floats its panel).
    void drop_outside();

    // Ends the drag: drops on the target (at `place`), or nothing.
    void end_drag(std::optional<DropPlace> place);

    // Chooses the menu item whose shortcut `kp` is, unless the focused text widget keeps the
    // key (Ctrl+C copies there). Disabled menus and items never match.
    // simplify: every menu in the tree matches, also one in a tab that is not shown; scope
    // shortcuts to visible widgets when two tabs give the same shortcut different meanings.
    // Whether the focused widget takes this key before routing does anything with it.
    [[nodiscard]] bool focused_keeps(const KeyPress& kp) const {
        auto it = index_.find(focused_id_);
        return it != index_.end() && it->second->keeps_keys && it->second->keeps_keys(kp);
    }

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

    // Where the last layout drew widget `id`, in window coordinates (a zoomed card's widgets at
    // their zoomed size); none for an unknown id.
    [[nodiscard]] std::optional<Rect> laid_out_at(WidgetId id) const {
        auto it = index_.find(id);
        if (it == index_.end()) return std::nullopt;
        return laid_out_rect(*it->second);
    }
    // The size the last layout gave widget `id`, at zoom 1 (a zoomed card's widgets are drawn at
    // a multiple of it); none for an unknown id.
    [[nodiscard]] std::optional<Size> arranged_extent(WidgetId id) const {
        auto it = index_.find(id);
        return it == index_.end() ? std::nullopt : std::optional{it->second->arranged_extent};
    }

    // Whether `id` is `ancestor` or below it.
    [[nodiscard]] bool within(WidgetId id, WidgetId ancestor) const;

    // That popup's card, in window coordinates, as last laid out. Routing reads it directly:
    // the anchor may be scrolled out of view, and then has no geometry to offset from.
    [[nodiscard]] std::optional<Rect> popup_card() const { return popup_card_; }

    // The open popup whose content is widgets (ui::Popup), if any.
    [[nodiscard]] const WidgetNode* open_content_popup() const;

    void mark_dirty_by_id(WidgetId id) { set_dirty(id); }

    // Outlines each block placed with vb.block(), labelled with its name: what a block is, seen on
    // the screen. Starts on when PRISM_SHOW_BLOCKS is set (the README captures use it).
    void show_blocks(bool on) { show_blocks_ = on; }   // the overlay is redrawn every publish

    void set_debug_highlight(std::optional<WidgetId> id) {
        highlight_id_ = id;
        mark_dirty_by_id(root_.id);
    }

    [[nodiscard]] std::unique_ptr<SceneSnapshot> build_snapshot(float w, float h, uint64_t version);

private:
    WidgetNode root_;
    Theme theme_;
#if __cpp_impl_reflection
    // Address -> reflected member identifier, accumulated across every build_node_tree() call
    // (the top-level model and every nested block()) -- lives as long as the tree itself, so
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
    std::uint8_t captured_button_ = input::buttons::left;
    std::optional<DragState> drag_;
    std::vector<DropArea> drop_areas_;   // containers that take drops, outermost first
    std::unordered_map<std::string, WidgetNode> parked_;   // kept subtrees, by keep_key (park_kept)
    WindowOpener window_opener_;
    std::function<void(WindowId)> window_closer_;
    std::optional<WidgetId> highlight_id_;
    bool show_blocks_ = std::getenv("PRISM_SHOW_BLOCKS") != nullptr;
    ScrollbarDrag scrollbar_drag_;
    SplitDrag split_drag_;
    std::vector<WidgetId> focus_order_;
    std::unordered_map<WidgetId, WidgetNode*> index_;
    TooltipState tooltip_;
    bool tooltip_dirty_ = false;
    std::optional<Rect> popup_card_;   // see popup_card()
    std::optional<LayoutNode> last_layout_;   // the last publish's layout, kept for the next one
    bool swallow_text_ = false;
    std::uint64_t last_built_version_ = 0;
    std::uint64_t input_frame_ = UINT64_MAX;   // none known: as new as it gets   // shown or hidden since the last publish

    void set_tooltip(TooltipState s) {
        if (s.shown != tooltip_.shown) tooltip_dirty_ = true;
        tooltip_ = s;
    }
    std::unordered_map<WidgetId, WidgetId> parent_map_;
    std::vector<std::function<void()>> drain_callbacks_;
    std::unordered_set<const void*> attached_keys_;
    // simplify: only grows; an address reused after a model dies could hide one debug warning.
    std::set<const void*> placed_anywhere_;
    std::vector<std::pair<const void*, std::function<void(AppContext&)>>> unattached_;
    std::vector<std::function<void(std::function<void()>)>> waker_binders_;

    void collect_drains(const Node& node);

    // --- Node → WidgetNode conversion ---

    WidgetNode build_widget_node(Node& node);

    // Starts the panes at their remembered sizes; sizes for another number of panes (an older
    // layout) are ignored.
    static void restore_split(const Node& node, WidgetNode& wn);

    void build_widget_kind(Node& node, WidgetNode& wn);

    void connect_on_change_dirty(Node& node, WidgetNode& wn);

    void connect_deps_dirty(Node& node, WidgetNode& wn);

    // While dragging: soft markers on every target taking it (DragRole::hint_targets), a marker
    // on the target under the pointer, the dragged widget (or the part dragged) following the
    // pointer where it was grabbed, or its trail, then over it a dock's guides (app/drag_draw.hpp).
    void draw_drag(SceneSnapshot& snap) const;
    void draw_drop_hints(SceneSnapshot& snap) const;
    void draw_dragged(SceneSnapshot& snap, const WidgetNode& source) const;

    // The container around the drop target whose edges take drops (a dock): it shows its edge
    // buttons while anything is dragged over it.
    [[nodiscard]] std::optional<std::pair<WidgetId, Rect>> guide_under_pointer() const;
    [[nodiscard]] const WidgetNode* edge_drop_container() const;

    // A widget whose condition changed redraws (faded or not); one just disabled lets go of the
    // focus, hover, press and popup it held.
    void connect_enablers(Node& node, WidgetNode& wn);

    // Lets go of the focus, hover, press and popups held by `w` or anything below it.
    void release_disabled(WidgetNode& w);

    bool trigger_shortcut_in(WidgetNode& n, const KeyPress& kp);

    [[nodiscard]] bool focused_edits_text() const {
        auto it = index_.find(focused_id_);
        return it != index_.end() && edits_text(*it->second);
    }

    [[nodiscard]] static bool edits_text(const WidgetNode& n);

    void connect_dirty(Node& node, WidgetNode& wn);

    // --- Node tree construction ---

    template <typename Model>
    Node build_node_tree(Model& model) {
        Node root;
        root.id = next_id_++;
        root.is_leaf = false;
#ifdef PRISM_DEBUG_TOOLS_ENABLED
#if __cpp_impl_reflection
        // Every block() nesting call (PRISM's primary composition mechanism) wraps its
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
                // Blocks were built inside this view(): what they placed counts for this model
                // too (a value it owns and hands to a block to draw).
                placed_anywhere_.insert(built.placed().begin(), built.placed().end());
                check_unplaced_fields(model, placed_anywhere_);
                drain_unplaced_pipes(model);
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

                if constexpr (core::has_annotation<m, decltype(core::skip)>()) {
                    // [[=core::skip]]: no widget. A Shared or a Channel is still delivered.
                    if constexpr (is_shared_v<M> || is_channel_v<M>) {
                        drain_callbacks_.push_back([&member] { member.drain_notifications(); });
                        waker_binders_.push_back(
                            [&member](std::function<void()> w) { member.set_waker(std::move(w)); });
                    }
                } else if constexpr (is_state_v<M>) {
                    // invisible observable — no widget
                } else if constexpr (is_channel_v<M>) {
                    // invisible observable — no widget, but still drained every tick since
                    // (unlike State<T>) it delivers cross-thread, not synchronously on send()
                    drain_callbacks_.push_back([&member] { member.drain_notifications(); });
                    waker_binders_.push_back(
                        [&member](std::function<void()> w) { member.set_waker(std::move(w)); });
                } else if constexpr (is_field_v<M>) {
                    auto leaf = reflected_leaf<m>(member);
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
                } else if constexpr (is_block_v<M>) {
                    root.children.push_back(build_node_tree(member));
                }
            }
        }
#endif // __cpp_impl_reflection

        if constexpr (requires(Model& m, AppContext& ctx) { m.attach(ctx); })
            note_attach(&model, [&model](AppContext& ctx) { model.attach(ctx); });
        return root;
    }

#if __cpp_impl_reflection
    // The Shared and Channel members view() did not show: a worker feeds them and the model
    // observes them. Drained and woken like the shown ones, so the model needs no drain().
    template <typename Model>
    void drain_unplaced_pipes(Model& model) {
        static constexpr auto members = std::define_static_array(
            std::meta::nonstatic_data_members_of(^^Model, std::meta::access_context::unchecked()));
        template for (constexpr auto m : members) {
            if constexpr (!std::meta::is_reference_type(std::meta::type_of(m))) {
                auto& member = model.[:m:];
                using M = std::remove_cvref_t<decltype(member)>;
                if constexpr (is_shared_v<M> || is_channel_v<M>) {
                    if (placed_anywhere_.contains(&member)) continue;
                    drain_callbacks_.push_back([&member] { member.drain_notifications(); });
                    waker_binders_.push_back(
                        [&member](std::function<void()> w) { member.set_waker(std::move(w)); });
                }
            }
        }
    }

    // A model's Field member as a widget: its value type's, or a slider for a number with a
    // [[=core::range<min, max, step>]] annotation.
    template <std::meta::info Member, typename T>
    Node reflected_leaf(Field<T>& field) {
        if constexpr (constexpr auto r = core::range_annotation<Member>(); r != std::meta::info{}) {
            static_assert(Numeric<T>, "[[=prism::core::range<...>]] is only supported on numbers");
            using R = [:r:];
            return node_slider_leaf(field, Slider<T>{.min = static_cast<T>(R::min), .max = static_cast<T>(R::max),
                                                     .step = static_cast<T>(R::step),
                                                     .label = std::string(core::caption_of<Member>()),
                                                     .unit = std::string(core::extract_string_annotation<Member, core::unit_t>())},
                                    next_id_);
        } else {
            return node_leaf(field, next_id_);
        }
    }
#endif

    // Views are rebuilt, so a block is met many times: remember each one once, by address.
    void note_attach(const void* key, std::function<void(AppContext&)> attach) {
        if (attached_keys_.insert(key).second) unattached_.emplace_back(key, std::move(attach));
    }

    // --- WidgetNode tree utilities ---

    void build_index(WidgetNode& node);

    void propagate_theme(WidgetNode& node) {
        node.theme = &theme_;
        for (auto& child : node.children)
            propagate_theme(child);
    }

    void set_dirty(WidgetId id) {
        if (auto it = index_.find(id); it != index_.end())
            it->second->dirty = true;
    }

    void unindex_node(WidgetId id);

    // Takes the subtrees to keep (WidgetNode::keep_key) out of `node`, which is about to be
    // dropped: out of the index, but still wired and subscribed, ready to be put back.
    void park_kept(WidgetNode& node);
    void unindex_ids(WidgetNode& node) {
        unindex_node(node.id);
        for (auto& c : node.children) unindex_ids(c);
    }

    void unindex_subtree(WidgetNode& node);

    // --- popups that hold widgets (doc/design/popup-content.md) ---

    // Builds the content when the popup opens (focusing its first field, remembering what had
    // the focus) and drops it when it closes (giving the focus back).
    void materialize_popup(WidgetNode& node);

    void open_popup_content(WidgetNode& node);

    void close_popup_content(WidgetNode& node);

    void materialize_tabs(WidgetNode& node);

    void materialize_virtual_list(WidgetNode& node);

    // Moves every materialized row back into the row pool for reuse.
    void recycle_table_rows(WidgetNode& node, TableState& ts);

    // A press on the body hit-tested against a frame from before the rows moved: it would act
    // on whichever row has since taken that place.
    bool stale_body_press(const TableState& ts, const InputEvent& ev) const {
        auto* mb = std::get_if<MouseButton>(&ev);
        return mb && mb->pressed && !table_input_detail::in_header(ts, mb->position) && input_frame() < ts.shifted_at;
    }

    // Wires the input handler for selection, sorting, resizing and scrolling (once per node).
    void wire_table_input(WidgetNode& node, TableState* ts);

    void materialize_table(WidgetNode& node);

    // A container built while the app runs (a dock): its content again from its builder, on the
    // first frame and whenever it is stale.
    void materialize_content(WidgetNode& node);

    void materialize_all_virtual_lists(WidgetNode& node);

};

} // namespace prism::app

#include <prism/app/view_builder.hpp>
