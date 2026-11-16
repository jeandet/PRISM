#include <prism/text/editor.hpp>

#include <prism/ui/text_segmentation.hpp>

#include <algorithm>
#include <cctype>
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
        view.marks = View::Marks{caret_, anchor_};
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

void Editor::show() {
    view.marks = View::Marks{caret_, anchor_};
    view.reveal(caret_);
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
    const std::string indent =
        options_.auto_indent ? std::string(leading_space(doc_[caret_.block].text.substr(0, caret_.byte))) : std::string{};
    doc_.break_undo_group();
    type("\n" + indent);
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
        case keys::c: return copy(clipboard);
        case keys::x: copy(clipboard); erase(selection()); return true;
        case keys::z:
            if (options_.read_only) return false;
            if (shift ? doc_.redo() : doc_.undo()) set_caret(caret_);
            return true;
        case keys::y:
            if (!options_.read_only && doc_.redo()) set_caret(caret_);
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
    if (auto* t = std::get_if<input::TextInput>(&ev)) { type(t->text); return !options_.read_only; }
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
