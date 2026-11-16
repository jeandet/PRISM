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
        draw_block(dl, doc_[i], l, pad + y - scroll.raw(), width, theme);
        y += l.height;
    }
    dl.clip_pop();
    clamp_scroll();   // the blocks just laid out replaced their estimates
}

void View::draw_block(render::DrawList& dl, const Block& b, const BlockLayout& l, float top, float width,
                      const ui::Theme& theme) const {
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

} // namespace prism::text
