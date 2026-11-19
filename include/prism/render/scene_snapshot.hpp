#pragma once

#include <prism/render/draw_list.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

namespace prism::render {

using WidgetId = uint64_t;

// Complete, immutable description of what should be on screen.
// All vectors are parallel — indexed by the same position.
// z_order contains indices into geometry/draw_lists, back-to-front.
// What the backend paints itself: the window behind the widgets and its custom title bar. From
// the app's theme (ui::Theme), so a theme change reaches them too.
struct WindowColors {
    Color background = Color::rgba(30, 30, 30);
    Color chrome_bg = Color::rgba(45, 45, 48);
    Color chrome_border = Color::rgba(60, 60, 65);
    Color chrome_text = Color::rgba(200, 200, 200);
    Color chrome_icon = Color::rgba(180, 180, 180);
    Color chrome_close = Color::rgba(196, 43, 28);
    bool operator==(const WindowColors&) const = default;
};

struct SceneSnapshot {
    uint64_t version = 0;
    WindowColors window;
    Size viewport{Width{0}, Height{0}};  // the window, in the same coordinates as geometry
    std::vector<std::pair<WidgetId, Rect>> geometry;
    // Parallel to geometry: the part of each entry that the scroll views and boards around it
    // leave visible, all a click can hit. geometry keeps the whole rect, as its origin is where
    // the widget's own coordinates start. Empty in a snapshot built by hand: hits use geometry.
    std::vector<Rect> visible;
    // Parallel to geometry: the zoom each entry is drawn at (a zoomed board's cards), 1 elsewhere.
    // An event reaches the widget at (position - origin) / scale. Empty in a snapshot built by
    // hand: read as 1.
    std::vector<float> scale;
    // Per-widget entries may be shared with a previous snapshot (see layout_flatten's
    // per-widget DrawList cache) -- const so nothing downstream can mutate a shared entry
    // out from under another snapshot still holding it.
    std::vector<std::shared_ptr<const DrawList>> draw_lists;
    std::vector<uint32_t> z_order; // uint32_t: a uint16_t silently wraps past 65535 entries
    DrawList overlay;  // rendered last, on top of everything, no clip
    std::vector<std::pair<WidgetId, Rect>> overlay_geometry;  // hit-test regions for overlays
    // The whole rect of each widget of an open popup (its overlay_geometry entry is only the
    // visible part): where its own coordinates start.
    std::vector<std::pair<WidgetId, Rect>> overlay_placed;
    // Scrollbar tracks (also in overlay_geometry): only a press here starts a thumb drag. Kept
    // apart because a popup's widgets are in overlay_geometry too.
    std::vector<std::pair<WidgetId, Rect>> scrollbar_geometry;

    // Instrumentation, populated by WidgetTree::build_snapshot(). Not part of the
    // rendered scene -- for the debug tree inspector / perf overlay.
    double build_time_ms = 0.0;
    std::size_t dirty_widget_count = 0;
    std::size_t draw_command_count = 0;
    std::size_t approx_bytes = 0;
    std::size_t layout_pass_count = 0;  // 1 normally, 2 when a VirtualList/Table viewport changed
    std::size_t layout_nodes_reused = 0;   // layout nodes kept from the last publish, not rebuilt
    std::chrono::steady_clock::time_point built_at{};  // when build_snapshot() finished
};

// resolve_clips() writes a ClipPush only when the rect the active outer clips resolve it to
// differs from the one stored, and only into a DrawList owned solely by the snapshot being
// resolved (use_count() == 1). A list's own nested clips are already resolved when the list
// was built (resolve_nested_clips below, called by layout_flatten before a list is published
// or cached), and intersection is idempotent, so for such a list the outer intersection is
// the stored rect itself and nothing is written; layout_flatten shares a list across
// snapshots only when the widget has no clipping ancestor, so a list reached with a clip
// already active was freshly built for this snapshot and is not aliased. When that invariant
// does not hold, this reports it and aborts — always on, not only under assert. Defined in
// libprism (src/render/scene_snapshot.cpp).
[[noreturn]] void clip_sharing_violation();

// Resolves the ClipPush rects nested inside ONE list against the clips the list itself pushes
// (the stack starts empty): a clip a widget drew inside the list is cut to the ClipPush that
// wraps the list, once, when the list is built — call this before the list is published into a
// snapshot (and before it is cached for reuse), while its builder still owns it alone.
// Afterwards a resolve_clips() over a snapshot holding the list finds each nested rect already
// equal to what the outer clips produce (intersection is idempotent), so it writes nothing.
inline void resolve_nested_clips(DrawList& dl) {
    std::vector<Rect> stack;
    for (auto& cmd : dl.commands) {
        if (auto* cp = std::get_if<ClipPush>(&cmd)) {
            Rect r = cp->rect;
            if (!stack.empty()) cp->rect = r = stack.back().intersect(r);
            stack.push_back(r);
        } else if (std::holds_alternative<ClipPop>(cmd)) {
            if (!stack.empty()) stack.pop_back();
        }
    }
}

// Pre-intersect all ClipPush rects so backends receive final clip regions.
// Must be called after layout_flatten, before handing the snapshot to any backend.
inline void resolve_clips(SceneSnapshot& snap) {
    struct ResolvedPush {
        uint32_t list;
        std::size_t command;
        Rect rect;
    };
    // Pass 1, read-only: every ClipPush that sits under an active clip AND whose resolved rect
    // differs from the stored one — only such a command is ever written, so a list with none
    // (a shared, already-resolved one among them) is never touched below.
    std::vector<Rect> stack;
    std::vector<ResolvedPush> writes;
    for (uint32_t idx : snap.z_order) {
        const auto& list = snap.draw_lists[idx];
        for (std::size_t i = 0; i < list->commands.size(); ++i) {
            const auto& cmd = list->commands[i];
            if (const auto* cp = std::get_if<ClipPush>(&cmd)) {
                Rect r = cp->rect;
                if (!stack.empty()) {
                    r = stack.back().intersect(r);
                    if (r != cp->rect) writes.push_back({idx, i, r});
                }
                stack.push_back(r);
            } else if (std::holds_alternative<ClipPop>(cmd)) {
                if (!stack.empty()) stack.pop_back();
            }
        }
    }
    // Pass 2: apply them. A list written here must be owned solely by this snapshot — a shared
    // one means the invariant above is broken, and writing it would mutate a list another
    // snapshot still holds, so report and abort instead of const_casting it.
    for (const auto& w : writes) {
        if (snap.draw_lists[w.list].use_count() != 1) clip_sharing_violation();
        auto& dl = *std::const_pointer_cast<DrawList>(snap.draw_lists[w.list]);
        std::get<ClipPush>(dl.commands[w.command]).rect = w.rect;
    }
}

} // namespace prism::render
