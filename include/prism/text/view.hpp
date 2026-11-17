#pragma once

#include <prism/core/connection.hpp>
#include <prism/core/field.hpp>
#include <prism/core/sum_tree.hpp>
#include <prism/core/types.hpp>
#include <prism/render/draw_list.hpp>
#include <prism/text/document.hpp>
#include <prism/text/highlight.hpp>
#include <prism/text/layout.hpp>
#include <prism/ui/context.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <optional>
#include <unordered_map>
#include <vector>

// A document drawn into a rectangle (doc/design/text-stack.md): only the blocks in sight are laid
// out, each once per version, and a height map (a SumTree of block heights, estimated until a block
// is laid out) finds the block at any scroll offset in O(log n). What `vb.text_view(doc)` shows.
namespace prism::text {

struct ViewOptions {
    float size = 14.f;
    render::FontRole family = render::FontRole::Ui;   // Ui: prose with spaced paragraphs. Mono: code
    bool wrap = true;
    float padding = 8.f;
    const Highlighter* highlighter = nullptr;   // code: its colors (text/highlight.hpp)
    bool line_numbers = false;                  // a gutter numbering the blocks
    bool current_line = false;                  // the caret's line shaded
    bool minimap = false;                       // the whole document shrunk in a column on the right
    std::string placeholder{};                  // shown, muted, while the document is empty
};

class View {
public:
    explicit View(Document& doc, ViewOptions options = {});
    View(const View&) = delete;
    View& operator=(const View&) = delete;

    // Draws what is in sight at the scroll offset into a `size` rectangle at (0, 0).
    void draw(render::DrawList& dl, core::Size size, const ui::Theme& theme);

    // The scroll state, in the shape the widget tree scrolls by (app/widget_tree_layout.hpp).
    core::DY scroll{0};
    core::Height viewport_h{0};
    core::Height content_h{0};
    std::uint8_t show_ticks = 0;
    // Sideways, without wrapping: how far the text is scrolled left, within the widest line laid
    // out so far. The gutter does not move.
    core::DX scroll_x{0};
    void scroll_sideways(float dx);

    // The minimap: each block a strip 2 px tall, its words bars in their colors, a band over what is
    // in sight. On a document taller than the column it scrolls along with the view.
    [[nodiscard]] float minimap_left() const { return minimap_left_; }
    [[nodiscard]] std::size_t minimap_first() const { return minimap_first_; }   // the block at its top
    [[nodiscard]] std::size_t minimap_rows() const { return minimap_rows_; }
    [[nodiscard]] bool in_minimap(core::Point p) const { return options_.minimap && p.x.raw() >= minimap_left_; }
    void minimap_scroll(core::Point p);              // the block under p into the middle of the view
    [[nodiscard]] float minimap_band_top() const { return band_top_; }
    // A press grabs the band where it is pressed (or, outside it, jumps there first); a drag then
    // moves it with the pointer: line for line while the whole file fits in the minimap, across
    // the whole file otherwise (as a scrollbar's thumb).
    void minimap_press(core::Point p);
    void minimap_drag(core::Point p);
    static render::Color minimap_tint(render::Color c) { return render::Color::rgba(c.r, c.g, c.b, 150); }

    // Brings up to `lines` more blocks' highlighting states up to date, past what is in sight:
    // what a view does between frames, so a jump far down does not wait for every line above.
    // Returns whether any are left.
    bool highlight_ahead(std::size_t lines);

    // What an editor shows over the text: the caret, and the selection between it and the anchor.
    struct Marks {
        Position caret;
        Position anchor;
        std::vector<Range> highlights{};   // drawn behind the text: matching brackets, search hits
        std::string preedit{};             // an input method's composition, not yet text
        std::size_t preedit_caret = 0;     // the input method's caret in it, in bytes
    };
    std::optional<Marks> marks;
    // Ranges the app highlights, shown focused or not: a SQL error's token, search matches.
    const core::Field<std::vector<Range>>* decorations = nullptr;

    // Geometry, in the view's coordinates ((0, 0) is its corner). Each lays out what it needs.
    [[nodiscard]] Position hit(core::Point p);                      // the position nearest a point
    [[nodiscard]] core::Rect caret_rect(Position p);
    [[nodiscard]] Position step_lines(Position from, int lines, float x);   // up or down, at x
    [[nodiscard]] float caret_x(Position p);                        // from the text's left edge
    [[nodiscard]] Range visual_line(Position p);                    // the wrapped line holding p
    void reveal(Position p);                                        // scrolls p into sight

    [[nodiscard]] Document& document() { return doc_; }
    [[nodiscard]] const ViewOptions& options() const { return options_; }
    [[nodiscard]] std::size_t first_drawn() const { return first_drawn_; }
    [[nodiscard]] std::size_t laid_out() const { return cache_.size(); }      // blocks in the cache
    [[nodiscard]] std::uint64_t layouts_made() const { return layouts_made_; }  // ever, for tests

private:
    struct Heights {
        std::size_t count = 0;
        float height = 0.f;
        friend Heights operator+(Heights a, Heights b) { return {a.count + b.count, a.height + b.height}; }
    };
    struct HeightOf {
        Heights operator()(float h) const { return {1, h}; }
    };
    // A layout depends on the block and on the state its highlighting starts in.
    struct Key {
        std::uint64_t version;
        Highlighter::State before;
        bool operator==(const Key&) const = default;
    };
    struct KeyHash {
        std::size_t operator()(const Key& k) const { return std::hash<std::uint64_t>{}(k.version * 31 + k.before); }
    };
    static constexpr Highlighter::State unknown = ~Highlighter::State{0};

    Document& doc_;
    ViewOptions options_;
    LayoutStyle style_{};        // what the cache and the estimates were made with
    core::SumTree<float, Heights, HeightOf> heights_;
    std::unordered_map<Key, BlockLayout, KeyHash> cache_;
    // Highlighting: each block's end state, right for blocks before valid_. Past an edit (blocks
    // before edit_end_), a block ending as it did before means every later one does too.
    std::vector<Highlighter::State> ends_;
    std::size_t valid_ = 0;
    std::size_t edit_end_ = 0;
    float gutter_ = 0.f;
    float widest_ = 0.f;   // the widest line laid out (no wrapping): how far scroll_x goes
    float minimap_left_ = 0.f;
    std::size_t minimap_first_ = 0, minimap_rows_ = 0;
    float band_top_ = 0.f, band_h_ = 0.f;   // the band over what is in sight, at the last draw
    float grab_ = 0.f;                      // where in the band the pointer holds it
    std::size_t last_drawn_ = 0;
    float text_w_ = 0.f;   // the text's column at the last draw
    std::size_t first_drawn_ = 0;
    std::uint64_t layouts_made_ = 0;
    core::Connection follow_;

    float estimate(const Block& b) const;
    void reset_heights();
    void follow(const Change& c);
    const BlockLayout& layout(std::size_t i);
    Highlighter::State state_before(std::size_t i);
    void follow_states(const Change& c);
    float text_left(const BlockLayout& l) const {
        return options_.padding + gutter_ + l.indent - (options_.wrap ? 0.f : scroll_x.raw());
    }
    void clamp_sideways();
    void draw_minimap(render::DrawList& dl, const ui::Theme& theme);
    float block_top(std::size_t i) const { return heights_.prefix(i).height; }   // in content coordinates
    static std::size_t line_of(const BlockLayout& l, std::size_t byte);
    // dx: what to add to x inside a clip whose origin is not the view's (the text column's).
    void draw_block(render::DrawList& dl, std::size_t i, const BlockLayout& l, float top, float width,
                    const ui::Theme& theme, float dx) const;
    void draw_number(render::DrawList& dl, std::size_t i, const BlockLayout& l, float top, const ui::Theme& theme) const;
    void draw_preedit(render::DrawList& dl, const ui::Theme& theme);
    void draw_range(render::DrawList& dl, std::size_t i, const BlockLayout& l, float top, Range r,
                    render::Color color, bool with_newline, float dx) const;
};

} // namespace prism::text
