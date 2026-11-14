#pragma once

#include <prism/app/widget_tree_traversal.hpp>
#include <prism/delegates/text_delegates.hpp>
#include <prism/input/input_event.hpp>
#include <prism/ui/cursor.hpp>
#include <prism/ui/table_ops.hpp>

#include <optional>

namespace prism::app {

struct TableInputResult {
    bool repaint = false;
    std::optional<ui::CursorShape> cursor;  // set on pointer events
};

namespace table_input_detail {
using namespace prism::core;
using namespace prism::input;
using namespace prism::ui;

inline DY max_scroll_y(const TableState& ts) {
    return DY{std::max(0.f, ts.row_height.raw() * static_cast<float>(ts.row_count()) - ts.viewport_h.raw())};
}

inline void reveal_current(TableState& ts) {
    auto row = ts.selected_row.get();
    if (!row) return;
    DY top{static_cast<float>(view_position(ts, *row)) * ts.row_height.raw()};
    ts.scroll_y = widget_detail::reveal_row_offset(top, ts.row_height, ts.scroll_y, ts.viewport_h, max_scroll_y(ts));
}

inline bool in_header(const TableState& ts, Point local) { return local.y.raw() < ts.row_height.raw(); }
inline float content_x(const TableState& ts, Point local) { return local.x.raw() + ts.scroll_x.raw(); }

inline CursorShape hover_cursor(const TableState& ts, Point local) {
    bool on_edge = in_header(ts, local) && resize_handle_at(column_edges(ts), content_x(ts, local));
    return on_edge || ts.resizing ? CursorShape::ResizeEW : CursorShape::Default;
}

inline TableInputResult header_press(TableState& ts, Point local) {
    auto edges = column_edges(ts);
    float x = content_x(ts, local);
    if (auto c = resize_handle_at(edges, x)) {
        ts.resizing = ColumnResize{*c, x - edges[*c + 1]};
        return {false, CursorShape::ResizeEW};
    }
    if (auto c = column_at(edges, x)) ts.header_drag = HeaderDrag{*c, x, x};   // sorts or moves on release
    return {false, CursorShape::Default};
}

// A header released: a click sorts by its column, a drag drops the column where it is.
inline TableInputResult header_release(TableState& ts) {
    auto d = *ts.header_drag;
    ts.header_drag.reset();
    auto shown = shown_columns(ts);
    if (d.position >= shown.size()) return {true, std::nullopt};
    if (d.moving) move_column(ts, d.position, drop_position(column_edges(ts), d.position, d.x));
    else ts.sort.set(next_sort(ts.sort.get(), shown[d.position]));
    return {true, std::nullopt};
}

// Beyond this many pixels, a pressed header is dragged, not clicked.
inline constexpr float header_drag_slop = 4.f;

// The column whose checkbox (4 to 20 px into the cell) is under `local`, in source row `row`.
// simplify: Space does not toggle a box yet (the keys move rows); add it with a current cell.
inline std::optional<size_t> check_box_at(const TableState& ts, size_t row, Point local) {
    auto edges = column_edges(ts);
    float x = content_x(ts, local);
    auto pos = column_at(edges, x);
    if (!pos) return std::nullopt;
    size_t c = shown_columns(ts)[*pos];
    float in = x - edges[*pos];
    return is_check_cell(ts, row, c) && in >= 4.f && in < 20.f ? std::optional<size_t>{c} : std::nullopt;
}

inline TableInputResult body_press(TableState& ts, Point local, uint16_t m, uint8_t clicks) {
    if (ts.editing && !commit_edit(ts)) cancel_edit(ts);   // a click elsewhere ends the edit
    float body_y = local.y.raw() - ts.row_height.raw() + ts.scroll_y.raw();
    auto pos = static_cast<size_t>(body_y / ts.row_height.raw());
    if (pos >= ts.row_count()) return {true, CursorShape::Default};
    size_t row = source_row(ts, pos);
    if (auto c = check_box_at(ts, row, local)) {   // every click on a box flips it
        toggle_check(ts, row, *c);
        return {true, CursorShape::Default};
    }
    if (clicks >= 2) {   // a double-click edits the cell
        select_single(ts, row);
        if (auto pos = column_at(column_edges(ts), content_x(ts, local))) begin_edit(ts, row, shown_columns(ts)[*pos]);
        return {true, CursorShape::Default};
    }
    if (m & mods::shift)
        select_range_to(ts, pos);
    else if (mods::shortcut(m))
        toggle_row(ts, row);
    else if (only_selected(ts, row))
        clear_selection(ts);
    else
        select_single(ts, row);
    return {true, CursorShape::Default};
}

inline TableInputResult on_button(TableState& ts, const MouseButton& mb) {
    if (mb.button != buttons::left) return {};
    if (!mb.pressed && ts.header_drag) return header_release(ts);
    if (!mb.pressed) {
        bool was_resizing = ts.resizing.has_value();
        ts.resizing.reset();
        return {was_resizing, hover_cursor(ts, mb.position)};
    }
    return in_header(ts, mb.position) ? header_press(ts, mb.position) : body_press(ts, mb.position, mb.mods, mb.clicks);
}

inline TableInputResult on_move(TableState& ts, const MouseMove& mm) {
    if (!(mm.buttons & buttons::left)) ts.header_drag.reset();             // release was lost
    if (ts.header_drag) {
        auto& d = *ts.header_drag;
        d.x = content_x(ts, mm.position);
        d.moving = d.moving || std::abs(d.x - d.press_x) > header_drag_slop;
        return {d.moving, CursorShape::Default};
    }
    if (ts.resizing && !(mm.buttons & buttons::left)) ts.resizing.reset();  // release was lost
    if (!ts.resizing) return {false, hover_cursor(ts, mm.position)};
    size_t c = ts.resizing->column;
    float left = column_edges(ts)[c];
    set_column_width(ts, shown_columns(ts)[c], content_x(ts, mm.position) - ts.resizing->grab_dx - left);
    return {true, CursorShape::ResizeEW};
}

// Horizontal only: the vertical wheel goes through WidgetTree::scroll_at, like every
// scrollable, which also shows the scrollbar and bubbles to the parent at the ends.
inline TableInputResult on_scroll(TableState& ts, const MouseScroll& ms) {
    DX x{std::clamp(ts.scroll_x.raw() + ms.dx.raw() * 20.f, 0.f, max_scroll_x(ts))};
    bool moved = x != ts.scroll_x;
    ts.scroll_x = x;
    return {moved, std::nullopt};
}

inline void move_current(TableState& ts, size_t pos, bool extend) {
    if (extend && ts.anchor_row)
        select_range_to(ts, pos);
    else
        select_single(ts, source_row(ts, pos));
}

// Keys while a cell is edited: Enter commits, Escape cancels, Tab commits and edits the next
// cell (Shift+Tab the previous one); the rest edit the text.
inline TableInputResult edit_key(TableState& ts, const KeyPress& kp) {
    auto& e = *ts.editing;
    if (kp.key == keys::enter) commit_edit(ts);
    else if (kp.key == keys::escape) cancel_edit(ts);
    else if (kp.key == keys::tab) {
        size_t row = e.row, col = e.column;
        auto next = next_editable_column(ts, col, (kp.mods & mods::shift) ? -1 : +1, row);
        if (commit_edit(ts) && next) begin_edit(ts, row, *next);
    } else if (kp.key == keys::home || kp.key == keys::end) {
        e.text = text_edit::move_to(e.text, kp.key == keys::home ? 0 : e.text.text.size(), (kp.mods & mods::shift) != 0);
    } else if (auto after = text_detail::common_edit_key(e.text, kp)) {
        e.text = *after;
    } else {
        return {};
    }
    return {true, std::nullopt};
}

inline TableInputResult on_key(TableState& ts, const KeyPress& kp) {
    if (ts.editing) return edit_key(ts, kp);
    if (kp.key == keys::f2 && ts.selected_row.get()) {
        if (auto c = first_editable_column(ts, *ts.selected_row.get())) begin_edit(ts, *ts.selected_row.get(), *c);
        return {true, std::nullopt};
    }
    size_t n = ts.row_count();
    if (n == 0) return {};
    bool extend = (kp.mods & mods::shift) != 0;
    auto current = ts.selected_row.get();
    auto pos = current ? std::optional{view_position(ts, *current)} : std::nullopt;
    DY page{ts.viewport_h.raw()};

    if (kp.key == keys::a && mods::shortcut(kp.mods)) {
        select_all(ts);
        return {true, std::nullopt};
    }
    if (kp.key == keys::down) move_current(ts, pos ? std::min(*pos + 1, n - 1) : 0, extend);
    else if (kp.key == keys::up && pos) move_current(ts, *pos > 0 ? *pos - 1 : 0, extend);
    else if (kp.key == keys::home) move_current(ts, 0, extend);
    else if (kp.key == keys::end) move_current(ts, n - 1, extend);
    else if (kp.key == keys::page_down)
        ts.scroll_y = DY{std::min(ts.scroll_y.raw() + page.raw(), max_scroll_y(ts).raw())};
    else if (kp.key == keys::page_up)
        ts.scroll_y = DY{std::max(ts.scroll_y.raw() - page.raw(), 0.f)};
    else
        return {};
    if (kp.key != keys::page_down && kp.key != keys::page_up) reveal_current(ts);
    return {true, std::nullopt};
}

} // namespace table_input_detail

// Pointer positions are local to the table widget (header at y = 0).
inline TableInputResult handle_table_input(ui::TableState& ts, const input::InputEvent& ev) {
    using namespace table_input_detail;
    if (ts.row_height.raw() <= 0.f) return {};
    if (auto* mb = std::get_if<input::MouseButton>(&ev)) return on_button(ts, *mb);
    if (auto* mm = std::get_if<input::MouseMove>(&ev)) return on_move(ts, *mm);
    if (auto* ms = std::get_if<input::MouseScroll>(&ev)) return on_scroll(ts, *ms);
    if (auto* kp = std::get_if<input::KeyPress>(&ev)) return on_key(ts, *kp);
    if (ts.editing) {   // typed or pasted text goes into the cell
        std::string_view text;
        if (auto* ti = std::get_if<input::TextInput>(&ev)) text = ti->text;
        else if (auto* p = std::get_if<input::Paste>(&ev)) text = p->text;
        else return {};
        ts.editing->text = text_edit::insert(ts.editing->text, text, 0);
        ts.editing->rejected = false;
        return {true, std::nullopt};
    }
    return {};
}

} // namespace prism::app
