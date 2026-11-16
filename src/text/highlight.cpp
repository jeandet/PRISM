#include <prism/text/highlight.hpp>

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>

namespace prism::text {

namespace {

enum : Highlighter::State { normal = 0, in_block_comment = 1, in_long_string = 2 };

bool word_start(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool word_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }
bool at(std::string_view line, std::size_t i, std::string_view token) {
    return !token.empty() && line.substr(i, token.size()) == token;
}

std::size_t skip_spaces(std::string_view line, std::size_t i) {
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    return i;
}

// The end of a one-line string opened at `i` (past its closing quote, or the line's end).
std::size_t string_end(std::string_view line, std::size_t i) {
    const char q = line[i];
    for (++i; i < line.size(); ++i) {
        if (line[i] == '\\') ++i;
        else if (line[i] == q) return i + 1;
    }
    return line.size();
}

std::size_t number_end(std::string_view line, std::size_t i) {
    while (i < line.size() && (word_char(line[i]) || line[i] == '.' || line[i] == '\'' ||
                                ((line[i] == '+' || line[i] == '-') && (line[i - 1] == 'e' || line[i - 1] == 'E'))))
        ++i;
    return i;
}

struct Scan {
    const Language& lang;
    std::string_view line;
    std::vector<Span>& out;

    void add(std::size_t a, std::size_t z, Style s) const {
        if (z > a) out.push_back({a, z, s});
    }

    // Closes what the line before left open; returns where the rest of the line starts, or npos
    // if the line ends still inside it.
    std::size_t close(Highlighter::State before) const {
        const std::string_view end = before == in_block_comment ? lang.block_close : lang.long_string;
        const Style s = before == in_block_comment ? Style::comment : Style::string;
        const std::size_t z = line.find(end);
        if (z == std::string_view::npos) { add(0, line.size(), s); return std::string_view::npos; }
        add(0, z + end.size(), s);
        return z + end.size();
    }

    std::optional<Style> word_style(std::string_view w, std::size_t after) const {
        std::string lower;
        std::string_view key = w;
        if (lang.case_insensitive) {
            lower.assign(w);
            std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            key = lower;
        }
        if (lang.keywords.contains(key)) return Style::keyword;
        if (lang.types.contains(key)) return Style::type;
        if (lang.calls && skip_spaces(line, after) < line.size() && line[skip_spaces(line, after)] == '(') return Style::function;
        return std::nullopt;   // a plain word
    }

    Highlighter::State rest(std::size_t i) const {
        const std::size_t first = skip_spaces(line, 0);
        if (lang.preprocessor && i <= first && first < line.size() && line[first] == '#') {
            add(first, line.size(), Style::preprocessor);
            return normal;
        }
        if (lang.sections && i <= first && first < line.size() && line[first] == '[') {
            const std::size_t z = line.find(']', first);
            add(first, z == std::string_view::npos ? line.size() : z + 1, Style::type);
            i = z == std::string_view::npos ? line.size() : z + 1;
        }
        bool line_start = true;
        while (i < line.size()) {
            if (at(line, i, lang.line_comment)) { add(i, line.size(), Style::comment); return normal; }
            if (at(line, i, lang.block_open)) {
                const std::size_t z = line.find(lang.block_close, i + lang.block_open.size());
                if (z == std::string_view::npos) { add(i, line.size(), Style::comment); return in_block_comment; }
                add(i, z + lang.block_close.size(), Style::comment);
                i = z + lang.block_close.size();
                continue;
            }
            if (at(line, i, lang.long_string)) {
                const std::size_t z = line.find(lang.long_string, i + lang.long_string.size());
                if (z == std::string_view::npos) { add(i, line.size(), Style::string); return in_long_string; }
                add(i, z + lang.long_string.size(), Style::string);
                i = z + lang.long_string.size();
                continue;
            }
            const char c = line[i];
            if (lang.quotes.find(c) != std::string_view::npos) {
                const std::size_t z = string_end(line, i);
                const bool key = lang.keys && skip_spaces(line, z) < line.size() && line[skip_spaces(line, z)] == ':';
                add(i, z, key ? Style::type : Style::string);
                i = z;
            } else if (std::isdigit(static_cast<unsigned char>(c)) && (i == 0 || !word_char(line[i - 1]))) {
                const std::size_t z = number_end(line, i);
                add(i, z, Style::number);
                i = z;
            } else if (word_start(c)) {
                std::size_t z = i;
                while (z < line.size() && word_char(line[z])) ++z;
                std::optional<Style> s = word_style(line.substr(i, z - i), z);
                if (lang.sections && line_start && skip_spaces(line, z) < line.size() && line[skip_spaces(line, z)] == '=')
                    s = Style::function;   // a key
                if (s) add(i, z, *s);
                i = z;
            } else {
                ++i;
            }
            if (c != ' ' && c != '\t') line_start = false;
        }
        return normal;
    }
};

Language make(std::string name, std::initializer_list<std::string_view> keywords,
              std::initializer_list<std::string_view> types) {
    Language l;
    l.name = std::move(name);
    l.keywords = keywords;
    l.types = types;
    return l;
}

Language cpp() {
    Language l = make("cpp",
        {"alignas", "alignof", "auto", "break", "case", "catch", "class", "concept", "const", "consteval",
         "constexpr", "constinit", "continue", "co_await", "co_return", "co_yield", "decltype", "default",
         "delete", "do", "else", "enum", "explicit", "export", "extern", "false", "final", "for", "friend",
         "goto", "if", "import", "inline", "module", "mutable", "namespace", "new", "noexcept", "nullptr",
         "operator", "override", "private", "protected", "public", "requires", "return", "sizeof", "static",
         "static_assert", "static_cast", "dynamic_cast", "reinterpret_cast", "const_cast", "struct", "switch",
         "template", "this", "throw", "true", "try", "typedef", "typename", "union", "using", "virtual",
         "volatile", "while"},
        {"bool", "char", "char8_t", "char16_t", "char32_t", "double", "float", "int", "long", "short",
         "signed", "unsigned", "void", "wchar_t", "size_t", "int8_t", "int16_t", "int32_t", "int64_t",
         "uint8_t", "uint16_t", "uint32_t", "uint64_t", "string", "string_view", "vector", "optional"});
    l.line_comment = "//";
    l.block_open = "/*";
    l.block_close = "*/";
    l.quotes = "\"'";
    l.preprocessor = true;
    return l;
}

Language python() {
    Language l = make("python",
        {"and", "as", "assert", "async", "await", "break", "class", "continue", "def", "del", "elif", "else",
         "except", "False", "finally", "for", "from", "global", "if", "import", "in", "is", "lambda", "None",
         "nonlocal", "not", "or", "pass", "raise", "return", "True", "try", "while", "with", "yield", "match",
         "case", "self"},
        {"int", "float", "str", "bytes", "bool", "list", "dict", "set", "tuple", "object", "type"});
    l.line_comment = "#";
    l.long_string = "\"\"\"";
    l.quotes = "\"'";
    return l;
}

Language sql() {
    Language l = make("sql",
        {"select", "from", "where", "and", "or", "not", "in", "is", "null", "as", "on", "join", "left", "right",
         "inner", "outer", "full", "cross", "group", "by", "order", "having", "limit", "offset", "union", "all",
         "distinct", "insert", "into", "values", "update", "set", "delete", "create", "table", "index", "view",
         "drop", "alter", "add", "primary", "key", "foreign", "references", "case", "when", "then", "else",
         "end", "with", "asc", "desc", "between", "like", "exists", "true", "false", "count", "sum", "avg",
         "min", "max", "cast"},
        {"integer", "int", "real", "text", "blob", "numeric", "varchar", "char", "date", "timestamp", "boolean"});
    l.case_insensitive = true;
    l.line_comment = "--";
    l.block_open = "/*";
    l.block_close = "*/";
    l.quotes = "'\"";
    l.calls = false;
    return l;
}

Language json() {
    Language l = make("json", {"true", "false", "null"}, {});
    l.quotes = "\"";
    l.keys = true;
    l.calls = false;
    return l;
}

Language toml() {
    Language l = make("toml", {"true", "false"}, {});
    l.line_comment = "#";
    l.long_string = "\"\"\"";
    l.quotes = "\"'";
    l.sections = true;
    l.calls = false;
    return l;
}

Language shell() {
    Language l = make("shell",
        {"if", "then", "else", "elif", "fi", "for", "while", "do", "done", "case", "esac", "in", "function",
         "return", "local", "export", "echo", "cd", "exit", "set"},
        {});
    l.line_comment = "#";
    l.quotes = "\"'";
    l.calls = false;
    return l;
}

} // namespace

Highlighter::State Language::highlight(std::string_view line, State before, std::vector<Span>& out) const {
    const Scan scan{*this, line, out};
    std::size_t i = 0;
    if (before != normal) {
        i = scan.close(before);
        if (i == std::string_view::npos) return before;
    }
    return scan.rest(i);
}

const Language& language(std::string_view name) {
    static const Language languages[] = {cpp(), python(), sql(), json(), toml(), shell(), Language{}};
    static const std::pair<std::string_view, std::size_t> aliases[] = {
        {"cpp", 0}, {"c++", 0}, {"cxx", 0}, {"hpp", 0}, {"c", 0}, {"h", 0},
        {"python", 1}, {"py", 1},
        {"sql", 2}, {"sqlite", 2},
        {"json", 3},
        {"toml", 4}, {"ini", 4},
        {"shell", 5}, {"sh", 5}, {"bash", 5}, {"zsh", 5},
    };
    for (const auto& [alias, index] : aliases)
        if (alias == name) return languages[index];
    return languages[std::size(languages) - 1];
}

} // namespace prism::text
