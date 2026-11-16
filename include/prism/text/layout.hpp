#pragma once

#include <prism/render/draw_list.hpp>
#include <prism/text/document.hpp>
#include <prism/ui/context.hpp>
#include <prism/ui/text_metrics.hpp>

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

// One block of a document laid out into lines of styled runs (doc/design/text-stack.md): what a
// text view draws, hit-tests and puts a caret in. A pure function of the block, the spans computed
// for it (a highlighter's, the search matches) and the style: a view caches the result per block.
namespace prism::text {

// How a run of text looks: what a Style becomes under a theme, on a block of some kind.
struct Look {
    render::FontStyle font{};
    float size = 14.f;
    render::Color color{};
    std::optional<render::Color> background{};
    bool underline = false;
    bool strike = false;
    bool operator==(const Look&) const = default;
};

struct LayoutStyle {
    const ui::TextMeasurer* measurer = nullptr;   // null: the monospace estimate
    const ui::Theme* theme = nullptr;             // null: the default (dark) theme
    float size = 14.f;                            // body text
    float width = 0.f;                            // the column; 0 or less: no wrapping
    render::FontRole family = render::FontRole::Ui;   // Ui: prose, spaced paragraphs. Mono: code
    bool wrap = true;
};

struct Run {
    std::size_t begin = 0, end = 0;   // bytes of the block's text
    float x = 0.f, width = 0.f;
    Look look{};
};

struct Line {
    std::size_t begin = 0, end = 0;   // a wrapped line keeps its hanging spaces
    float y = 0.f, height = 0.f;      // from the block's top
    std::vector<Run> runs{};
};

struct BlockLayout {
    std::vector<Line> lines{};        // at least one, even for an empty block
    float height = 0.f;               // the lines and the space around them
    float indent = 0.f;               // the text's left margin (quotes, list items)
    std::string marker{};             // drawn in the margin: a bullet, "3."
    std::optional<render::Color> background;   // a code block's
    bool quote_bar = false;
    bool rule = false;                // a horizontal rule, drawn across the line
};

[[nodiscard]] BlockLayout layout_block(const Block& block, std::span<const Span> computed, const LayoutStyle& style);

// The x of a byte within a laid out line, and the byte nearest an x (a click, a caret moving down).
[[nodiscard]] float x_of(const Block& block, const Line& line, std::size_t byte, const LayoutStyle& style);
[[nodiscard]] std::size_t byte_at(const Block& block, const Line& line, float x, const LayoutStyle& style);

} // namespace prism::text
