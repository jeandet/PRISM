#pragma once

#include <prism/render/scene_snapshot.hpp>
#include <prism/ui/widget_node.hpp>

#include <algorithm>
#include <optional>
#include <string>
#include <unordered_map>

// Block outlines (WidgetTree::show_blocks): a frame around everything each vb.block() laid out,
// with the block's name on a tab at its top-left corner. Drawn into the snapshot's overlay, so it
// sits on top of the widgets and changes none of them.
namespace prism::app {

namespace block_outline_detail {

inline constexpr core::Color kColour = core::Color::rgba(124, 60, 245);   // the block logo's purple
inline constexpr float kLabelSize = 11.f;
inline constexpr float kLabelPad = 4.f;
// simplify: the label's tab is sized from an average glyph width, not measured; a measured
// width needs the window's text measurer here. Good enough for type names.
inline constexpr float kAverageGlyph = 6.4f;

inline core::Rect united(core::Rect a, core::Rect b) {
    const float x0 = std::min(a.origin.x.raw(), b.origin.x.raw());
    const float y0 = std::min(a.origin.y.raw(), b.origin.y.raw());
    const float x1 = std::max(a.origin.x.raw() + a.extent.w.raw(), b.origin.x.raw() + b.extent.w.raw());
    const float y1 = std::max(a.origin.y.raw() + a.extent.h.raw(), b.origin.y.raw() + b.extent.h.raw());
    return {core::Point{core::X{x0}, core::Y{y0}}, core::Size{core::Width{x1 - x0}, core::Height{y1 - y0}}};
}

// What `node` and everything under it cover on the screen. Nothing when none of it was laid out
// (a tab page not shown).
inline std::optional<core::Rect> extent(const ui::WidgetNode& node,
                                        const std::unordered_map<ui::WidgetId, core::Rect>& placed) {
    std::optional<core::Rect> covered;
    if (auto it = placed.find(node.id); it != placed.end()) covered = it->second;
    for (const auto& child : node.children)
        if (auto c = extent(child, placed)) covered = covered ? united(*covered, *c) : *c;
    return covered;
}

// The name hangs above the frame's top edge, at its right end: over a panel's title bar rather
// than over the block's own first widget.
inline void draw_one(render::DrawList& dl, core::Rect around, const std::string& name) {
    dl.rect_outline(around, kColour, 2.f);
    const float tab_w = kLabelPad * 2.f + kAverageGlyph * static_cast<float>(name.size());
    const float tab_h = kLabelSize + kLabelPad * 2.f;
    const core::X left{around.origin.x.raw() + std::max(0.f, around.extent.w.raw() - tab_w)};
    const core::Y top{std::max(0.f, around.origin.y.raw() - tab_h)};
    dl.filled_rect({core::Point{left, top}, core::Size{core::Width{tab_w}, core::Height{tab_h}}}, kColour);
    dl.text(name, core::Point{left + core::DX{kLabelPad}, top + core::DY{kLabelPad}}, kLabelSize,
            core::Color::rgba(255, 255, 255));
}

// `visible`: what the enclosing scroll views leave on screen. A scroll view's own rectangle in
// the snapshot is its viewport, while its children keep their full extent.
inline void draw_all(const ui::WidgetNode& node, const std::unordered_map<ui::WidgetId, core::Rect>& placed,
                     std::optional<core::Rect> visible, render::DrawList& dl) {
    if (node.block)
        if (auto around = extent(node, placed))
            draw_one(dl, visible ? visible->intersect(*around) : *around, *node.block);
    if (auto it = placed.find(node.id); node.layout_kind == ui::LayoutKind::Scroll && it != placed.end())
        visible = visible ? visible->intersect(it->second) : it->second;
    for (const auto& child : node.children) draw_all(child, placed, visible, dl);
}

} // namespace block_outline_detail

inline void draw_block_outlines(const ui::WidgetNode& root, render::SceneSnapshot& snap) {
    std::unordered_map<ui::WidgetId, core::Rect> placed;
    for (const auto& [id, rect] : snap.geometry) placed.emplace(id, rect);
    block_outline_detail::draw_all(root, placed, std::nullopt, snap.overlay);
}

} // namespace prism::app
