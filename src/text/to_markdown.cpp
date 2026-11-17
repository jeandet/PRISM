#include <prism/text/to_markdown.hpp>

#include <algorithm>
#include <string_view>
#include <vector>

namespace prism::text {

namespace {

std::size_t longest_ticks(std::string_view s);

// A code span's delimiter: one backtick more than any run inside it, padded with a space when it
// holds a backtick (CommonMark strips one space on each side).
std::string code_fence(std::string_view code, bool opens) {
    const std::size_t n = longest_ticks(code) + 1;
    const std::string ticks(n, '`');
    if (n == 1) return ticks;
    return opens ? ticks + " " : " " + ticks;
}

std::string_view marker(Style s) {
    switch (s) {
    case Style::strong: return "**";
    case Style::emphasis: return "*";
    case Style::code: return "`";
    case Style::strike: return "~~";
    default: return "";
    }
}

bool is_list(BlockKind k) { return k == BlockKind::bullet || k == BlockKind::ordered; }

// A span opening or closing at a byte.
struct Edge {
    std::size_t at;
    bool opens;
    const Span* span;
};

// The block's text with its spans as Markdown markup, closing before opening at each byte, so
// spans that nest come out nested.
std::string inline_text(const Block& b) {
    std::vector<Edge> edges;
    for (const auto& s : b.spans) {
        if (s.style != Style::link && marker(s.style).empty()) continue;
        edges.push_back({s.begin, true, &s});
        edges.push_back({s.end, false, &s});
    }
    // At one byte: closes before opens; the outer span opens first (it ends later) and closes last
    // (it began earlier); spans over the same words close in the reverse of their opening order.
    std::ranges::sort(edges, [](const Edge& x, const Edge& y) {
        if (x.at != y.at) return x.at < y.at;
        if (x.opens != y.opens) return !x.opens;
        const auto rank = [](const Edge& e) { return static_cast<int>(e.span->style); };
        if (x.opens) return x.span->end != y.span->end ? x.span->end > y.span->end : rank(x) < rank(y);
        return x.span->begin != y.span->begin ? x.span->begin > y.span->begin : rank(x) > rank(y);
    });
    std::string out;
    std::size_t next = 0;
    int in_code = 0;
    auto text_until = [&](std::size_t to) {
        for (; next < to; ++next) {
            const char c = b.text[next];
            if (!in_code && std::string_view("\\`*_[]~").find(c) != std::string_view::npos) out += '\\';
            out += c;
        }
    };
    for (const Edge& e : edges) {
        text_until(e.at);
        if (e.span->style == Style::link) out += e.opens ? "[" : "](" + e.span->target + ")";
        else if (e.span->style == Style::code) out += code_fence(b.text.substr(e.span->begin, e.span->end - e.span->begin), e.opens);
        else out += marker(e.span->style);
        if (e.span->style == Style::code) in_code += e.opens ? 1 : -1;
    }
    text_until(b.text.size());
    return out;
}

// A paragraph's text that Markdown would read as the start of a heading, a list item or a quote,
// escaped so it stays text: "\# ", "\- ", "\> ", "1\. ".
std::string escape_block_start(std::string text) {
    const std::size_t digits = text.find_first_not_of("0123456789");
    if (!text.empty() && std::string_view("#->+").find(text[0]) != std::string_view::npos) return "\\" + text;
    if (digits > 0 && digits != std::string::npos && (text[digits] == '.' || text[digits] == ')'))
        return text.insert(digits, "\\");
    return text;
}

// The longest run of backticks in `s`.
std::size_t longest_ticks(std::string_view s) {
    std::size_t best = 0, run = 0;
    for (char c : s) { run = c == '`' ? run + 1 : 0; best = std::max(best, run); }
    return best;
}

std::string list_marker(const Block& b) {
    if (b.info == "\xE2\x96\xA0") return "- [x]";
    if (b.info == "\xE2\x96\xA1") return "- [ ]";
    if (b.kind == BlockKind::ordered) return b.info.empty() ? "1." : b.info;
    return "-";
}

std::string line_of(const Block& b) {
    switch (b.kind) {
    case BlockKind::heading: return std::string(static_cast<std::size_t>(std::clamp(b.level, 1, 6)), '#') + " " + inline_text(b);
    case BlockKind::quote: return "> " + inline_text(b);
    case BlockKind::bullet:
    case BlockKind::ordered:
        return std::string(2 * static_cast<std::size_t>(b.level), ' ') + list_marker(b) + " " + inline_text(b);
    case BlockKind::rule: return "---";
    default: return escape_block_start(inline_text(b));
    }
}

} // namespace

std::string to_markdown(const Document& doc) {
    std::string out;
    for (std::size_t i = 0; i < doc.size(); ++i) {
        const Block& b = doc[i];
        if (i > 0) out += is_list(b.kind) && is_list(doc[i - 1].kind) ? "\n" : "\n\n";
        if (b.kind != BlockKind::code) {
            out += line_of(b);
            continue;
        }
        // A fence around the code lines that follow, of one language: longer than any backtick
        // run inside them, so a line of ``` stays code.
        std::size_t last = i;
        std::size_t ticks = 2;
        for (; last < doc.size() && doc[last].kind == BlockKind::code && doc[last].info == b.info; ++last)
            ticks = std::max(ticks, longest_ticks(doc[last].text));
        const std::string fence(ticks + 1, '`');
        out += fence + b.info + "\n";
        for (; i < last; ++i) out += doc[i].text + "\n";
        --i;
        out += fence;
    }
    return out + "\n";
}

} // namespace prism::text
