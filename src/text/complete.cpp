#include <prism/text/complete.hpp>

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace prism::text {

namespace {
bool is_word_byte(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || (c & 0x80); }
char fold(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
bool is_upper(char c) { return std::isupper(static_cast<unsigned char>(c)) != 0; }
bool is_lower(char c) { return std::islower(static_cast<unsigned char>(c)) != 0; }

// A word starts at j: after a separator (get_widget, a.b) or at a capital after a small letter
// (getWidget).
bool word_start(std::string_view s, std::size_t j) {
    return j > 0 && (!is_word_byte(s[j - 1]) || s[j - 1] == '_' || (is_lower(s[j - 1]) && is_upper(s[j])));
}

// A letter right after the last one outweighs one at a word start: "st" is std before size_t.
constexpr int matched = 1, at_start = 8, at_word_start = 6, consecutive = 8, same_case = 1;

// The document's blocks scanned for words, around the caret.
// simplify: a fixed window, so a keystroke in a huge file stays cheap; an index of the
// document's words kept up to date by its changes would see all of it.
constexpr std::size_t word_window = 2000;

void add_words(const Document& doc, Position at, std::unordered_set<std::string_view>& seen,
               std::vector<Completion>& out) {
    const std::size_t first = at.block > word_window ? at.block - word_window : 0;
    const std::size_t last = std::min(doc.size(), at.block + word_window + 1);
    for (std::size_t b = first; b < last; ++b) {
        const std::string_view t = doc[b].text;
        for (std::size_t i = 0; i < t.size();) {
            if (!is_word_byte(t[i])) { ++i; continue; }
            std::size_t z = i;
            while (z < t.size() && is_word_byte(t[z])) ++z;
            const bool typing = b == at.block && i <= at.byte && at.byte <= z;   // the word at the caret
            const auto word = t.substr(i, z - i);
            if (!typing && !std::isdigit(static_cast<unsigned char>(word[0])) && seen.insert(word).second)
                out.push_back({.label = std::string(word)});
            i = z;
        }
    }
}
} // namespace

std::optional<int> fuzzy_score(std::string_view typed, std::string_view label) {
    int score = 0;
    std::size_t j = 0;
    std::optional<std::size_t> previous;
    for (const char t : typed) {
        while (j < label.size() && fold(label[j]) != fold(t)) ++j;
        if (j == label.size()) return std::nullopt;
        score += matched + (j == 0 ? at_start : word_start(label, j) ? at_word_start : 0)
               + (previous && *previous + 1 == j ? consecutive : 0) + (label[j] == t ? same_case : 0);
        previous = j++;
    }
    return score;
}

std::vector<Completion> rank(std::vector<Completion> candidates, std::string_view typed, std::size_t limit) {
    struct Scored {
        int score;
        Completion c;
    };
    std::vector<Scored> kept;
    for (auto& c : candidates)
        if (auto s = fuzzy_score(typed, c.label); s && c.label != typed) kept.push_back({*s, std::move(c)});
    std::ranges::sort(kept, [](const Scored& a, const Scored& b) {
        if (a.score != b.score) return a.score > b.score;
        if (a.c.label.size() != b.c.label.size()) return a.c.label.size() < b.c.label.size();
        return a.c.label < b.c.label;
    });
    if (typed.empty()) std::ranges::sort(kept, {}, [](const Scored& s) -> const std::string& { return s.c.label; });
    std::vector<Completion> out;
    for (std::size_t i = 0; i < kept.size() && i < limit; ++i) out.push_back(std::move(kept[i].c));
    return out;
}

Completer from_list(std::vector<std::string> words) {
    std::vector<Completion> all;
    for (auto& w : words) all.push_back({.label = std::move(w)});
    return [all = std::move(all)](const Document&, Position) { return all; };
}

Completer document_words(const Language* language) {
    return [language](const Document& doc, Position at) {
        std::vector<Completion> out;
        std::unordered_set<std::string_view> seen;
        if (language) {
            for (auto k : language->keywords) if (seen.insert(k).second) out.push_back({std::string(k), {}, "keyword"});
            for (auto k : language->types) if (seen.insert(k).second) out.push_back({std::string(k), {}, "type"});
        }
        add_words(doc, at, seen, out);
        return out;
    };
}

} // namespace prism::text
