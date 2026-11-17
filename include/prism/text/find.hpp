#pragma once

#include <prism/text/document.hpp>

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

// Finding and replacing in a document (doc/design/text-stack.md): Boyer-Moore-Horspool
// (std::boyer_moore_horspool_searcher) block by block, so a match never spans a newline.
//
//   matches.set(text::find_all(doc, query));          // shown with .highlights(matches)
//   text::replace_all(doc, "old", "new");             // one undo step
//
// simplify: ignoring case folds ASCII letters only, and a needle holding a newline finds nothing.
// Unicode case folding and multi-line needles when a search needs them.
namespace prism::text {

struct FindOptions {
    bool case_sensitive = true;
    bool whole_word = false;   // not inside a longer word (letters, digits, _)
};

[[nodiscard]] std::vector<Range> find_all(const Document& doc, std::string_view needle, FindOptions options = {});

// The first match starting at or after `from`, else the first from the top (it wraps around).
[[nodiscard]] std::optional<Range> find_next(const Document& doc, std::string_view needle, Position from,
                                             FindOptions options = {});

// Every match replaced, as one undo step. Returns how many.
std::size_t replace_all(Document& doc, std::string_view needle, std::string_view with, FindOptions options = {});

} // namespace prism::text
