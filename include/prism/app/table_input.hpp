#pragma once

#include <prism/app/widget_tree_traversal.hpp>
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
    if (auto c = column_at(edges, x)) {
        ts.sort.set(next_sort(ts.sort.get(), *c));
        return {true, CursorShape::Default};
    }
    return {false, CursorShape::Default};
}

inline TableInputResult body_press(TableState& ts, Point local, uint16_t m) {
    float body_y = local.y.raw() - ts.row_height.raw() + ts.scroll_y.raw();
    auto pos = static_cast<size_t>(body_y / ts.row_height.raw());
    if (pos >= ts.row_count()) return {false, CursorShape::Default};
    size_t row = source_row(ts, pos);
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
    if (!mb.pressed) {
        bool was_resizing = ts.resizing.has_value();
        ts.resizing.reset();
        return {was_resizing, hover_cursor(ts, mb.position)};
    }
    return in_header(ts, mb.position) ? header_press(ts, mb.position) : body_press(ts, mb.position, mb.mods);
}

inline TableInputResult on_move(TableState& ts, const MouseMove& mm) {
    if (!ts.resizing) return {false, hover_cursor(ts, mm.position)};
    size_t c = ts.resizing->column;
    float left = column_edges(ts)[c];
    set_column_width(ts, c, content_x(ts, mm.position) - ts.resizing->grab_dx - left);
    return {true, CursorShape::ResizeEW};
}

inline TableInputResult on_scroll(TableState& ts, const MouseScroll& ms) {
    DY y{std::clamp(ts.scroll_y.raw() + ms.dy.raw() * ts.row_height.raw() * 3.f, 0.f, max_scroll_y(ts).raw())};
    DX x{std::clamp(ts.scroll_x.raw() + ms.dx.raw() * 20.f, 0.f, max_scroll_x(ts))};
    bool moved = y != ts.scroll_y || x != ts.scroll_x;
    ts.scroll_y = y;
    ts.scroll_x = x;
    return {moved, std::nullopt};
}

inline void move_current(TableState& ts, size_t pos, bool extend) {
    if (extend && ts.anchor_row)
        select_range_to(ts, pos);
    else
        select_single(ts, source_row(ts, pos));
}

inline TableInputResult on_key(TableState& ts, const KeyPress& kp) {
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
    return {};
}

} // namespace prism::app
