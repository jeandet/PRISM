#pragma once

#include <prism/text/document.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

// Syntax highlighting (doc/design/text-stack.md): a line's spans from the state the line before
// ended in, a comment or a string still open. After an edit a view highlights on down only until
// a line ends in the state it ended in before (VS Code's TextMate tokenizer does the same,
// https://code.visualstudio.com/blogs/2017/02/08/syntax-highlighting-optimizations): typing
// usually costs one line.
//
//   vb.code_editor(doc, {.language = &text::language("cpp")});
//
// A Highlighter is the one thing to implement for another engine (tree-sitter, a language
// server's semantic tokens); the built-in languages are rules, written as data below.
namespace prism::text {

struct Highlighter {
    using State = std::uint32_t;   // 0: nothing open
    virtual ~Highlighter() = default;
    // Appends `line`'s spans to `out` and returns the state the line ends in.
    virtual State highlight(std::string_view line, State before, std::vector<Span>& out) const = 0;
};

// A language as rules: what its words, comments and strings look like.
// simplify: rules, not a grammar: a type is a word from the list, not what the code declares, and
// a string's escapes are only skipped. tree-sitter fits the same interface when that is not enough.
struct Language final : Highlighter {
    std::string name;
    std::unordered_set<std::string_view> keywords, types;
    bool case_insensitive = false;          // SQL: keywords in any case (the lists are lowercase)
    std::string_view line_comment;          // "//", "#", "--"
    std::string_view block_open, block_close;   // "/*", "*/"
    std::string_view quotes;                // characters that open a one-line string
    std::string_view long_string;           // a string that may span lines: Python's """
    bool preprocessor = false;              // a line starting with # (C, C++)
    bool calls = true;                      // a word followed by ( is a function
    bool keys = false;                      // JSON: a string followed by : is a key (a type's color)
    bool sections = false;                  // TOML/INI: [a section]; a key before =

    State highlight(std::string_view line, State before, std::vector<Span>& out) const override;
};

// A built-in language by name or a common alias ("cpp", "c++", "c", "python", "py", "sql",
// "json", "toml", "markdown"...); one with no rules for a name it does not know.
[[nodiscard]] const Language& language(std::string_view name);

} // namespace prism::text
