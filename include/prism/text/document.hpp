#pragma once

#include <prism/core/connection.hpp>
#include <prism/core/sum_tree.hpp>

#include <compare>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// A text document: blocks (a paragraph, a line of code, a heading, a list item...) holding text
// and styled spans, edited in place, observed, and undone (doc/design/text-stack.md).
//
//   text::Document doc{"first line\nsecond line"};
//   doc.insert({1, 0}, "the ");                 // a Position is {block, byte}
//   doc.style({{0, 0}, {0, 5}}, Style::strong);
//   doc.on_change().connect([](const Change& c) { ... blocks c.first.. changed ... });
//
// An edit replaces only the blocks it touches, so a view lays out only those again. The blocks sit
// in a SumTree keyed on their bytes: a byte offset finds its block in O(log n). Logic thread only,
// like every model value.
namespace prism::text {

// What a span of text is. The theme decides how each looks, so a highlighter or a parser never
// picks a color that a dark theme cannot show.
enum class Style : std::uint8_t {
    strong, emphasis, code, strike, link,                              // rich text
    keyword, type, string, number, comment, preprocessor, function,    // code
    match, current_match, error, warning,                              // decorations
};

// Whether text typed right at a span's end joins it: yes for bold (typing on in bold), no for a
// link (typing after a link is not part of it). The rule of Peritext, the rich-text CRDT.
constexpr bool grows_at_end(Style s) { return s != Style::link; }

enum class BlockKind : std::uint8_t { paragraph, heading, quote, bullet, ordered, code, rule };

struct Span {
    std::size_t begin = 0, end = 0;   // bytes in the block's text
    Style style = Style::strong;
    std::string target{};             // a link's address
    bool operator==(const Span&) const = default;
};

struct Block {
    std::string text{};
    std::vector<Span> spans{};
    BlockKind kind = BlockKind::paragraph;
    int level = 0;               // a heading's level (1-6), a list item's depth
    std::string info{};          // a code block's language; a list item's marker ("•", "3.", "☑")
    std::uint64_t version = 0;   // new at each change: what a layout cache keys on
};

struct Position {
    std::size_t block = 0, byte = 0;
    auto operator<=>(const Position&) const = default;
};

struct Range {
    Position begin, end;
    [[nodiscard]] bool empty() const { return begin == end; }
};

// Blocks [first, first + removed) were replaced by blocks [first, first + inserted).
struct Change {
    std::size_t first = 0, removed = 0, inserted = 0;
    bool operator==(const Change&) const = default;
};

class Document {
public:
    Document() : Document(std::string_view{}) {}
    explicit Document(std::string_view text);   // one block per line
    explicit Document(std::vector<Block> blocks);   // what a parser builds; at least one block kept

    [[nodiscard]] std::size_t size() const { return blocks_.size(); }
    [[nodiscard]] const Block& operator[](std::size_t i) const { return blocks_[i]; }
    [[nodiscard]] std::size_t bytes() const { return blocks_.total() - 1; }   // newlines included
    [[nodiscard]] Position end() const { return {size() - 1, blocks_[size() - 1].text.size()}; }
    [[nodiscard]] Position position_at(std::size_t offset) const;
    [[nodiscard]] std::size_t offset_of(Position p) const { return blocks_.prefix(p.block) + p.byte; }
    [[nodiscard]] std::string text() const { return text({{0, 0}, end()}); }
    [[nodiscard]] std::string text(Range r) const;

    // Each returns where the edit ends. Text may hold newlines: they split blocks.
    Position insert(Position at, std::string_view text);
    void erase(Range r);
    Position replace(Range r, std::string_view text);
    void style(Range r, Style s, std::string target = {});
    void unstyle(Range r, Style s);
    void set_kind(std::size_t block, BlockKind kind, int level = 0);
    // Everything replaced by `other`'s blocks, as one edit: a view showing this document follows
    // (assigning a new Document over it would leave the view on the old one). Undoable.
    void assign(Document other);

    // Edits between two breaks undo as one step: an editor breaks on a pause, a caret jump, a
    // new word. Undo and redo restore text, spans and kinds alike.
    void break_undo_group() { grouping_ = false; }
    bool undo();
    bool redo();
    [[nodiscard]] bool can_undo() const { return !undo_.empty(); }
    [[nodiscard]] bool can_redo() const { return !redo_.empty(); }

    core::SenderHub<const Change&>& on_change() { return changed_; }

private:
    struct Bytes {
        std::size_t operator()(const Block& b) const { return b.text.size() + 1; }   // + its newline
    };
    // Blocks [first, first + after) replaced the `before` blocks: what undo puts back.
    struct Step {
        std::size_t first;
        std::vector<Block> before;
        std::size_t after;
    };

    core::SumTree<Block, std::size_t, Bytes> blocks_;
    core::SenderHub<const Change&> changed_;
    std::vector<Step> undo_, redo_;
    bool grouping_ = false;
    std::uint64_t next_version_ = 1;

    // The one edit every other is made of: blocks [first, first + removed) become `blocks`.
    void splice(std::size_t first, std::size_t removed, std::vector<Block> blocks);
    std::vector<Block> take(std::size_t first, std::size_t count) const;
    void record(std::size_t first, std::vector<Block> before, std::size_t after);
    bool replay(std::vector<Step>& from, std::vector<Step>& to);
};

} // namespace prism::text
