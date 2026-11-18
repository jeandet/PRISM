#pragma once

#include <prism/ui/frame.hpp>
#include <prism/core/connection.hpp>
#include <prism/ui/context.hpp>
#include <prism/ui/delegate.hpp>
#include <prism/render/draw_list.hpp>
#include <prism/core/field.hpp>
#include <prism/core/derived.hpp>
#include <prism/core/shared.hpp>
#include <prism/input/input_event.hpp>
#include <prism/ui/node.hpp>
#include <prism/core/reflect.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <chrono>
#include <vector>

namespace prism::ui {
using namespace prism::core;
using namespace prism::render;
using namespace prism::input;

struct LayoutNode;

// "Something below this node changed since the last publish": lets the walks over the tree
// (redraw, clear, layout reuse) skip a subtree with nothing changed instead of visiting it.
// On the heap so it stays put when its node moves (into a list's pool, a parent's vector).
struct SubtreeMark {
    bool below = false;          // a descendant is dirty, or must be measured again
    SubtreeMark* up = nullptr;   // the parent's mark; null at the root and while detached
    // This node or one below builds children at publish time (builds_children). Set when it
    // joins the tree, never cleared: a subtree removed since only costs a needless visit.
    bool builds = false;
};

// Flags `m` and its ancestors, up to the first already flagged: it and everything above it
// already are, since a mark is cleared only once nothing below it is flagged.
inline void flag_up(SubtreeMark* m) {
    for (; m && !m->below; m = m->up) m->below = true;
}

// A widget's "draw me again" flag, read and written as a bool; setting it also flags the
// ancestors (flag_up), so every node.dirty = true in the delegates reaches them.
class DirtyFlag {
public:
    explicit DirtyFlag(SubtreeMark* own) : own_(own) {}
    DirtyFlag& operator=(bool v) {
        self_ = v;
        if (v && own_) flag_up(own_->up);
        return *this;
    }
    operator bool() const { return self_; }

private:
    bool self_ = false;
    SubtreeMark* own_;   // this node's mark, which moves with it
};

struct WidgetNode {
    WidgetId id = 0;
    // Read for every node on every publish: kept together, at the front. A WidgetNode spans
    // dozens of cache lines; spread out, these cost a miss each (bench_widget_scale).
    std::unique_ptr<SubtreeMark> mark = std::make_unique<SubtreeMark>();
    DirtyFlag dirty{mark.get()};   // after `mark`, which it points to
    bool is_container = false;
    LayoutKind layout_kind = LayoutKind::Default;
    // Layout kept between publishes (app/widget_tree_layout.hpp, link_layout_cache): this
    // node's layout node from the last publish, and whether it still holds as it is. `relayout`
    // marks a widget re-recorded after layout (its size changed), which must be measured again.
    bool layout_reusable = false;
    bool relayout = false;
    LayoutNode* cached_layout = nullptr;
    std::size_t kept_nodes = 0;   // layout nodes this subtree kept the last time it was walked
    std::string name;                     // accessible name: the field's member name, if known
    std::string tooltip;                  // empty: none (see ui/tooltip.hpp)
    std::optional<Framing> frame;           // a Row/Column's look around its content (ui/frame.hpp)
    bool focus_on_open = false;           // focused when the popup holding it opens
    // Null: always enabled. Otherwise read live: false greys the widget out and the tree gives
    // it no input, focus, hover or press (see make_disableable).
    std::function<bool()> enabled;
    std::function<void(std::vector<float>)> split_changed;   // a drag ended (vb.remember_split)
    DragRole drag;   // a drag source and/or drop target (doc/design/drag-and-docking.md)
    std::function<DragRole(std::size_t)> row_drag;   // a virtual list: each bound row's role
    std::function<void(Node&)> build_content;   // see Node::build_content
    std::function<bool()> content_stale;
    std::function<std::vector<Rect>()> layer_places;   // see Node::layer_places
    std::function<std::vector<Point>()> board_places;   // see Node::board_places
    BoardDraw board_under;
    BoardDraw board_over;
    std::function<float()> board_scale;   // see Node::board_scale
    std::function<std::optional<WidgetId>(const InputEvent&)> claims;   // see Node::claims
    bool content_built = false;   // build_content has run (its content may well be empty)
    std::string keep_key;   // see Node::keep_key
    bool reused = false;    // just put back from the parked subtrees: already wired and subscribed
    std::function<AccessInfo()> access;   // leaves only: role, value and state for assistive tech
    std::function<float(const WidgetNode&)> intrinsic_width;   // leaves that size to their content
    std::optional<float> intrinsic_width_cache;   // its last answer, until the widget is dirty again
    // A widget opens a popup (menu, popover, dialog) by setting this, in its own coordinates;
    // while set, the popup gets all input first (see doc/design/menus-and-dialogs.md).
    std::optional<Rect> popup_area;
    // What the popup opened from (a menu's title, a right click's point), same coordinates:
    // near a window edge the popup flips to its other side. None: just kept inside the window.
    std::optional<Rect> popup_opened_from;
    // More of the same popup outside popup_area (a menu's open submenus), same coordinates:
    // input there is the popup's too.
    std::vector<Rect> popup_more;
    // A leaf that scrolls its own content (TextArea): drawn with a scrollbar like a scroll
    // container's. viewport is in the widget's coordinates.
    struct InnerScroll { Rect viewport; Height content_h; DY offset; };
    std::optional<InnerScroll> inner_scroll;
    // A popup holding widgets (ui::Popup): builds its content when it opens (the tree calls it
    // with a column node to fill); `popup_content_open` is what the widget asks for.
    std::function<void(Node&)> popup_builder;
    bool popup_content_open = false;
    bool popup_rebuild = false;   // the widget asks for its open content anew (what it shows changed)
    // An animating widget's next frame (set by its record(); see WidgetTree::next_wake).
    std::optional<std::chrono::steady_clock::time_point> redraw_at;
    WidgetId focus_before_popup = 0;   // given back when the popup closes
    bool popup_modal = false;   // outside presses are swallowed instead of closing the popup
    bool opens_menu = false;    // a menu title: sibling menus form a bar (arrows, hover switch)
    std::function<std::int32_t()> mnemonic_key;   // a menu title's Alt+key (0: none); read live
    // A menu's keyboard shortcuts: chooses the item `kp` triggers, if any (see ui/shortcut.hpp).
    std::function<bool(const KeyPress&, WidgetNode& self)> try_shortcut;
    // While it says so, the focused widget gets every key first -- Tab and shortcuts included
    // (a table cell being edited owns the keyboard).
    std::function<bool(const KeyPress&)> keeps_keys;
    bool context_menu = false;  // opened by a right click in its parent container
    bool popup_anchor = false;  // draws nothing in place, only popups (context menu, dialog)
#ifdef PRISM_DEBUG_TOOLS_ENABLED
    std::string debug_name;
#endif
    std::optional<std::string> block;   // see Node::block
    FocusPolicy focus_policy = FocusPolicy::none;
    WidgetVisualState visual_state;
    EditState edit_state;

    template <typename S>
    S& get_or_create() {
        auto* p = std::any_cast<S>(&edit_state);
        if (!p) {
            edit_state = S{};
            p = std::any_cast<S>(&edit_state);
        }
        return *p;
    }

    // Read-only counterpart of get_or_create: the stored state if present,
    // a shared default-constructed one otherwise.
    template <typename S>
    const S& peek() const {
        static const S default_state;
        auto* p = std::any_cast<S>(&edit_state);
        return p ? *p : default_state;
    }

    DrawList draws;
    DrawList overlay_draws;
    std::vector<WidgetNode> children;
    SenderHub<const InputEvent&> on_input;
    // connections must be declared after on_input so they are destroyed first,
    // disconnecting from on_input before it is destroyed
    std::vector<Connection> connections;
    std::function<void(WidgetNode&)> wire;
    std::function<void(WidgetNode&)> record;
    float spacer_size = 0.f;   // see Node::spacer_size
    Rect canvas_bounds{Point{X{0}, Y{0}}, Size{Width{0}, Height{0}}};
    Size arranged_extent{Width{0}, Height{0}};  // last-arranged main-axis extent, populated for every node kind

    // Snapshot-assembly cache (see layout_flatten): the absolute screen rect and the
    // already-translated+clipped DrawList last computed for this widget. Reused when the
    // widget is still not dirty AND still at the same allocated rect, to skip re-copying
    // and re-translating its draw commands -- which can be large (polylines, wide tables)
    // -- on frames where neither its content nor its screen position changed.
    Rect cached_screen_rect{Point{X{0}, Y{0}}, Size{Width{0}, Height{0}}};
    Rect cached_hit_rect{Point{X{0}, Y{0}}, Size{Width{0}, Height{0}}};
    std::shared_ptr<const DrawList> cached_snapshot_draws;

    bool expand = false;
    ExpandAxis expand_axis = ExpandAxis::None;
    std::optional<float> canvas_min_width;   // only meaningful when layout_kind == Canvas
    std::optional<float> canvas_min_height;  // only meaningful when layout_kind == Canvas
    const Theme* theme = nullptr;
    bool table_input_wired = false;
    std::shared_ptr<std::vector<std::string>> tab_names;
    Height viewport_height{0};   // the window's size, set after each layout
    Width viewport_width{0};
    // Where the last layout drew it in the window, and at what zoom (a zoomed board's cards: its
    // own coordinates are scaled by view_scale there; arranged_extent stays at zoom 1).
    X absolute_x{0};
    Y absolute_y{0};
    float view_scale = 1.f;
};

// Where the last layout drew `n` in the window, zoom included.
inline Rect laid_out_rect(const WidgetNode& n) {
    return {Point{n.absolute_x, n.absolute_y}, n.arranged_extent * n.view_scale};
}

// Where the last layout drew `n`, and at what zoom: its own coordinates in the window.
inline input::Placed place_of(const WidgetNode& n) { return {laid_out_rect(n), n.view_scale}; }

// A node whose children are made at publish time (WidgetTree::materialize_all_virtual_lists).
inline bool builds_children(const WidgetNode& n) {
    return n.layout_kind == LayoutKind::VirtualList || n.layout_kind == LayoutKind::Table
        || n.layout_kind == LayoutKind::Tabs || n.build_content || n.popup_builder;
}

// Puts `child` under `parent`, and the nodes below it under theirs: their changes now flag the
// parent's subtree, and whatever they bring flags it now. Every node put in a tree passes here.
inline void adopt(WidgetNode& parent, WidgetNode& child) {
    child.mark->up = parent.mark.get();
    if (child.dirty || child.relayout) flag_up(parent.mark.get());
    if (child.mark->builds || builds_children(child)) {
        child.mark->builds = true;
        for (auto* m = child.mark->up; m && !m->builds; m = m->up) m->builds = true;
    }
    for (auto& c : child.children) adopt(child, c);
}

// A widget re-recorded after layout: its layout node must be built and measured again.
inline void mark_relayout(WidgetNode& node) {
    node.relayout = true;
    flag_up(node.mark->up);
}

struct VirtualListState {
    ItemCount item_count{0};
    Height item_height{0};
    DY scroll_offset{0};
    Height viewport_h{0};
    ItemIndex visible_start{0};
    ItemIndex visible_end{0};
    ItemCount overscan{2};
    ScrollBarPolicy scrollbar{ScrollBarPolicy::Auto};
    ScrollEventPolicy event_policy{ScrollEventPolicy::BubbleAtBounds};
    uint8_t show_ticks = 0;

    // A log or console: rows appended while the list shows its bottom scroll it to the new bottom.
    bool follow_tail = false;
    DY tail_offset{0};   // the bottom as of the last layout; at it, the list is following

    std::vector<WidgetNode> pool;
    std::function<void(WidgetNode&, size_t index)> bind_row;
    std::function<void(WidgetNode&)> unbind_row;
};

struct TabsState {
    std::shared_ptr<std::vector<std::string>> tab_names;
    std::vector<std::function<void(Node&)>> tab_node_builders;
    std::function<size_t()> get_selected;
    size_t active_tab = std::numeric_limits<size_t>::max();
};

inline std::pair<ItemIndex, ItemIndex> compute_visible_range(
    ItemCount item_count, Height item_height, DY scroll_offset,
    Height viewport_h, ItemCount overscan)
{
    if (item_count.raw() == 0 || item_height.raw() <= 0.f)
        return {ItemIndex{0}, ItemIndex{0}};

    float h = item_height.raw();
    size_t first_visible = static_cast<size_t>(std::max(0.f, scroll_offset.raw() / h));
    size_t last_visible = static_cast<size_t>(
        std::ceil((scroll_offset.raw() + viewport_h.raw()) / h));

    size_t start = (first_visible >= overscan.raw())
        ? first_visible - overscan.raw() : 0;
    size_t end = std::min(last_visible + overscan.raw(), item_count.raw());

    return {ItemIndex{start}, ItemIndex{end}};
}

inline const WidgetVisualState& node_vs(const WidgetNode& n) { return n.visual_state; }
inline void set_node_cursor(WidgetNode& n, CursorShape c) { n.visual_state.cursor = c; }

inline bool is_enabled(const WidgetNode& n) { return !n.enabled || n.enabled(); }

inline constexpr float disabled_alpha = 0.4f;

// Makes `wn` enabled only while `on` holds; while it does not, `wn` records as usual, then fades
// what it drew.
inline void make_disableable(WidgetNode& wn, std::function<bool()> on) {
    wn.enabled = std::move(on);
    if (wn.record)
        wn.record = [inner = std::move(wn.record), on = wn.enabled](WidgetNode& n) {
            inner(n);
            if (!on()) fade(n.draws, disabled_alpha);
        };
    if (!wn.enabled()) fade(wn.draws, disabled_alpha);   // it usually recorded once already
}

inline void make_disableable(WidgetNode& wn, const std::vector<Enabler>& enablers) {
    std::vector<std::function<bool()>> gets;
    for (const auto& e : enablers) gets.push_back(e.get);
    make_disableable(wn, [gets = std::move(gets)] { return std::ranges::all_of(gets, [](const auto& g) { return g(); }); });
}
inline Size node_allocated(const WidgetNode& n) { return n.canvas_bounds.extent; }
inline const Theme& node_theme(const WidgetNode& n) { return *n.theme; }

namespace widget_node_detail {

// Fallback paint for a leaf whose T has no Widget<T> delegate: a plain hover-tinted box.
inline void record_fallback_box(WidgetNode& node) {
    node.draws.clear();
    node.overlay_draws.clear();
    const auto& vs = node_vs(node);
    auto bg = vs.hovered ? Color::rgba(70, 70, 80)
                         : Color::rgba(50, 50, 60);
    auto sz = node_allocated(node);
    node.draws.filled_rect(Rect{Point{X{0}, Y{0}}, sz}, bg);
}

template <typename T>
void set_debug_name([[maybe_unused]] Node& n) {
#ifdef PRISM_DEBUG_TOOLS_ENABLED
#if __cpp_impl_reflection
    // has_identifier()/identifier_of() must be bound together into a single
    // static constexpr constant expression (mirroring check_unplaced_fields'
    // model_name idiom below) -- as a runtime ternary, each consteval call is
    // an independent immediate invocation evaluated regardless of the branch
    // taken, so identifier_of(^^T) would throw for a T without an identifier
    // (e.g. int) even though has_identifier(^^T) guards it at "runtime".
    static constexpr std::string_view type_name = std::meta::has_identifier(^^T)
        ? std::meta::identifier_of(^^T) : std::string_view{};
    n.debug_name = std::string(type_name);
#endif
#endif
}

} // namespace widget_node_detail

// --- Accessibility (see ui/access.hpp) ---

template <typename T>
constexpr Role access_role_of() {
    if constexpr (requires { Widget<T>::access_role; }) return Widget<T>::access_role;
    else return Role::GenericContainer;
}

template <typename T>
AccessInfo access_info(const Field<T>& field) {
    AccessInfo info;
    info.role = access_role_of<T>();
    if constexpr (requires(AccessInfo& a) { Widget<T>::describe(field, a); }) Widget<T>::describe(field, info);
    return info;
}

// --- Node factory functions (need complete WidgetNode) ---

template <typename T>
Node node_leaf(Field<T>& field, WidgetId& next_id) {
    Node n;
    n.id = next_id++;
    n.is_leaf = true;
    widget_node_detail::set_debug_name<T>(n);

    n.build_widget = [&field](WidgetNode& wn) {
        if constexpr (is_widget_v<T>) {
            wn.focus_policy = Widget<T>::focus_policy;
            wn.access = [&field] { return access_info(field); };
            if constexpr (requires(const WidgetNode& n) { Widget<T>::intrinsic_width(field, n); })
                wn.intrinsic_width = [&field](const WidgetNode& n) { return Widget<T>::intrinsic_width(field, n); };
            if constexpr (requires { Widget<T>::expand; })
                wn.expand = Widget<T>::expand;
            if constexpr (requires { Widget<T>::opens_menu; })
                wn.opens_menu = Widget<T>::opens_menu;
            if constexpr (requires(const KeyPress& kp) { Widget<T>::try_shortcut(field, kp, wn); })
                wn.try_shortcut = [&field](const KeyPress& kp, WidgetNode& self) { return Widget<T>::try_shortcut(field, kp, self); };
            if constexpr (requires { Widget<T>::mnemonic_key(field); })
                wn.mnemonic_key = [&field] { return Widget<T>::mnemonic_key(field); };
            if constexpr (requires { Widget<T>::context_menu; })
                wn.context_menu = Widget<T>::context_menu;
            if constexpr (requires { Widget<T>::popup_anchor; })
                wn.popup_anchor = Widget<T>::popup_anchor;
            if constexpr (requires { Widget<T>::expand_axis; })
                wn.expand_axis = Widget<T>::expand_axis;
            wn.record = [&field](WidgetNode& node) {
                node.draws.clear();
                node.overlay_draws.clear();
                Widget<T>::record(node.draws, field, node);
            };
            wn.wire = [&field](WidgetNode& node) {
                node.connections.push_back(
                    node.on_input.connect([&field, &node](const InputEvent& ev) {
                        Widget<T>::handle_input(field, ev, node);
                    })
                );
            };
        } else {
            wn.focus_policy = FocusPolicy::none;
            wn.record = widget_node_detail::record_fallback_box;
        }
        wn.record(wn);
    };

    n.on_change = [&field](std::function<void()> cb) -> Connection {
        return field.on_change().connect([cb = std::move(cb)](const T&) { cb(); });
    };

    return n;
}

// A number drawn and edited as a slider with fixed bounds: `look` gives them, the caption and
// the unit; the field keeps only the number ([[=core::range<...>]] on a model's member).
template <Numeric T>
Node node_slider_leaf(Field<T>& field, Slider<T> look, WidgetId& next_id) {
    using W = Widget<Slider<T>>;
    Node n;
    n.id = next_id++;
    n.is_leaf = true;
    auto as_slider = [&field, look] {
        auto s = look;
        s.value = field.get();
        return Field<Slider<T>>{std::move(s)};
    };
    n.build_widget = [&field, as_slider](WidgetNode& wn) {
        wn.focus_policy = W::focus_policy;
        wn.expand = W::expand;
        wn.expand_axis = W::expand_axis;
        wn.access = [as_slider] { return access_info(as_slider()); };
        wn.record = [as_slider](WidgetNode& node) {
            node.draws.clear();
            node.overlay_draws.clear();
            W::record(node.draws, as_slider(), node);
        };
        wn.wire = [&field, as_slider](WidgetNode& node) {
            node.connections.push_back(node.on_input.connect([&field, &node, as_slider](const InputEvent& ev) {
                auto slider = as_slider();
                W::handle_input(slider, ev, node);
                if (slider.get().value != field.get()) field.set(slider.get().value);
            }));
        };
        wn.record(wn);
    };
    n.on_change = [&field](std::function<void()> cb) -> Connection {
        return field.on_change().connect([cb = std::move(cb)](const T&) { cb(); });
    };
    return n;
}

template <typename T, typename Observable>
Node node_readonly_leaf(Observable& obs, WidgetId& next_id) {
    Node n;
    n.id = next_id++;
    n.is_leaf = true;
    widget_node_detail::set_debug_name<T>(n);

    n.build_widget = [&obs](WidgetNode& wn) {
        wn.focus_policy = FocusPolicy::none;
        if constexpr (is_widget_v<T>) {
            wn.record = [&obs](WidgetNode& node) {
                node.draws.clear();
                node.overlay_draws.clear();
                Field<T> tmp{obs.get()};
                Widget<T>::record(node.draws, tmp, node);
            };
        } else {
            wn.record = widget_node_detail::record_fallback_box;
        }
        wn.record(wn);
    };

    n.on_change = [&obs](std::function<void()> cb) -> Connection {
        return obs.on_change().connect([cb = std::move(cb)](const T&) { cb(); });
    };

    return n;
}

template <typename T>
    requires requires(T& t, DrawList& dl, Rect r, const WidgetNode& n) {
        t.canvas(dl, r, n);
    }
Node node_canvas(T& model, WidgetId& next_id) {
    Node n;
    n.id = next_id++;
    n.is_leaf = true;
    n.layout_kind = LayoutKind::Canvas;

    n.build_widget = [&model](WidgetNode& wn) {
        wn.record = [&model](WidgetNode& node) {
            node.draws.clear();
            model.canvas(node.draws, node.canvas_bounds, node);
        };
        wn.record(wn);

        if constexpr (requires(T& t, const InputEvent& ev, WidgetNode& nd, Rect r) {
                           t.handle_canvas_input(ev, nd, r);
                       }) {
            wn.focus_policy = FocusPolicy::tab_and_click;
            wn.wire = [&model](WidgetNode& node) {
                node.connections.push_back(
                    node.on_input.connect([&model, &node](const InputEvent& ev) {
                        model.handle_canvas_input(ev, node, node.canvas_bounds);
                    })
                );
            };
        }
    };

    return n;
}

} // namespace prism::ui
