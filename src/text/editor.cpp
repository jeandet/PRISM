#include <prism/text/editor.hpp>

#include <prism/core/utf8.hpp>
#include <prism/ui/text_segmentation.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <variant>

namespace prism::text {

namespace keys = input::keys;
namespace mods = input::mods;

namespace {
bool is_word_byte(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || (c & 0x80); }
bool is_blank(std::string_view s) {
    return std::ranges::all_of(s, [](char c) { return c == ' ' || c == '\t' || c == '\n'; });
}
std::string_view leading_space(std::string_view s) {
    return s.substr(0, s.find_first_not_of(" \t") == std::string_view::npos ? s.size() : s.find_first_not_of(" \t"));
}
} // namespace

Editor::Editor(Document& doc, EditorOptions options) : view(doc, options.view), doc_(doc), options_(std::move(options)) {
    // An edit made elsewhere (a reload, an assign) may leave the caret past the end.
    follow_ = doc_.on_change().connect([this](const Change&) {
        caret_ = clamp(caret_);
        anchor_ = clamp(anchor_);
        view.marks = marks();
    });
    show();
}

Position Editor::clamp(Position p) const {
    if (p.block >= doc_.size()) return doc_.end();
    return {p.block, std::min(p.byte, doc_[p.block].text.size())};
}

Position Editor::next_char(Position p) const {
    const auto& t = doc_[p.block].text;
    if (p.byte < t.size()) return {p.block, ui::next_grapheme(t, p.byte)};
    return p.block + 1 < doc_.size() ? Position{p.block + 1, 0} : p;
}

Position Editor::prev_char(Position p) const {
    if (p.byte > 0) return {p.block, ui::prev_grapheme(doc_[p.block].text, p.byte)};
    return p.block > 0 ? Position{p.block - 1, doc_[p.block - 1].text.size()} : p;
}

// The end of the next word: spaces skipped, then the word.
Position Editor::next_word(Position p) const {
    const auto& t = doc_[p.block].text;
    if (p.byte >= t.size()) return next_char(p);
    std::size_t i = p.byte;
    while (i < t.size() && !is_word_byte(t[i])) ++i;
    while (i < t.size() && is_word_byte(t[i])) ++i;
    return {p.block, i};
}

// The start of the previous word: spaces skipped back, then the word.
Position Editor::prev_word(Position p) const {
    if (p.byte == 0) return prev_char(p);
    const auto& t = doc_[p.block].text;
    std::size_t i = p.byte;
    while (i > 0 && !is_word_byte(t[i - 1])) --i;
    while (i > 0 && is_word_byte(t[i - 1])) --i;
    return {p.block, i};
}

Range Editor::word_at(Position p) const {
    const auto& t = doc_[p.block].text;
    std::size_t a = p.byte, z = p.byte;
    while (a > 0 && is_word_byte(t[a - 1])) --a;
    while (z < t.size() && is_word_byte(t[z])) ++z;
    return {{p.block, a}, {p.block, z}};
}

View::Marks Editor::marks() const {
    return {caret_, anchor_, options_.match_brackets ? bracket_pair() : std::vector<Range>{}, preedit_, preedit_caret_};
}

void Editor::show() {
    view.marks = marks();
    view.reveal(caret_);
}

// The bracket at the caret (else just before it) and its match, counting nesting across blocks.
// simplify: gives up after a fixed number of blocks, as editors do on huge files.
std::vector<Range> Editor::bracket_pair() const {
    constexpr std::string_view open = "([{", close = ")]}";
    constexpr std::size_t max_blocks = 5000;
    for (const Position p : {caret_, prev_char(caret_)}) {
        const auto& t = doc_[p.block].text;
        if (p.byte >= t.size()) continue;
        const char c = t[p.byte];
        const auto o = open.find(c), cl = close.find(c);
        if (o == std::string_view::npos && cl == std::string_view::npos) continue;
        const bool forward = o != std::string_view::npos;
        const char mine = c, other = forward ? close[o] : open[cl];
        int depth = 0;
        Position q = p;
        for (std::size_t blocks = 0; blocks < max_blocks;) {
            const auto& s = doc_[q.block].text;
            if (q.byte < s.size()) {
                if (s[q.byte] == mine) ++depth;
                else if (s[q.byte] == other && --depth == 0)
                    return {{p, {p.block, p.byte + 1}}, {q, {q.block, q.byte + 1}}};
            }
            if (forward) {
                if (q.byte + 1 < s.size()) ++q.byte;
                else if (q.block + 1 < doc_.size()) { q = {q.block + 1, 0}; ++blocks; }
                else break;
            } else {
                if (q.byte > 0) --q.byte;
                else if (q.block > 0) { --q.block; q.byte = doc_[q.block].text.empty() ? 0 : doc_[q.block].text.size() - 1; ++blocks; }
                else break;
            }
        }
        // no match for this one: the bracket before the caret may have one
    }
    return {};
}

void Editor::set_caret(Position p, bool extend) {
    caret_ = clamp(p);
    if (!extend) anchor_ = caret_;
    column_x_ = -1.f;
    show();
}

void Editor::move(Position to, bool extend) {
    doc_.break_undo_group();
    set_caret(to, extend);
}

void Editor::select_all() {
    anchor_ = {0, 0};
    caret_ = doc_.end();
    show();
}

void Editor::type(std::string_view text) {
    if (options_.read_only || text.empty()) return;
    const Range sel = selection();
    caret_ = anchor_ = doc_.replace(sel, text);
    column_x_ = -1.f;
    if (is_blank(text)) doc_.break_undo_group();   // a word typed is one undo step
    if (options_.rich && text == " ") markdown_shortcut();
    show();
}

// The Markdown that starts a paragraph, typed: the block becomes what it says, the markup goes.
void Editor::markdown_shortcut() {
    const std::size_t i = caret_.block;
    if (doc_[i].kind != BlockKind::paragraph) return;
    const std::string_view head = std::string_view(doc_[i].text).substr(0, caret_.byte);
    BlockKind kind = BlockKind::paragraph;
    int level = 0;
    std::string info;
    const std::size_t hashes = head.find_first_not_of('#');
    const std::size_t digits = head.find_first_not_of("0123456789");
    if (hashes >= 1 && hashes <= 6 && head.substr(hashes) == " ") { kind = BlockKind::heading; level = static_cast<int>(hashes); }
    else if (head == "- " || head == "* ") { kind = BlockKind::bullet; info = "\xE2\x80\xA2"; }
    else if (digits >= 1 && head.substr(digits) == ". ") { kind = BlockKind::ordered; info = std::string(head.substr(0, digits + 1)); }
    else if (head == "> ") kind = BlockKind::quote;
    else return;
    doc_.break_undo_group();
    doc_.erase({{i, 0}, {i, head.size()}});
    doc_.set_kind(i, kind, level, info);
    caret_ = anchor_ = {i, 0};
}

// After Enter split a block: the new line under a heading is a paragraph, a numbered item counts
// on. Enter at a heading's start pushed it down instead: the empty line left above is a paragraph.
void Editor::continue_block(bool at_start) {
    const std::size_t i = caret_.block;
    if (i == 0) return;
    const Block& before = doc_[i - 1];
    if (at_start && before.kind == BlockKind::heading) doc_.set_kind(i - 1, BlockKind::paragraph);
    else if (before.kind == BlockKind::heading) doc_.set_kind(i, BlockKind::paragraph);
    else if (before.kind == BlockKind::ordered)
        doc_.set_kind(i, BlockKind::ordered, before.level, std::to_string(std::atoi(before.info.c_str()) + 1) + ".");
}

bool Editor::leave_block() {
    const Block& b = doc_[caret_.block];
    if (b.kind == BlockKind::paragraph || b.kind == BlockKind::code || caret_ != anchor_ || caret_.byte != 0) return false;
    doc_.break_undo_group();
    doc_.set_kind(caret_.block, BlockKind::paragraph);
    show();
    return true;
}

// The selection, or the word at the caret, gets style s; if all of it has s already, loses it.
void Editor::toggle(Style s) {
    if (options_.read_only) return;
    const Range r = caret_ != anchor_ ? selection() : word_at(caret_);
    if (r.empty()) return;
    bool all = true;
    for (std::size_t i = r.begin.block; i <= r.end.block; ++i) {
        const std::size_t a = i == r.begin.block ? r.begin.byte : 0;
        const std::size_t z = i == r.end.block ? r.end.byte : doc_[i].text.size();
        if (a >= z) continue;
        const auto& spans = doc_[i].spans;
        all = all && std::ranges::any_of(spans, [&](const Span& sp) { return sp.style == s && sp.begin <= a && sp.end >= z; });
    }
    doc_.break_undo_group();
    if (all) doc_.unstyle(r, s);
    else doc_.style(r, s);
    doc_.break_undo_group();
    show();
}

void Editor::erase(Range r) {
    if (options_.read_only || r.empty()) return;
    doc_.erase(r);
    caret_ = anchor_ = r.begin;
    column_x_ = -1.f;
    show();
}

void Editor::enter() {
    if (options_.rich && (doc_[caret_.block].kind == BlockKind::bullet || doc_[caret_.block].kind == BlockKind::ordered) &&
        doc_[caret_.block].text.empty()) {
        doc_.break_undo_group();
        doc_.set_kind(caret_.block, BlockKind::paragraph);   // Enter on an empty item leaves the list
        return show();
    }
    const std::string indent =
        options_.auto_indent ? std::string(leading_space(doc_[caret_.block].text.substr(0, caret_.byte))) : std::string{};
    const bool at_start = caret_.byte == 0 && caret_ == anchor_;
    doc_.break_undo_group();
    type("\n" + indent);
    if (options_.rich) continue_block(at_start);
}

// Tab: the indent typed at the caret. Shift+Tab: up to one indent's width of leading spaces gone.
void Editor::indent(bool more) {
    if (options_.read_only) return;
    if (more) return type(options_.indent);
    const auto& t = doc_[caret_.block].text;
    const std::size_t lead = leading_space(t).size();
    const std::size_t n = std::min(lead, options_.indent.size());
    if (n == 0) return;
    doc_.break_undo_group();
    doc_.erase({{caret_.block, 0}, {caret_.block, n}});
    caret_ = anchor_ = {caret_.block, caret_.byte > n ? caret_.byte - n : 0};
    show();
}

bool Editor::copy(const ui::Clipboard* clipboard) const {
    if (clipboard && caret_ != anchor_) clipboard->set_text(selected_text());
    return false;
}

bool Editor::key(const input::KeyPress& k, const ui::Clipboard* clipboard) {
    const bool shift = (k.mods & mods::shift) != 0;
    const bool word = (k.mods & mods::ctrl) != 0;
    const bool has_selection = caret_ != anchor_;
    if (mods::shortcut(k.mods)) {
        switch (k.key) {
        case keys::a: select_all(); return true;
        case keys::b: if (!options_.rich) break; toggle(Style::strong); return true;
        case keys::i: if (!options_.rich) break; toggle(Style::emphasis); return true;
        case keys::e: if (!options_.rich) break; toggle(Style::code); return true;
        case keys::c: return copy(clipboard);
        case keys::x: copy(clipboard); erase(selection()); return true;
        case keys::z:
            if (options_.read_only) return false;
            if (shift ? doc_.redo() : doc_.undo()) set_caret(doc_.replayed_at());
            return true;
        case keys::y:
            if (!options_.read_only && doc_.redo()) set_caret(doc_.replayed_at());
            return true;
        default: break;
        }
    }
    switch (k.key) {
    case keys::left:
        if (has_selection && !shift) move(selection().begin, false);
        else move(word ? prev_word(caret_) : prev_char(caret_), shift);
        return true;
    case keys::right:
        if (has_selection && !shift) move(selection().end, false);
        else move(word ? next_word(caret_) : next_char(caret_), shift);
        return true;
    case keys::up:
    case keys::down:
    case keys::page_up:
    case keys::page_down: {
        const float x = column_x_ >= 0.f ? column_x_ : view.caret_x(caret_);
        const int page = std::max(1, static_cast<int>(view.viewport_h.raw() / (1.4f * options_.view.size)));
        const int lines = k.key == keys::up ? -1 : k.key == keys::down ? 1 : k.key == keys::page_up ? -page : page;
        move(view.step_lines(caret_, lines, x), shift);
        column_x_ = x;
        return true;
    }
    case keys::home: move(word ? Position{0, 0} : view.visual_line(caret_).begin, shift); return true;
    case keys::end: move(word ? doc_.end() : view.visual_line(caret_).end, shift); return true;
    case keys::backspace:
        if (options_.rich && leave_block()) return true;
        if (has_selection) erase(selection());
        else erase({word ? prev_word(caret_) : prev_char(caret_), caret_});
        return true;
    case keys::delete_:
        if (has_selection) erase(selection());
        else erase({caret_, word ? next_word(caret_) : next_char(caret_)});
        return true;
    case keys::enter: if (options_.read_only) return false; enter(); return true;
    case keys::tab: if (!options_.tab_inserts) return false; indent(!shift); return true;
    case keys::escape: if (!has_selection) return false; move(caret_, false); return true;
    default: return false;
    }
}

bool Editor::mouse_button(const input::MouseButton& b) {
    if (b.button != input::buttons::left) return false;
    if (!b.pressed) { dragging_ = false; return false; }
    const Position at = view.hit(b.position);
    if (b.clicks == 2) {
        const Range w = word_at(at);
        anchor_ = w.begin;
        caret_ = w.end;
        show();
    } else if (b.clicks >= 3) {
        anchor_ = {at.block, 0};
        caret_ = {at.block, doc_[at.block].text.size()};
        show();
    } else {
        move(at, (b.mods & mods::shift) != 0);
        dragging_ = true;
    }
    return true;
}

bool Editor::handle(const input::InputEvent& ev, const ui::Clipboard* clipboard) {
    if (auto* k = std::get_if<input::KeyPress>(&ev)) return key(*k, clipboard);
    if (auto* t = std::get_if<input::TextInput>(&ev)) {
        preedit_.clear();
        type(t->text);
        show();
        return !options_.read_only;
    }
    if (auto* c = std::get_if<input::Composition>(&ev)) {
        if (options_.read_only) return false;
        // The input method's caret, used only when it lands between characters (platforms count
        // it in characters or in bytes); else after the composition, as TextArea does.
        preedit_ = core::utf8::sanitize(c->text);
        const auto start = static_cast<std::size_t>(c->start);
        preedit_caret_ = c->start >= 0 && start <= preedit_.size() && core::utf8::snap(preedit_, start) == start
                             ? start : preedit_.size();
        show();
        return true;
    }
    if (auto* p = std::get_if<input::Paste>(&ev)) {
        doc_.break_undo_group();
        type(p->text);
        doc_.break_undo_group();
        return !options_.read_only;
    }
    if (auto* b = std::get_if<input::MouseButton>(&ev)) return mouse_button(*b);
    if (auto* m = std::get_if<input::MouseMove>(&ev); m && dragging_ && (m->buttons & input::buttons::left_mask)) {
        caret_ = view.hit(m->position);
        show();
        return true;
    }
    return false;
}

} // namespace prism::text
