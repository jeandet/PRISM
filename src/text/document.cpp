#include <prism/text/document.hpp>

#include <algorithm>
#include <iterator>
#include <utility>

namespace prism::text {

namespace {

std::vector<std::string_view> split_lines(std::string_view text) {
    std::vector<std::string_view> lines;
    std::size_t start = 0;
    for (std::size_t nl; (nl = text.find('\n', start)) != std::string_view::npos; start = nl + 1)
        lines.push_back(text.substr(start, nl - start));
    lines.push_back(text.substr(start));
    return lines;
}

// `n` bytes inserted at `at`: spans after it move, a span around it grows, and a span ending
// right there grows only if its style does.
void spans_after_insert(std::vector<Span>& spans, std::size_t at, std::size_t n) {
    for (auto& s : spans) {
        if (s.begin >= at) { s.begin += n; s.end += n; }
        else if (s.end > at || (s.end == at && grows_at_end(s.style))) s.end += n;
    }
}

// Bytes [a, b) erased: a span loses what was inside, and one left empty goes.
void spans_after_erase(std::vector<Span>& spans, std::size_t a, std::size_t b) {
    auto map = [&](std::size_t x) { return x <= a ? x : x >= b ? x - (b - a) : a; };
    for (auto& s : spans) { s.begin = map(s.begin); s.end = map(s.end); }
    std::erase_if(spans, [](const Span& s) { return s.begin >= s.end; });
}

// The spans of a block cut at `at`: the part before it, and the part after it moved to start at 0.
std::vector<Span> spans_before(const std::vector<Span>& spans, std::size_t at) {
    std::vector<Span> out;
    for (auto s : spans)
        if (s.begin < at) { s.end = std::min(s.end, at); out.push_back(std::move(s)); }
    return out;
}
std::vector<Span> spans_from(const std::vector<Span>& spans, std::size_t at, std::size_t new_start) {
    std::vector<Span> out;
    for (auto s : spans)
        if (s.end > at) {
            s.begin = std::max(s.begin, at) - at + new_start;
            s.end = s.end - at + new_start;
            out.push_back(std::move(s));
        }
    return out;
}

// Sorted, and spans of one style and target that touch or overlap made one.
void normalize(std::vector<Span>& spans) {
    std::ranges::sort(spans, [](const Span& x, const Span& y) {
        return std::tie(x.style, x.target, x.begin) < std::tie(y.style, y.target, y.begin);
    });
    std::vector<Span> out;
    for (auto& s : spans) {
        if (!out.empty() && out.back().style == s.style && out.back().target == s.target && s.begin <= out.back().end)
            out.back().end = std::max(out.back().end, s.end);
        else
            out.push_back(std::move(s));
    }
    std::ranges::sort(out, [](const Span& x, const Span& y) { return std::tie(x.begin, x.end) < std::tie(y.begin, y.end); });
    spans = std::move(out);
}

// The bytes of block `i` a range covers.
std::pair<std::size_t, std::size_t> bytes_in(const Range& r, std::size_t i, std::size_t length) {
    return {i == r.begin.block ? r.begin.byte : 0, i == r.end.block ? r.end.byte : length};
}

Block like(const Block& b, std::string text) {
    Block out;
    out.text = std::move(text);
    out.kind = b.kind;
    out.level = b.level;
    out.info = b.info;
    return out;
}

} // namespace

Document::Document(std::string_view text) {
    std::vector<Block> blocks;
    for (auto line : split_lines(text)) blocks.push_back(Block{.text = std::string(line), .version = next_version_++});
    blocks_.insert(0, std::make_move_iterator(blocks.begin()), std::make_move_iterator(blocks.end()));
}

Document::Document(std::vector<Block> blocks) {
    if (blocks.empty()) blocks.emplace_back();
    for (auto& b : blocks) b.version = next_version_++;
    blocks_.insert(0, std::make_move_iterator(blocks.begin()), std::make_move_iterator(blocks.end()));
}

Position Document::position_at(std::size_t offset) const {
    auto at = blocks_.seek([](std::size_t s) { return s; }, offset);
    if (at.index >= size()) return end();
    return {at.index, offset - at.before};
}

std::string Document::text(Range r) const {
    std::string out;
    for (std::size_t i = r.begin.block; i <= r.end.block && i < size(); ++i) {
        const auto& t = blocks_[i].text;
        auto [a, b] = bytes_in(r, i, t.size());
        out.append(t, a, b - a);
        if (i != r.end.block) out += '\n';
    }
    return out;
}

Position Document::insert(Position at, std::string_view text) {
    const Block& b = blocks_[at.block];
    const auto lines = split_lines(text);
    if (lines.size() == 1) {
        Block one = b;
        one.text.insert(at.byte, text);
        spans_after_insert(one.spans, at.byte, text.size());
        carets(at, {at.block, at.byte + text.size()});
        splice(at.block, 1, {std::move(one)});
        return {at.block, at.byte + text.size()};
    }
    std::vector<Block> out;
    Block head = like(b, b.text.substr(0, at.byte) + std::string(lines.front()));
    head.spans = spans_before(b.spans, at.byte);
    out.push_back(std::move(head));
    for (std::size_t k = 1; k + 1 < lines.size(); ++k) out.push_back(like(b, std::string(lines[k])));
    Block tail = like(b, std::string(lines.back()) + b.text.substr(at.byte));
    tail.spans = spans_from(b.spans, at.byte, lines.back().size());
    out.push_back(std::move(tail));
    const Position end_at{at.block + lines.size() - 1, lines.back().size()};
    carets(at, end_at);
    splice(at.block, 1, std::move(out));
    return end_at;
}

void Document::erase(Range r) {
    if (r.empty()) return;
    carets(r.end, r.begin);
    const Block& first = blocks_[r.begin.block];
    if (r.begin.block == r.end.block) {
        Block one = first;
        one.text.erase(r.begin.byte, r.end.byte - r.begin.byte);
        spans_after_erase(one.spans, r.begin.byte, r.end.byte);
        splice(r.begin.block, 1, {std::move(one)});
        return;
    }
    const Block& last = blocks_[r.end.block];
    Block joined = like(first, first.text.substr(0, r.begin.byte) + last.text.substr(r.end.byte));
    joined.spans = spans_before(first.spans, r.begin.byte);
    auto rest = spans_from(last.spans, r.end.byte, r.begin.byte);
    joined.spans.insert(joined.spans.end(), rest.begin(), rest.end());
    normalize(joined.spans);
    splice(r.begin.block, r.end.block - r.begin.block + 1, {std::move(joined)});
}

// The new text takes the styles of the text it replaces, where that starts (a word retyped in
// bold stays bold), as editors do.
Position Document::replace(Range r, std::string_view text) {
    std::vector<Span> kept;
    if (!r.empty())
        for (const Span& s : blocks_[r.begin.block].spans)
            if (s.begin <= r.begin.byte && r.begin.byte < s.end) kept.push_back(s);
    erase(r);
    const Position end = insert(r.begin, text);
    for (const Span& s : kept)
        if (!text.empty()) style({r.begin, end}, s.style, s.target);
    return end;
}

void Document::replace_each(const std::vector<Range>& ranges, std::string_view with) {
    if (ranges.empty()) return;
    const std::size_t first = ranges.front().begin.block, last = ranges.back().begin.block;
    auto blocks = take(first, last - first + 1);
    for (auto r = ranges.rbegin(); r != ranges.rend(); ++r) {   // from the end: earlier offsets stay put
        Block& b = blocks[r->begin.block - first];
        std::vector<Span> kept;   // styles where the match starts: the replacement takes them
        for (const Span& s : b.spans)
            if (s.begin <= r->begin.byte && r->begin.byte < s.end) kept.push_back(s);
        b.text.replace(r->begin.byte, r->end.byte - r->begin.byte, with);
        spans_after_erase(b.spans, r->begin.byte, r->end.byte);
        spans_after_insert(b.spans, r->begin.byte, with.size());
        for (Span s : kept) {
            s.begin = r->begin.byte;
            s.end = r->begin.byte + with.size();
            if (s.end > s.begin) b.spans.push_back(std::move(s));
        }
        normalize(b.spans);
    }
    const Range& end = ranges.back();
    std::size_t shift = 0;   // the last range's end moved by the replacements before it in its block
    for (const Range& r : ranges)
        if (r.begin.block == end.begin.block && r.begin.byte < end.begin.byte) shift += with.size() - (r.end.byte - r.begin.byte);
    break_undo_group();
    carets(ranges.front().begin, {end.begin.block, end.begin.byte + shift + with.size()});
    rewrite(first, std::move(blocks));
    break_undo_group();
}

void Document::style(Range r, Style s, std::string target) {
    carets(r.end, r.end);
    auto blocks = take(r.begin.block, r.end.block - r.begin.block + 1);
    for (std::size_t k = 0; k < blocks.size(); ++k) {
        auto [a, b] = bytes_in(r, r.begin.block + k, blocks[k].text.size());
        if (a >= b) continue;
        blocks[k].spans.push_back({a, b, s, target});
        normalize(blocks[k].spans);
    }
    rewrite(r.begin.block, std::move(blocks));
}

void Document::unstyle(Range r, Style s) {
    carets(r.end, r.end);
    auto blocks = take(r.begin.block, r.end.block - r.begin.block + 1);
    for (std::size_t k = 0; k < blocks.size(); ++k) {
        auto [a, b] = bytes_in(r, r.begin.block + k, blocks[k].text.size());
        std::vector<Span> kept;
        for (auto& sp : blocks[k].spans) {
            if (sp.style != s || sp.end <= a || sp.begin >= b) { kept.push_back(sp); continue; }
            if (sp.begin < a) kept.push_back({sp.begin, a, sp.style, sp.target});
            if (sp.end > b) kept.push_back({b, sp.end, sp.style, sp.target});
        }
        blocks[k].spans = std::move(kept);
        normalize(blocks[k].spans);
    }
    rewrite(r.begin.block, std::move(blocks));
}

void Document::set_kind(std::size_t block, BlockKind kind, int level, std::string info) {
    carets({block, 0}, {block, 0});
    auto blocks = take(block, 1);
    blocks[0].kind = kind;
    blocks[0].level = level;
    blocks[0].info = std::move(info);
    rewrite(block, std::move(blocks));
}

void Document::reset(Document other) {
    assign(std::move(other));
    undo_.clear();
    redo_.clear();
}

void Document::assign(Document other) {
    break_undo_group();
    carets({0, 0}, {0, 0});
    splice(0, size(), other.take(0, other.size()));
    break_undo_group();
}

std::vector<Block> Document::take(std::size_t first, std::size_t count) const {
    std::vector<Block> out;
    out.reserve(count);
    for (std::size_t i = first; i < first + count; ++i) out.push_back(blocks_[i]);
    return out;
}

// Blocks [first, first + n) replaced by n new ones: a style, a kind, replacements within blocks.
void Document::rewrite(std::size_t first, std::vector<Block> blocks) {
    const std::size_t n = blocks.size();   // first: argument order is unspecified, and the vector moves
    splice(first, n, std::move(blocks));
}

void Document::splice(std::size_t first, std::size_t removed, std::vector<Block> blocks) {
    auto before = take(first, removed);
    const std::size_t inserted = blocks.size();
    for (auto& b : blocks) b.version = next_version_++;
    blocks_.erase(first, first + removed);
    blocks_.insert(first, std::make_move_iterator(blocks.begin()), std::make_move_iterator(blocks.end()));
    record(first, std::move(before), inserted);
    redo_.clear();
    changed_.emit(Change{first, removed, inserted});
}

// An edit inside the blocks the last step produced joins that step: typing is one undo.
void Document::record(std::size_t first, std::vector<Block> before, std::size_t after) {
    const std::size_t removed = before.size();
    if (grouping_ && !undo_.empty()) {
        Step& last = undo_.back();
        if (first >= last.first && first + removed <= last.first + last.after) {
            last.after = last.after - removed + after;
            last.redo_caret = redo_caret_;
            return;
        }
    }
    undo_.push_back({first, std::move(before), after, undo_caret_, redo_caret_});
    grouping_ = true;
}


// Undo and redo are one move: put a step's blocks back, and keep what they replaced as the step
// that goes the other way.
bool Document::replay(std::vector<Step>& from, std::vector<Step>& to, bool undoing) {
    if (from.empty()) return false;
    Step step = std::move(from.back());
    from.pop_back();
    auto now = take(step.first, step.after);
    const std::size_t restored = step.before.size();
    for (auto& b : step.before) b.version = next_version_++;
    blocks_.erase(step.first, step.first + step.after);
    blocks_.insert(step.first, std::make_move_iterator(step.before.begin()), std::make_move_iterator(step.before.end()));
    replayed_at_ = undoing ? step.undo_caret : step.redo_caret;
    to.push_back({step.first, std::move(now), restored, step.undo_caret, step.redo_caret});
    grouping_ = false;
    changed_.emit(Change{step.first, step.after, restored});
    return true;
}

bool Document::undo() { return replay(undo_, redo_, true); }
bool Document::redo() { return replay(redo_, undo_, false); }

} // namespace prism::text
