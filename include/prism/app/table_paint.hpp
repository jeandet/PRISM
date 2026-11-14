#pragma once

#include <prism/render/draw_list.hpp>
#include <prism/ui/table_ops.hpp>
#include <prism/ui/context.hpp>
#include <prism/ui/widget_node.hpp>

#include <algorithm>
#include <string>
#include <vector>

// What a table draws for its visible rows and its header; `edges` are the column edges
// (ui::column_edges). Pure drawing: WidgetTree::materialize_table places the results.
namespace prism::app::widget_detail {
using namespace prism::core;
using namespace prism::render;
using namespace prism::ui;

// The editor over a cell being edited (clip-relative): a field, the selection, the text and the
// caret; outlined in the error colour when the source refused the last commit.
inline void paint_cell_editor(DrawList& dl, const CellEdit& e, Size cell, const Theme& theme) {
    draw_line_editor(dl, e.text, e.rejected, Rect{Point{X{0}, Y{0}}, cell}, 14.f, theme);
}

// A cell's content (clip-relative): its box, or its icon and text.
inline void paint_cell(DrawList& dl, const TableState& ts, size_t row, size_t c, const Theme& theme) {
    constexpr float box = 16.f;
    Y mid{(ts.row_height.raw() - box) / 2};
    if (auto on = ts.source.cell_check ? ts.source.cell_check(row, c) : std::nullopt)
        return draw_check_box(dl, X{4.f}, mid, *on, WidgetVisualState{}, theme);
    X x{4.f};
    if (auto img = ts.source.cell_image ? ts.source.cell_image(row, c) : nullptr) {
        dl.image(img, Rect{Point{x, mid}, Size{Width{box}, Height{box}}}, icon_tint(img, theme.text));
        x = x + DX{box + 4.f};
    }
    dl.text(ts.source.cell_text(row, c), Point{x, Y{4.f}}, 14.f, theme.text);
}

// Paints one row (recycled from the pool when possible): zebra/selection
// background, per-cell clipped text, and column dividers.
inline WidgetNode paint_table_row(TableState& ts, size_t view_pos, const std::vector<float>& edges, const Theme& theme) {
    WidgetNode wn;
    if (!ts.pool.empty()) {
        wn = std::move(ts.pool.back());
        ts.pool.pop_back();
    }
    wn.id = 0;  // cells are hit-test transparent; the table container handles input
    wn.dirty = true;
    wn.draws.clear();
    wn.theme = &theme;

    size_t row = source_row(ts, view_pos);
    auto bg = is_selected(ts, row) ? theme.table_selected
            : view_pos % 2 == 0    ? theme.table_row_even
                                   : theme.table_row_odd;
    float dx = -ts.scroll_x.raw();
    Width total_w{std::max(edges.back() + dx, ts.viewport_w.raw())};
    wn.draws.filled_rect(Rect{Point{X{0}, Y{0}}, Size{total_w, ts.row_height}}, bg);

    for (size_t c = 0; c < ts.column_count; ++c) {
        X cx{edges[c] + dx};
        Width cw{edges[c + 1] - edges[c]};
        wn.draws.clip_push(Point{cx, Y{0}}, Size{cw, ts.row_height});
        if (ts.editing && ts.editing->row == row && ts.editing->column == c)
            paint_cell_editor(wn.draws, *ts.editing, Size{cw, ts.row_height}, theme);
        else
            paint_cell(wn.draws, ts, row, c, theme);
        wn.draws.clip_pop();
        if (c > 0)
            wn.draws.filled_rect(Rect{Point{cx, Y{0}}, Size{Width{1.f}, ts.row_height}},
                                 theme.table_divider);
    }
    return wn;
}

inline void draw_sort_arrow(DrawList& dl, X right, bool descending, Color c) {
    float x = right.raw() - 14.f, y = 12.f, w = 8.f, h = 5.f;
    if (descending)
        dl.filled_polygon({Point{X{x}, Y{y - h / 2}}, Point{X{x + w}, Y{y - h / 2}},
                           Point{X{x + w / 2}, Y{y + h / 2}}}, c);
    else
        dl.filled_polygon({Point{X{x + w / 2}, Y{y - h / 2}}, Point{X{x + w}, Y{y + h / 2}},
                           Point{X{x}, Y{y + h / 2}}}, c);
}

// Renders header cells into overlay_draws (picked up by table flatten).
inline void render_table_header(WidgetNode& node, const TableState& ts, const std::vector<float>& edges, const Theme& theme) {
    node.overlay_draws.clear();
    auto sort = ts.sort.get();
    float dx = -ts.scroll_x.raw();
    for (size_t c = 0; c < ts.column_count; ++c) {
        X cx{edges[c] + dx};
        X right{edges[c + 1] + dx};
        Width cw{edges[c + 1] - edges[c]};
        node.overlay_draws.clip_push(Point{cx, Y{0}}, Size{cw, ts.row_height});  // origin moves to cx
        node.overlay_draws.text(std::string(ts.column_header(c)), Point{X{4.f}, Y{4.f}},
                                14.f, theme.table_header_text);
        if (sort && sort->column == c)
            draw_sort_arrow(node.overlay_draws, X{cw.raw()}, sort->descending, theme.table_header_text);
        node.overlay_draws.clip_pop();
        node.overlay_draws.filled_rect(Rect{Point{right, Y{4.f}},
                                            Size{Width{1.f}, ts.row_height - Height{8.f}}},
                                       theme.table_header_divider);
    }
}

} // namespace prism::app::widget_detail
