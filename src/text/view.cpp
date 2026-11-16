#include <prism/text/view.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace prism::text {

namespace {
using core::DY;
using core::Height;
using core::Point;
using core::Rect;
using core::Size;
using core::Width;
using core::X;
using core::Y;

// simplify: the cache is dropped whole when it holds this many layouts (a long scroll), not
// trimmed least-recently-used first. Upgrade when scrolling back shows the cost.
constexpr std::size_t cache_cap = 4096;
} // namespace

View::View(Document& doc, ViewOptions options) : doc_(doc), options_(options) {
    style_ = {.size = options_.size, .width = 0.f, .family = options_.family, .wrap = options_.wrap};
    reset_heights();
    follow_ = doc_.on_change().connect([this](const Change& c) { follow(c); });
}

// A block's height before it is laid out: its lines at an average glyph width. Cheap on purpose:
// every block of a large document gets one when the width changes.
float View::estimate(const Block& b) const {
    const float line = ui::line_height(style_.measurer, style_.size, style_.family);
    float lines = 1.f;
    if (style_.wrap && style_.width > 0.f)
        lines = std::max(1.f, std::ceil(0.55f * style_.size * static_cast<float>(b.text.size()) / style_.width));
    const float gap = style_.family == render::FontRole::Ui && b.kind == BlockKind::paragraph ? 0.4f * style_.size : 0.f;
    const float scale = b.kind == BlockKind::heading ? 1.6f : 1.f;
    return lines * line * scale + gap;
}

void View::reset_heights() {
    std::vector<float> all;
    all.reserve(doc_.size());
    for (std::size_t i = 0; i < doc_.size(); ++i) all.push_back(estimate(doc_[i]));
    heights_.clear();
    heights_.insert(0, all.begin(), all.end());
}

void View::follow(const Change& c) {
    heights_.erase(c.first, c.first + c.removed);
    std::vector<float> fresh;
    for (std::size_t i = c.first; i < c.first + c.inserted; ++i) fresh.push_back(estimate(doc_[i]));
    heights_.insert(c.first, fresh.begin(), fresh.end());
}

const BlockLayout& View::layout(std::size_t i) {
    const Block& b = doc_[i];
    auto it = cache_.find(b.version);
    if (it == cache_.end()) {
        if (cache_.size() >= cache_cap) cache_.clear();
        it = cache_.emplace(b.version, layout_block(b, {}, style_)).first;
        ++layouts_made_;
    }
    if (heights_[i] != it->second.height) heights_.modify(i, [&](float& h) { h = it->second.height; });
    return it->second;
}

void View::draw(render::DrawList& dl, Size size, const ui::Theme& theme) {
    const float pad = options_.padding;
    const float width = size.w.raw() - 2.f * pad;
    if (width != style_.width || &theme != style_.theme || theme.measurer != style_.measurer) {
        style_.width = width;
        style_.theme = &theme;
        style_.measurer = theme.measurer;
        cache_.clear();
        reset_heights();
    }
    viewport_h = Height{std::max(0.f, size.h.raw() - 2.f * pad)};
    const auto clamp_scroll = [&] {
        content_h = Height{heights_.total().height};
        scroll = DY{std::clamp(scroll.raw(), 0.f, std::max(0.f, content_h.raw() - viewport_h.raw()))};
    };
    clamp_scroll();

    dl.clip_push(Point{X{0}, Y{0}}, size);
    auto at = heights_.seek([](const Heights& h) { return h.height; }, scroll.raw());
    std::size_t i = std::min(at.index, doc_.size() - 1);
    float y = i == at.index ? at.before.height : heights_.prefix(i).height;
    first_drawn_ = i;
    for (; i < doc_.size() && y < scroll.raw() + viewport_h.raw(); ++i) {
        const BlockLayout& l = layout(i);
        draw_block(dl, i, l, pad + y - scroll.raw(), width, theme);
        y += l.height;
    }
    if (marks && marks->caret.block < doc_.size()) {
        const Rect r = caret_rect(marks->caret);
        if (r.origin.y.raw() + r.extent.h.raw() > 0.f && r.origin.y.raw() < size.h.raw())
            dl.filled_rect(Rect{r.origin, Size{Width{2.f}, r.extent.h}}, theme.cursor);
    }
    dl.clip_pop();
    clamp_scroll();   // the blocks just laid out replaced their estimates
}

void View::draw_block(render::DrawList& dl, std::size_t i, const BlockLayout& l, float top, float width,
                      const ui::Theme& theme) const {
    const Block& b = doc_[i];
    const float left = options_.padding;
    const float text_x = left + l.indent;
    if (l.background) dl.filled_rect(Rect{Point{X{left}, Y{top}}, Size{Width{width}, Height{l.height}}}, *l.background);
    if (l.quote_bar) dl.filled_rect(Rect{Point{X{left}, Y{top}}, Size{Width{3.f}, Height{l.height}}}, theme.divider);
    if (l.rule) dl.filled_rect(Rect{Point{X{left}, Y{top + l.height / 2.f}}, Size{Width{width}, Height{1.f}}}, theme.divider);
    if (!l.marker.empty()) {
        const float w = ui::text_width(style_.measurer, l.marker, style_.size, style_.family);
        dl.text(l.marker, Point{X{text_x - w - 0.4f * style_.size}, Y{top + l.lines.front().y}}, style_.size,
                theme.text_muted, 0.f, render::TextAnchor::TopLeft, render::FontStyle{style_.family});
    }
    draw_selection(dl, i, l, top, theme);
    for (const Line& line : l.lines) {
        const float line_top = top + line.y;
        for (const Run& r : line.runs) {
            const float run_h = ui::line_height(style_.measurer, r.look.size, r.look.font);
            const float run_top = line_top + (line.height - run_h);   // bottoms aligned: mixed sizes share a line
            const X x{text_x + r.x};
            if (r.look.background) dl.filled_rect(Rect{Point{x, Y{line_top}}, Size{Width{r.width}, Height{line.height}}}, *r.look.background);
            dl.text(b.text.substr(r.begin, r.end - r.begin), Point{x, Y{run_top}}, r.look.size, r.look.color, 0.f,
                    render::TextAnchor::TopLeft, r.look.font);
            if (r.look.underline) dl.filled_rect(Rect{Point{x, Y{run_top + 0.85f * run_h}}, Size{Width{r.width}, Height{1.f}}}, r.look.color);
            if (r.look.strike) dl.filled_rect(Rect{Point{x, Y{run_top + 0.5f * run_h}}, Size{Width{r.width}, Height{1.f}}}, r.look.color);
        }
    }
}

// The part of the selection on block i, line by line, behind the text. A selection that goes on
// past the block's end covers a space's width there: the newline it holds.
void View::draw_selection(render::DrawList& dl, std::size_t i, const BlockLayout& l, float top,
                          const ui::Theme& theme) const {
    if (!marks || marks->caret == marks->anchor) return;
    const Position from = std::min(marks->caret, marks->anchor), to = std::max(marks->caret, marks->anchor);
    if (i < from.block || i > to.block) return;
    const Block& b = doc_[i];
    const std::size_t a = i == from.block ? from.byte : 0;
    const std::size_t z = i == to.block ? to.byte : b.text.size();
    const float text_x = options_.padding + l.indent;
    const float newline = i < to.block ? 0.5f * style_.size : 0.f;
    for (std::size_t k = 0; k < l.lines.size(); ++k) {
        const Line& line = l.lines[k];
        const bool last = k + 1 == l.lines.size();
        const std::size_t la = std::max(a, line.begin), lz = std::min(z, line.end);
        if (la > lz || (la == lz && !(last && newline > 0.f && la == b.text.size()))) continue;
        const float x0 = text::x_of(b, line, la, style_), x1 = text::x_of(b, line, lz, style_) + (last ? newline : 0.f);
        dl.filled_rect(Rect{Point{X{text_x + x0}, Y{top + line.y}}, Size{Width{x1 - x0}, Height{line.height}}},
                       theme.text_selection);
    }
}

// The line holding `byte`: the last whose start is at or before it, so the caret at a wrap point
// sits at the start of the next line, where typing goes.
std::size_t View::line_of(const BlockLayout& l, std::size_t byte) {
    std::size_t k = 0;
    while (k + 1 < l.lines.size() && l.lines[k + 1].begin <= byte) ++k;
    return k;
}

Position View::hit(Point p) {
    const float pad = options_.padding;
    const float y = std::max(0.f, p.y.raw() - pad + scroll.raw());
    auto at = heights_.seek([](const Heights& h) { return h.height; }, y);
    if (at.index >= doc_.size()) return doc_.end();
    const BlockLayout& l = layout(at.index);
    const float local = y - block_top(at.index);
    std::size_t k = 0;
    while (k + 1 < l.lines.size() && l.lines[k + 1].y <= local) ++k;
    const float x = p.x.raw() - pad - l.indent;
    return {at.index, text::byte_at(doc_[at.index], l.lines[k], x, style_)};
}

float View::caret_x(Position p) {
    const BlockLayout& l = layout(p.block);
    return text::x_of(doc_[p.block], l.lines[line_of(l, p.byte)], p.byte, style_);
}

Rect View::caret_rect(Position p) {
    const BlockLayout& l = layout(p.block);
    const Line& line = l.lines[line_of(l, p.byte)];
    const float pad = options_.padding;
    const float x = pad + l.indent + text::x_of(doc_[p.block], line, p.byte, style_);
    const float y = pad + block_top(p.block) + line.y - scroll.raw();
    return Rect{Point{X{x}, Y{y}}, Size{Width{0.f}, Height{line.height}}};
}

Position View::step_lines(Position from, int lines, float x) {
    std::size_t block = from.block;
    std::size_t k = line_of(layout(block), from.byte);
    for (; lines > 0; --lines) {
        if (k + 1 < layout(block).lines.size()) ++k;
        else if (block + 1 < doc_.size()) { ++block; k = 0; }
        else return {block, doc_[block].text.size()};
    }
    for (; lines < 0; ++lines) {
        if (k > 0) --k;
        else if (block > 0) { --block; k = layout(block).lines.size() - 1; }
        else return {0, 0};
    }
    return {block, text::byte_at(doc_[block], layout(block).lines[k], x, style_)};
}

Range View::visual_line(Position p) {
    const BlockLayout& l = layout(p.block);
    const std::size_t k = line_of(l, p.byte);
    const Line& line = l.lines[k];
    std::size_t end = line.end;
    const std::string& text = doc_[p.block].text;
    if (k + 1 < l.lines.size() && end > line.begin && ui::is_wrap_space(text[end - 1])) --end;   // before the hanging space
    return {{p.block, line.begin}, {p.block, end}};
}

void View::reveal(Position p) {
    const BlockLayout& l = layout(p.block);
    const Line& line = l.lines[line_of(l, p.byte)];
    const float top = block_top(p.block) + line.y;
    if (top < scroll.raw()) scroll = DY{top};
    else if (top + line.height > scroll.raw() + viewport_h.raw()) scroll = DY{top + line.height - viewport_h.raw()};
}

} // namespace prism::text
