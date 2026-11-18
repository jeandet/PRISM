#pragma once

#include <prism/core/connection.hpp>
#include <prism/ui/delegate.hpp>
#include <prism/ui/frame.hpp>
#include <prism/input/hit_test.hpp>
#include <prism/render/scene_snapshot.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace prism::ui {
using namespace prism::core;
using namespace prism::render;


struct WidgetNode;
struct TableState;
struct TabsState;

// A board's descendant's rect in the board's coordinates, from the layout just made, zoomed as
// it is drawn.
using BoardRectOf = std::function<std::optional<Rect>(WidgetId)>;
// Drawn in the board's coordinates; may ask where the board's descendants lie. `scale`: the
// cards' zoom, for what is drawn to their scale (a wire's thickness).
using BoardDraw = std::function<void(DrawList&, const BoardRectOf&, float scale)>;

// Where a drop landed, in the target widget's coordinates, and the widget's size: enough to
// tell before from after, or which row.
struct DropPlace {
    Point at;
    Size size;
    std::size_t slot = 0;   // between which of the target's items (DragRole::slot), when it has them
};

// A drop at `p` (window coordinates) in the target's own coordinates: where it is drawn, at its zoom.
inline DropPlace drop_place_in(const input::Placed& target, Point p) {
    const float s = target.scale;
    return {Point{X{(p.x.raw() - target.rect.origin.x.raw()) / s}, Y{(p.y.raw() - target.rect.origin.y.raw()) / s}},
            Size{Width{target.rect.extent.w.raw() / s}, Height{target.rect.extent.h.raw() / s}}};
}

// Where a container taking drops is, in the window: `rect` places drops in its coordinates,
// `visible` (what the scroll views and boards around it leave) is where a drop can land.
struct DropArea {
    WidgetId id = 0;
    Rect rect;
    Rect visible;
    float scale = 1.f;   // its zoom (a zoomed board's card): its coordinates are rect's, divided by it
};

// What a widget does in a drag inside the app (doc/design/drag-and-docking.md). The value is
// typed; std::any carries it between source and target, and the target checks its type.
struct DragRole {
    // What a press at `at` (in the widget's coordinates) drags; an empty any: nothing.
    std::function<std::any(const WidgetNode& self, Point at)> source{};
    // The part of the widget that follows the pointer (a tab of a tab bar); none: all of it.
    std::function<std::optional<Rect>(const WidgetNode& self, Point at)> ghost_area{};
    // A drag from this source let go outside the window (a dock tab: the panel floats).
    std::function<void(const std::any&)> outside{};
    std::function<bool(const std::any&)> accepts{};            // a target: takes this value?
    std::function<void(const std::any&, DropPlace)> drop{};
    bool between = false;   // drops go before or after it (a list row): shown as a line, not an outline
    bool inside_middle = false;   // with between: its middle half takes drops inside it (a tree folder)
    bool zones = false;           // a dock group: the middle or an edge, shown as the area it would take
    bool edge_zones = false;      // a dock's container: the four buttons on its edges, drawn while dragging
    // The parts of it that take a drop over whatever lies under them (a dock's edge buttons, over
    // its groups); none: all of it, and the innermost target holding the pointer wins.
    std::function<bool(DropPlace)> claims{};
    // A target that takes some values only at some places in it (a dock group: a floating card,
    // on its guide's buttons); none: anywhere it accepts them.
    std::function<bool(const std::any&, DropPlace)> takes_at{};
    // A target whose drops land between its items (a tab bar's tabs): the slot a drop at `at`
    // takes (DropPlace::slot), and the line drawn there while dragging, in its coordinates.
    std::function<std::size_t(const WidgetNode& self, Point at)> slot{};
    std::function<Rect(const WidgetNode& self, std::size_t slot)> slot_marker{};
    // A source whose drag is drawn by this instead of a ghost of it (a node editor's wire), with
    // the dragged value and the pointer, in window coordinates.
    std::function<void(DrawList&, const std::any& dragged, Point pointer)> trail{};
    // A source whose drag shows a soft marker on every target that takes it, not only on the one
    // under the pointer.
    bool hint_targets = false;
};

// A condition a widget is enabled under (vb.widget(f).enabled(flag), vb.enabled_if): read live;
// `subscribe` tells the tree when it may have changed.
struct Enabler {
    std::function<bool()> get;
    std::function<Connection(std::function<void()>)> subscribe;
};

struct Node {
    WidgetId id = 0;
    bool is_leaf = false;
    std::string name;   // accessible name (reflected member name); copied to WidgetNode::name
    std::string tooltip;   // shown when the pointer rests on the widget; copied to WidgetNode
    std::optional<Framing> frame;   // a Row/Column's look around its content; copied to WidgetNode
    bool focus_on_open = false;   // takes the focus when the popup holding it opens; copied too
    std::function<void(Node&)> popup_builder;   // ui::Popup content; copied to WidgetNode
    std::vector<Enabler> enablers;   // all true: enabled (none: always)
    DragRole drag;                   // copied to WidgetNode
    std::function<DragRole(std::size_t)> row_drag;   // a list's rows (vb.list(...).reorderable())
    // A container whose content is built while the app runs (a dock): built on the first frame,
    // and again whenever content_stale() says so. Copied to WidgetNode.
    std::function<void(Node&)> build_content;
    std::function<bool()> content_stale;
    // LayoutKind::Layers: where the children after the first lie, in its coordinates. Copied too.
    std::function<std::vector<Rect>()> layer_places;
    // LayoutKind::Board: where the children after the first lie, in its coordinates, read at
    // every layout; and what it draws above its first child (under) and above all (over). Copied.
    std::function<std::vector<Point>()> board_places;
    BoardDraw board_under;
    BoardDraw board_over;
    std::function<float()> board_scale;   // its cards' zoom, read at every layout; unset = 1. Copied.
    // Kept when what holds it is rebuilt (a dock panel moved elsewhere): the built widgets are
    // parked under this key and put back where a node with the same key is built next.
    std::string keep_key;
    // A split container's remembered pane sizes (vb.remember_split): read when built, written
    // when a drag ends.
    std::function<std::vector<float>()> split_get;
    std::function<void(std::vector<float>)> split_set;
#ifdef PRISM_DEBUG_TOOLS_ENABLED
    std::string debug_name;
#endif
    std::optional<std::string> block;   // placed with vb.block(): the block's type name
    LayoutKind layout_kind = LayoutKind::Default;
    float spacer_size = 0.f;   // a Spacer: 0 stretches, more is a fixed gap of that many points
    std::vector<Node> children;

    // Type-erased WidgetNode builder (leaf nodes only)
    std::function<void(WidgetNode&)> build_widget;

    // Type-erased change subscription (leaf nodes only)
    std::function<Connection(std::function<void()>)> on_change;

    // Multiple dependencies for canvas nodes with depends_on()
    std::vector<std::function<Connection(std::function<void()>)>> dependencies;

    // Scroll container metadata (only meaningful when layout_kind == Scroll)
    ScrollBarPolicy scroll_bar_policy = ScrollBarPolicy::Auto;
    ScrollEventPolicy scroll_event_policy = ScrollEventPolicy::BubbleAtBounds;

    // Virtual list metadata (only meaningful when layout_kind == VirtualList)
    std::function<void(WidgetNode&, size_t index)> vlist_bind_row;
    std::function<void(WidgetNode&)> vlist_unbind_row;
    std::function<Connection(size_t, std::function<void()>)> vlist_on_insert;
    std::function<Connection(size_t, std::function<void()>)> vlist_on_remove;
    std::function<Connection(size_t, std::function<void()>)> vlist_on_update;
    size_t vlist_item_count = 0;
    bool vlist_follow_tail = false;   // vb.list(...).follow_tail()

    // Table metadata (only meaningful when layout_kind == Table)
    std::shared_ptr<TableState> table_state;

    // Tabs metadata (only meaningful when layout_kind == Tabs)
    std::shared_ptr<TabsState> tabs_state;

    // Shared<T> drain callback (set for Shared<T> leaf nodes)
    std::function<void()> drain_fn;
    std::function<void(std::function<void()>)> bind_waker_fn;  // set with drain_fn for cross-thread state

    // Opt-in fixed main-axis size for a Canvas node (only meaningful when
    // layout_kind == Canvas); unset means the canvas keeps the default expand behavior.
    std::optional<float> canvas_min_width;
    std::optional<float> canvas_min_height;
};

} // namespace prism::ui
