#pragma once

#include <prism/text/document.hpp>
#include <prism/text/highlight.hpp>

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Completion (doc/design/text-stack.md): a source lists what could go at the caret; the editor
// finds the word being typed, ranks the candidates against it, shows the best in a list under the
// caret, and puts the one taken in place of the word. A source never filters, so every editor
// matches the same way.
//
//   vb.code_editor(doc, "cpp");                          // keywords and the file's words
//   vb.code_editor(query, "sql").complete(table_names);  // a list of strings
//   vb.text_editor(doc).complete([&](const text::Document& d, text::Position at) { ... });
namespace prism::text {

struct Completion {
    std::string label;      // what the list shows and the typing is matched against
    std::string insert{};   // what replaces the word; empty: the label
    std::string detail{};   // shown muted on the right: "keyword", a type, a signature
    bool operator==(const Completion&) const = default;
};

using Completer = std::function<std::vector<Completion>(const Document&, Position)>;

// How well `typed` matches `label`, ignoring case; nothing when its characters are not all in
// `label`, in order. A match at the start beats one at a later word's start ("gw": get_widget),
// which beats one inside a word; consecutive characters and the same case count a little more.
// simplify: greedy, left to right: "ab" in "xa_ab" takes the first a, so scores low. fzf's
// dynamic programming finds the best alignment when that matters.
[[nodiscard]] std::optional<int> fuzzy_score(std::string_view typed, std::string_view label);

// The candidates matching `typed`, best first, at most `limit`; the word already typed whole is
// left out (nothing to complete). With nothing typed: all of them, in alphabetical order.
[[nodiscard]] std::vector<Completion> rank(std::vector<Completion> candidates, std::string_view typed,
                                           std::size_t limit = 50);

// Always these.
[[nodiscard]] Completer from_list(std::vector<std::string> words);
// The words of the document near the caret, and a language's keywords and types, each once.
[[nodiscard]] Completer document_words(const Language* language = nullptr);

} // namespace prism::text
