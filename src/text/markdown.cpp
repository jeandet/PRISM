#include <prism/text/markdown.hpp>

#include <cmark-gfm-core-extensions.h>
#include <cmark-gfm.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace prism::text {

namespace {

std::string_view type_name(cmark_node* n) { return cmark_node_get_type_string(n); }

std::string literal(cmark_node* n) {
    const char* s = cmark_node_get_literal(n);
    return s ? s : "";
}

void mark(Block& b, std::size_t from, Style s, std::string target = {}) {
    if (b.text.size() > from) b.spans.push_back({from, b.text.size(), s, std::move(target)});
}

// The inline content of `n` appended to the block: its text, and its styles as spans.
void inlines(cmark_node* n, Block& b) {
    for (cmark_node* c = cmark_node_first_child(n); c; c = cmark_node_next(c)) {
        const std::size_t from = b.text.size();
        switch (cmark_node_get_type(c)) {
        case CMARK_NODE_TEXT:
        case CMARK_NODE_HTML_INLINE: b.text += literal(c); break;
        case CMARK_NODE_SOFTBREAK:
        case CMARK_NODE_LINEBREAK: b.text += ' '; break;
        case CMARK_NODE_CODE: b.text += literal(c); mark(b, from, Style::code); break;
        case CMARK_NODE_EMPH: inlines(c, b); mark(b, from, Style::emphasis); break;
        case CMARK_NODE_STRONG: inlines(c, b); mark(b, from, Style::strong); break;
        case CMARK_NODE_LINK: inlines(c, b); mark(b, from, Style::link, cmark_node_get_url(c)); break;
        case CMARK_NODE_IMAGE: inlines(c, b); mark(b, from, Style::emphasis); break;
        default:
            inlines(c, b);
            if (type_name(c) == "strikethrough") mark(b, from, Style::strike);
        }
    }
}

const char* bullet(int depth) {
    static constexpr const char* marks[] = {"\xE2\x80\xA2", "\xE2\x97\xA6", "\xE2\x96\xAA"};   // • ◦ ▪
    return marks[depth % 3];
}

// Where a block sits: inside quotes, inside list items (depth), and the marker its first
// paragraph takes when it opens a list item.
struct Place {
    bool quote = false;
    int depth = -1;
    BlockKind item_kind = BlockKind::bullet;
    std::string marker{};
};

struct Walker {
    std::vector<Block> out;

    void children(cmark_node* n, Place place) {
        for (cmark_node* c = cmark_node_first_child(n); c; c = cmark_node_next(c)) {
            block(c, place);
            place.marker.clear();   // only the first block of an item carries its marker
        }
    }

    void block(cmark_node* n, const Place& place) {
        switch (cmark_node_get_type(n)) {
        case CMARK_NODE_PARAGRAPH: paragraph(n, place); break;
        case CMARK_NODE_HEADING: {
            Block b{.kind = BlockKind::heading, .level = cmark_node_get_heading_level(n)};
            inlines(n, b);
            out.push_back(std::move(b));
            break;
        }
        case CMARK_NODE_CODE_BLOCK: code(literal(n), cmark_node_get_fence_info(n)); break;
        case CMARK_NODE_HTML_BLOCK: code(literal(n), "html"); break;
        case CMARK_NODE_THEMATIC_BREAK: out.push_back(Block{.kind = BlockKind::rule}); break;
        case CMARK_NODE_BLOCK_QUOTE: {
            Place inner = place;
            inner.quote = true;
            children(n, inner);
            break;
        }
        case CMARK_NODE_LIST: list(n, place); break;
        default:
            if (type_name(n) == "table") table(n);
            else children(n, place);
        }
    }

    void paragraph(cmark_node* n, const Place& place) {
        Block b;
        inlines(n, b);
        if (place.depth >= 0) {
            b.kind = place.item_kind;
            b.level = place.depth;
            b.info = place.marker;   // empty for an item's later paragraphs: indented, no marker
        } else if (place.quote) {
            b.kind = BlockKind::quote;
        }
        out.push_back(std::move(b));
    }

    void code(const std::string& text, const char* info) {
        std::size_t start = 0;
        const std::string lang = info ? info : "";
        const std::size_t end = !text.empty() && text.back() == '\n' ? text.size() - 1 : text.size();
        while (start <= end) {
            std::size_t nl = text.find('\n', start);
            if (nl == std::string::npos || nl > end) nl = end;
            out.push_back(Block{.text = text.substr(start, nl - start), .kind = BlockKind::code, .info = lang});
            start = nl + 1;
        }
    }

    void list(cmark_node* n, const Place& place) {
        const bool ordered = cmark_node_get_list_type(n) == CMARK_ORDERED_LIST;
        int number = cmark_node_get_list_start(n);
        for (cmark_node* item = cmark_node_first_child(n); item; item = cmark_node_next(item), ++number) {
            Place inner = place;
            inner.depth = place.depth + 1;
            inner.item_kind = ordered ? BlockKind::ordered : BlockKind::bullet;
            if (type_name(item) == "tasklist")
                inner.marker = cmark_gfm_extensions_get_tasklist_item_checked(item) ? "\xE2\x98\x91" : "\xE2\x98\x90";
            else
                inner.marker = ordered ? std::to_string(number) + "." : bullet(inner.depth);
            children(item, inner);
        }
    }

    void table(cmark_node* n) {
        for (cmark_node* row = cmark_node_first_child(n); row; row = cmark_node_next(row)) {
            Block b;
            for (cmark_node* cell = cmark_node_first_child(row); cell; cell = cmark_node_next(cell)) {
                if (cell != cmark_node_first_child(row)) b.text += "  \xE2\x94\x82  ";   // │
                inlines(cell, b);
            }
            if (cmark_gfm_extensions_get_table_row_is_header(row)) mark(b, 0, Style::strong);
            out.push_back(std::move(b));
        }
    }
};

} // namespace

Document markdown(std::string_view source) {
    cmark_gfm_core_extensions_ensure_registered();
    std::unique_ptr<cmark_parser, decltype(&cmark_parser_free)> parser{cmark_parser_new(CMARK_OPT_DEFAULT),
                                                                      &cmark_parser_free};
    for (const char* name : {"table", "strikethrough", "autolink", "tasklist"})
        if (cmark_syntax_extension* ext = cmark_find_syntax_extension(name))
            cmark_parser_attach_syntax_extension(parser.get(), ext);
    cmark_parser_feed(parser.get(), source.data(), source.size());
    std::unique_ptr<cmark_node, decltype(&cmark_node_free)> root{cmark_parser_finish(parser.get()), &cmark_node_free};
    Walker walker;
    walker.children(root.get(), {});
    return Document{std::move(walker.out)};
}

} // namespace prism::text
