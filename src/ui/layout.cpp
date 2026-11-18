#include <prism/ui/layout.hpp>
#include <prism/render/view_transform.hpp>

#include <iterator>

namespace prism::ui::layout_detail {

using render::ViewTransform;
using render::scale_draw_list;

namespace {

// How long each list of a snapshot was before a subtree was flattened into it.
struct SnapshotMarks {
    std::size_t geometry = 0, draw_lists = 0, overlay = 0, overlay_geometry = 0, overlay_placed = 0,
                scrollbar_geometry = 0;
};

SnapshotMarks marks_of(const SceneSnapshot& snap) {
    return {snap.geometry.size(), snap.draw_lists.size(), snap.overlay.commands.size(),
            snap.overlay_geometry.size(), snap.overlay_placed.size(), snap.scrollbar_geometry.size()};
}

void zoom_rects(std::vector<std::pair<WidgetId, Rect>>& entries, std::size_t from, const ViewTransform& t) {
    for (std::size_t i = from; i < entries.size(); ++i) entries[i].second = t.apply(entries[i].second);
}

// A copy: the entry may be shared (layout_flatten's per-widget cache), never changed in place.
std::shared_ptr<const DrawList> zoomed_copy(const DrawList& dl, const ViewTransform& t) {
    auto out = std::make_shared<DrawList>(dl);
    scale_draw_list(*out, t);
    return out;
}

void zoom_commands_from(DrawList& dl, std::size_t from, const ViewTransform& t) {
    DrawList tail;
    tail.commands.assign(std::make_move_iterator(dl.commands.begin() + static_cast<std::ptrdiff_t>(from)),
                         std::make_move_iterator(dl.commands.end()));
    scale_draw_list(tail, t);
    std::ranges::move(tail.commands, dl.commands.begin() + static_cast<std::ptrdiff_t>(from));
}

// Every entry appended since `from`, mapped through `t`: where it lies, what clicks can hit of
// it, its zoom, and what it draws, overlays included.
// simplify: text is drawn at size × scale from the layout at zoom 1, not measured again at its
// zoomed size; re-measure at the zoom level if text visibly overflows its widget.
void zoom_entries(SceneSnapshot& snap, const SnapshotMarks& from, const ViewTransform& t) {
    zoom_rects(snap.geometry, from.geometry, t);
    for (std::size_t i = from.geometry; i < snap.geometry.size(); ++i) {
        snap.visible[i] = t.apply(snap.visible[i]);
        snap.scale[i] *= t.scale;
    }
    for (std::size_t i = from.draw_lists; i < snap.draw_lists.size(); ++i)
        if (snap.draw_lists[i]) snap.draw_lists[i] = zoomed_copy(*snap.draw_lists[i], t);
    zoom_commands_from(snap.overlay, from.overlay, t);
    zoom_rects(snap.overlay_geometry, from.overlay_geometry, t);
    zoom_rects(snap.overlay_placed, from.overlay_placed, t);
    zoom_rects(snap.scrollbar_geometry, from.scrollbar_geometry, t);
}

} // namespace

// The cards laid out at zoom 1, flattened against the visible part brought back to zoom 1, then
// mapped to the board's zoom about its origin.
// simplify: a board inside a zoomed board's card is mapped twice, which composes its drawing and
// hits, but its wires and laid_out_at are not composed: unsupported until a nested board is needed.
void flatten_zoomed_cards(LayoutNode& board, SceneSnapshot& snap, Rect inside) {
    const ViewTransform zoom{board.allocated.origin, board.board_scale};
    const ViewTransform unzoom{board.allocated.origin, 1.f / board.board_scale};
    const SnapshotMarks from = marks_of(snap);
    flatten_cards(board, snap, unzoom.apply(inside));
    zoom_entries(snap, from, zoom);
}

} // namespace prism::ui::layout_detail
