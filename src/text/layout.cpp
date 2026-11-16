#include <prism/text/layout.hpp>

#include <prism/ui/text_segmentation.hpp>

#include <algorithm>
#include <array>

namespace prism::text {

namespace {

const ui::Theme& theme_of(const LayoutStyle& st) {
    static const ui::Theme fallback{};
    return st.theme ? *st.theme : fallback;
}

bool prose(const LayoutStyle& st) { return st.family == render::FontRole::Ui; }

float heading_scale(int level) {
    constexpr std::array<float, 6> scale{2.f, 1.6f, 1.3f, 1.15f, 1.f, 0.9f};
    return scale[static_cast<std::size_t>(std::clamp(level, 1, 6) - 1)];
}

// The look of plain text in this block.
Look base_look(const Block& b, const LayoutStyle& st) {
    const auto& t = theme_of(st);
    Look l{.font = {st.family}, .size = st.size, .color = t.text};
    switch (b.kind) {
    case BlockKind::heading: l.font.bold = true; l.size = st.size * heading_scale(b.level); break;
    case BlockKind::quote: l.color = t.text_muted; l.font.italic = true; break;
    case BlockKind::code: l.font.family = render::FontRole::Mono; break;
    default: break;
    }
    return l;
}

void apply(Look& l, Style s, const ui::Theme& t) {
    switch (s) {
    case Style::strong: l.font.bold = true; break;
    case Style::emphasis: l.font.italic = true; break;
    case Style::code: l.font.family = render::FontRole::Mono; l.background = t.code_bg; break;
    case Style::strike: l.strike = true; break;
    case Style::link: l.color = t.link; l.underline = true; break;
    case Style::keyword: l.color = t.syntax_keyword; break;
    case Style::type: l.color = t.syntax_type; break;
    case Style::string: l.color = t.syntax_string; break;
    case Style::number: l.color = t.syntax_number; break;
    case Style::comment: l.color = t.syntax_comment; l.font.italic = true; break;
    case Style::preprocessor: l.color = t.syntax_preprocessor; break;
    case Style::function: l.color = t.syntax_function; break;
    case Style::match: l.background = t.text_match; break;
    case Style::current_match: l.background = t.text_selection; break;
    case Style::error: case Style::warning: l.underline = true; break;
    }
}

// The block cut where its styles change, each piece with its look.
struct Piece {
    std::size_t begin, end;
    Look look;
};

std::vector<Piece> pieces_of(const Block& b, std::span<const Span> computed, const LayoutStyle& st) {
    const std::size_t n = b.text.size();
    std::vector<std::size_t> cuts{0, n};
    for (auto spans : {std::span<const Span>(b.spans), computed})
        for (const auto& s : spans) { cuts.push_back(std::min(s.begin, n)); cuts.push_back(std::min(s.end, n)); }
    std::ranges::sort(cuts);
    cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
    const Look base = base_look(b, st);
    const auto& t = theme_of(st);
    std::vector<Piece> out;
    for (std::size_t k = 0; k + 1 < cuts.size(); ++k) {
        Piece p{cuts[k], cuts[k + 1], base};
        for (auto spans : {std::span<const Span>(b.spans), computed})
            for (const auto& s : spans)
                if (s.begin <= p.begin && s.end >= p.end) apply(p.look, s.style, t);
        out.push_back(p);
    }
    if (out.empty()) out.push_back({0, 0, base});
    return out;
}

float measure(const Block& b, const Piece& p, std::size_t from, std::size_t to, const LayoutStyle& st) {
    const std::size_t a = std::max(from, p.begin), z = std::min(to, p.end);
    if (a >= z) return 0.f;
    return ui::text_width(st.measurer, std::string_view(b.text).substr(a, z - a), p.look.size, p.look.font);
}

// The width of bytes [from, to): each piece in its own face.
float measure(const Block& b, const std::vector<Piece>& pieces, std::size_t from, std::size_t to, const LayoutStyle& st) {
    float w = 0.f;
    for (const auto& p : pieces) w += measure(b, p, from, to, st);
    return w;
}

std::size_t trim_spaces(const std::string& s, std::size_t from, std::size_t to) {
    while (to > from && ui::is_wrap_space(s[to - 1])) --to;
    return to;
}

// Greedy wrapping: each line takes the most words that fit; its spaces hang past the edge. A word
// longer than the whole line is cut between characters.
std::vector<std::pair<std::size_t, std::size_t>> wrap(const Block& b, const std::vector<Piece>& pieces,
                                                      float avail, const LayoutStyle& st) {
    const std::size_t n = b.text.size();
    if (!st.wrap || avail <= 0.f || n == 0) return {{0, n}};
    const auto can_start = ui::line_break_opportunities(b.text);
    std::vector<std::pair<std::size_t, std::size_t>> lines;
    std::size_t start = 0;
    while (start < n) {
        std::size_t fit = start;
        for (std::size_t q = start + 1; q <= n; ++q) {
            if (!can_start[q]) continue;
            if (measure(b, pieces, start, trim_spaces(b.text, start, q), st) > avail) break;
            fit = q;
        }
        if (fit == start) {   // one word wider than the line: as many characters as fit, at least one
            fit = ui::next_grapheme(b.text, start);
            for (std::size_t q = ui::next_grapheme(b.text, fit); q <= n && fit < n; q = ui::next_grapheme(b.text, q)) {
                if (measure(b, pieces, start, q, st) > avail) break;
                fit = q;
                if (q == n) break;
            }
        }
        lines.push_back({start, fit});
        start = fit;
    }
    return lines;
}

float line_height_of(const Look& l, const LayoutStyle& st) { return ui::line_height(st.measurer, l.size, l.font); }

Line place(const Block& b, const std::vector<Piece>& pieces, std::size_t from, std::size_t to, const LayoutStyle& st) {
    Line line{.begin = from, .end = to};
    float x = 0.f;
    for (const auto& p : pieces) {
        if (p.end <= from || p.begin >= to) {
            if (from == to && p.begin <= from && p.end >= to) line.height = std::max(line.height, line_height_of(p.look, st));
            continue;
        }
        Run r{std::max(from, p.begin), std::min(to, p.end), x, 0.f, p.look};
        r.width = measure(b, p, r.begin, r.end, st);
        x += r.width;
        line.height = std::max(line.height, line_height_of(p.look, st));
        line.runs.push_back(r);
    }
    if (line.height == 0.f) line.height = line_height_of(pieces.front().look, st);
    return line;
}

// The margin and marker a block's kind gives it, and the space above and below it.
void frame(BlockLayout& out, const Block& b, const LayoutStyle& st, float& above, float& below) {
    const auto& t = theme_of(st);
    const float em = st.size;
    above = below = 0.f;
    switch (b.kind) {
    case BlockKind::heading: above = 0.6f * em; below = 0.3f * em; break;
    case BlockKind::quote: out.indent = 1.2f * em; out.quote_bar = true; break;
    case BlockKind::bullet:
    case BlockKind::ordered: out.indent = 1.5f * em * static_cast<float>(b.level + 1); out.marker = b.info; break;
    case BlockKind::code: out.background = t.code_bg; out.indent = 0.6f * em; break;
    case BlockKind::rule: out.rule = true; above = below = 0.5f * em; break;
    case BlockKind::paragraph: if (prose(st)) below = 0.4f * em; break;
    }
}

} // namespace

BlockLayout layout_block(const Block& block, std::span<const Span> computed, const LayoutStyle& st) {
    BlockLayout out;
    float above = 0.f, below = 0.f;
    frame(out, block, st, above, below);
    const auto pieces = pieces_of(block, computed, st);
    float y = above;
    for (auto [from, to] : wrap(block, pieces, st.width - out.indent, st)) {
        Line line = place(block, pieces, from, to, st);
        line.y = y;
        y += line.height;
        out.lines.push_back(std::move(line));
    }
    out.height = y + below;
    return out;
}

float x_of(const Block& block, const Line& line, std::size_t byte, const LayoutStyle& st) {
    for (const auto& r : line.runs)
        if (byte < r.end || &r == &line.runs.back()) {
            const std::size_t at = std::clamp(byte, r.begin, r.end);
            return r.x + ui::text_width(st.measurer, std::string_view(block.text).substr(r.begin, at - r.begin),
                                        r.look.size, r.look.font);
        }
    return 0.f;
}

std::size_t byte_at(const Block& block, const Line& line, float x, const LayoutStyle& st) {
    std::size_t best = line.begin;
    float best_d = x_of(block, line, line.begin, st) - x;
    best_d = best_d < 0 ? -best_d : best_d;
    for (std::size_t at = line.begin; at < line.end;) {
        at = ui::next_grapheme(block.text, at);
        if (at > line.end) at = line.end;
        float d = x_of(block, line, at, st) - x;
        d = d < 0 ? -d : d;
        if (d < best_d) { best_d = d; best = at; }
        else if (d > best_d && x_of(block, line, at, st) > x) break;   // moving away from x
    }
    return best;
}

} // namespace prism::text
