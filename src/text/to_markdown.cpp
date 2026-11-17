#include <prism/text/to_markdown.hpp>

#include <algorithm>
#include <string_view>
#include <vector>

namespace prism::text {

namespace {

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
    std::ranges::stable_sort(edges, [](const Edge& x, const Edge& y) {
        if (x.at != y.at) return x.at < y.at;
        return !x.opens && y.opens;   // closes first
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
        else out += marker(e.span->style);
        if (e.span->style == Style::code) in_code += e.opens ? 1 : -1;
    }
    text_until(b.text.size());
    return out;
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
    default: return inline_text(b);
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
        out += "```" + b.info + "\n";   // a fence around the code lines that follow, of one language
        for (;; ++i) {
            out += doc[i].text + "\n";
            if (i + 1 >= doc.size() || doc[i + 1].kind != BlockKind::code || doc[i + 1].info != b.info) break;
        }
        out += "```";
    }
    return out + "\n";
}

} // namespace prism::text
