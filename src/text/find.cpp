#include <prism/text/find.hpp>

#include <algorithm>
#include <cctype>
#include <functional>

namespace prism::text {

namespace {

char fold(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
bool word_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

bool whole_word_at(std::string_view text, std::size_t at, std::size_t n) {
    return (at == 0 || !word_char(text[at - 1])) && (at + n >= text.size() || !word_char(text[at + n]));
}

// Where `needle` occurs in `text`, with a searcher built once for the needle.
template <typename Searcher>
void matches_in(std::string_view text, std::size_t block, std::size_t n, const Searcher& searcher, bool whole_word,
                std::vector<Range>& out) {
    for (auto it = text.begin(); it != text.end();) {
        const auto [from, to] = searcher(it, text.end());
        if (from == text.end()) break;
        const auto at = static_cast<std::size_t>(from - text.begin());
        if (!whole_word || whole_word_at(text, at, n)) out.push_back({{block, at}, {block, at + n}});
        it = from + 1;
    }
}

} // namespace

std::vector<Range> find_all(const Document& doc, std::string_view needle, FindOptions options) {
    std::vector<Range> out;
    if (needle.empty() || needle.find('\n') != std::string_view::npos) return out;
    const auto each_block = [&](const auto& searcher) {
        for (std::size_t i = 0; i < doc.size(); ++i)
            matches_in(doc[i].text, i, needle.size(), searcher, options.whole_word, out);
    };
    if (options.case_sensitive) {
        each_block(std::boyer_moore_horspool_searcher(needle.begin(), needle.end()));
    } else {
        const auto hash = [](char c) { return std::hash<char>{}(fold(c)); };
        const auto same = [](char a, char b) { return fold(a) == fold(b); };
        each_block(std::boyer_moore_horspool_searcher(needle.begin(), needle.end(), hash, same));
    }
    return out;
}

std::optional<Range> find_next(const Document& doc, std::string_view needle, Position from, FindOptions options) {
    const auto all = find_all(doc, needle, options);
    if (all.empty()) return std::nullopt;
    const auto next = std::ranges::find_if(all, [&](const Range& r) { return r.begin >= from; });
    return next != all.end() ? *next : all.front();
}

std::size_t replace_all(Document& doc, std::string_view needle, std::string_view with, FindOptions options) {
    const auto all = find_all(doc, needle, options);
    doc.replace_each(all, with);
    return all.size();
}

} // namespace prism::text
