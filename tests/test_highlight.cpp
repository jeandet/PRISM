// text::Highlighter and the built-in languages: a line's spans from the state the line before
// ended in (a comment or a string still open), as VS Code's tokenizer does.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/text/highlight.hpp>

#include <string>
#include <vector>

using namespace prism::text;

namespace {
struct Result {
    std::vector<Span> spans;
    Highlighter::State after;
};
Result run(const Highlighter& h, std::string_view line, Highlighter::State before = 0) {
    Result r;
    r.after = h.highlight(line, before, r.spans);
    return r;
}
// The style of the text at `byte`, or nullopt if none covers it.
std::optional<Style> at(const Result& r, std::size_t byte) {
    for (const auto& s : r.spans)
        if (s.begin <= byte && byte < s.end) return s.style;
    return std::nullopt;
}
} // namespace

TEST_CASE("C++: keywords, types, strings, numbers, comments, preprocessor, calls") {
    const auto& cpp = language("cpp");
    auto r = run(cpp, "if (x) return std::max(42, 0x1F); // done");
    CHECK(at(r, 0) == Style::keyword);          // if
    CHECK(at(r, 4) == std::nullopt);            // x
    CHECK(at(r, 7) == Style::keyword);          // return
    CHECK(at(r, 19) == Style::function);        // max(
    CHECK(at(r, 23) == Style::number);          // 42
    CHECK(at(r, 27) == Style::number);          // 0x1F
    CHECK(at(r, 34) == Style::comment);
    CHECK(at(run(cpp, "int n = 0;"), 0) == Style::type);
    CHECK(at(run(cpp, R"(auto s = "a \" b";)"), 12) == Style::string);   // an escaped quote stays inside
    CHECK(at(run(cpp, "#include <vector>"), 3) == Style::preprocessor);
    CHECK(at(run(cpp, "    #pragma once"), 6) == Style::preprocessor);
}

TEST_CASE("a block comment carries over to the next lines, and ends where it closes") {
    const auto& cpp = language("cpp");
    auto open = run(cpp, "int x; /* starts");
    CHECK(at(open, 10) == Style::comment);
    CHECK(open.after != 0);
    auto inside = run(cpp, "still comment", open.after);
    CHECK(at(inside, 3) == Style::comment);
    CHECK(inside.after == open.after);
    auto close = run(cpp, "end */ int y;", inside.after);
    CHECK(at(close, 1) == Style::comment);
    CHECK(at(close, 7) == Style::type);
    CHECK(close.after == 0);
}

TEST_CASE("Python: # comments, triple-quoted strings over several lines, def and call") {
    const auto& py = language("python");
    auto r = run(py, "def f(x):  # note");
    CHECK(at(r, 0) == Style::keyword);
    CHECK(at(r, 4) == Style::function);
    CHECK(at(r, 12) == Style::comment);
    auto doc = run(py, R"(s = """first)");
    CHECK(at(doc, 6) == Style::string);
    auto more = run(py, R"(last""" + 1)", doc.after);
    CHECK(at(more, 0) == Style::string);
    CHECK(at(more, 10) == Style::number);
    CHECK(more.after == 0);
}

TEST_CASE("SQL keywords are found in any case; -- starts a comment") {
    const auto& sql = language("sql");
    auto r = run(sql, "select count(*) FROM trips -- all");
    CHECK(at(r, 0) == Style::keyword);
    CHECK(at(r, 16) == Style::keyword);
    CHECK(at(r, 28) == Style::comment);
}

TEST_CASE("JSON: keys, strings, numbers and literals; TOML: sections, keys, comments") {
    const auto& json = language("json");
    auto j = run(json, R"(  "name": "prism", "n": -1.5e3, "ok": true)");
    CHECK(at(j, 3) == Style::type);       // a key
    CHECK(at(j, 12) == Style::string);    // a value
    CHECK(at(j, 25) == Style::number);
    CHECK(at(j, 39) == Style::keyword);
    const auto& toml = language("toml");
    CHECK(at(run(toml, "[window]"), 2) == Style::type);
    auto kv = run(toml, R"(width = 800  # px)");
    CHECK(at(kv, 1) == Style::function);
    CHECK(at(kv, 9) == Style::number);
    CHECK(at(kv, 14) == Style::comment);
}

TEST_CASE("an unknown language highlights nothing") {
    CHECK(run(language("klingon"), "int x; // y").spans.empty());
    CHECK(language("c++").name == "cpp");     // common aliases
    CHECK(language("py").name == "python");
}
