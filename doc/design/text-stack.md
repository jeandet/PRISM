# The text stack: from a text area to a code editor, rich text and Markdown

Status: built (2026-11-17), steps 1 to 6 below. What is left is listed at the end.

## What we want, and what it shares

Four widgets, one after the other:

1. **TextArea** (exists): a multi-line field for forms. One string, one font, background marks.
2. **TextEditor**: a real document editor. Large files, find and replace, wrap on or off.
3. **CodeEditor**: a TextEditor with a line-number gutter, syntax colors, indentation and
   bracket matching.
4. **Rich text and Markdown**: text with bold, italic, links, headings, lists, quotes, code
   blocks. First shown (a README in the app, a help page), later edited.

They differ in what they show around the text and in how keys behave. Underneath they need the
same four things:

- a **document**: text that can be large, changed in one place without copying the rest;
- **styles** on ranges of it: stored (bold in rich text) or computed (a keyword's color, a search
  match, the selection);
- a **layout** of styled text into lines: wrapped or not, right to left too, cached, and done only
  for what is on screen;
- **fonts** in more than one weight and slant.

So the plan is to build those four once, then each widget as a thin layer on them.

## Where the text code stands

- `TextArea<T>` keeps the whole text in its `Field`: each key copies the string, and each change
  wraps every line again (`delegates/text_delegates.hpp`). Fine for a note; a long file is the
  open question, to be measured before anything is built.
- Line breaking, bidi, caret movement and selection already exist as functions over one line
  (`ui/text_metrics.hpp`, `ui/text_bidi.hpp`, `ui/text_segmentation.hpp`, `ui/text_edit.hpp`).
  They take one font per line: they become per-run.
- A draw command holds text in one font role (`FontRole::Ui` or `Mono`) and one color
  (`render/draw_list.hpp`); the measurer is keyed the same way (`ui/text_metrics.hpp`). No bold,
  no italic, no other size within a line.

## The design

### 1. Fonts: a style, not just a role

`FontRole { Ui, Mono }` grows into a small value type:

```cpp
struct FontStyle {
    FontRole family = FontRole::Ui;   // Ui, Mono
    bool bold = false;
    bool italic = false;
    bool operator==(const FontStyle&) const = default;
};
```

`TextCmd` and `TextMeasurer::width` take a `FontStyle` instead of a role (the role converts, so
every existing call keeps compiling). The SDL backend loads Noto Sans Bold, Italic and Bold Italic
beside the regular face (real faces; SDL_ttf's synthetic bold is the fallback), and the mono family
gets a bold face for keywords. The web build bundles the same files.

### 2. The document: blocks, observed like a List

```cpp
namespace prism::text {
struct Span  { std::size_t begin, end; Style style; };          // byte range within the block
struct Block { BlockKind kind; std::string text; std::vector<Span> spans; };
class Document;   // a vector of blocks, with insert/erase/replace and change notices
}
```

- A **block** is a paragraph: a line of code, a paragraph of prose, a heading, a list item, a code
  block line. Plain text is one block per line with no spans.
- `Document` is an observable container like `core::List<T>`, not a value in a `Field`: an edit
  says which blocks changed (`{first, count_removed, count_inserted}`), so the views redo only
  those. A `Field` would copy the whole text on every key.
- Inside, the blocks sit in a **sum tree** (below, "Algorithms"): a B+ tree whose nodes carry
  totals, so a byte offset, a line number or a y position finds its block in O(log n).
- Edits are a few operations on a position `{block, byte}`: insert text, erase a range, set a
  block's kind, add or remove a span. Each records its inverse: that is the editor's undo, for
  free, and the same record serves a collaborative or remote edit later.
- Logic thread only, like every model value.

### 3. Styles are names; the theme gives them looks

A span or a highlighter never says "red" or "bold". It says what the text **is**:

```cpp
enum class Style : std::uint16_t {
    plain, emphasis, strong, code, link, heading1, heading2, heading3,      // rich text
    keyword, type, string, number, comment, preprocessor, function,          // code
    match, current_match, error, warning,                                    // decorations
};
```

The theme maps each name to a `FontStyle`, a size factor, a color and an optional background. So
light and dark themes, and a high-contrast one, restyle every editor at once, and a highlighter
cannot pick a color that disappears on dark. (A rich-text editor that needs arbitrary colors adds
a color span later; it is not needed to start.)

Styles come from three sources, merged per block when it is laid out:

1. **stored**: the document's spans (rich text, Markdown);
2. **computed**: a `Highlighter` (syntax colors), run per block;
3. **decorations**: search matches, the selection, diagnostics, from the editor.

A computed style never enters the document: the document stays what the user wrote.

### 4. Highlighting: one small interface

```cpp
struct Highlighter {
    using State = std::uint32_t;   // what a block ends inside: a comment, a string...
    virtual State highlight(std::string_view block, State before, std::vector<Span>& out) const = 0;
};
```

A block is highlighted from the state the previous block ended in, so an edit re-highlights from
that block until the end state stops changing (usually one block). Two implementations:

- **Rules, as data** (built in, no dependency, works in the browser): per language, keywords,
  types, string and comment delimiters (multi-line ones carry over in `State`), numbers. Shipped
  for C++, Python, SQL, JSON, TOML and Markdown.
- **tree-sitter** (later, optional, on WrapDB): precise, but one grammar per language to bundle.
  It fits the same interface through a per-document parse.

### 5. Layout: per block, cached, only what is visible

- A block is laid out into lines: styled runs measured with their font, wrapped at the width (or
  not: code), bidi reordered. The result is cached with the block's version and the width.
- The document's height is the sum of the block heights, kept as prefix sums; a block not yet
  laid out uses an estimate (lines x line height) until it is. Scrolling finds the first visible
  block by binary search and lays out only the visible ones: the virtual list's approach, for
  text.
- Caret, selection and hit testing work on `{block, byte}` and reuse the one-line functions that
  exist, made per-run.

### 6. Widgets on top

| Widget | Is | Adds |
|---|---|---|
| `TextView` | the layout, read-only, selectable | links that open, copy |
| `TextEditor` | `TextView` + editing | caret, keys, clipboard, input methods, undo, find and replace, wrap toggle |
| `CodeEditor` | `TextEditor` + code behaviour | gutter with line numbers, current line, Tab and Shift+Tab, auto-indent, matching bracket, go to line, a `Highlighter` |
| Markdown view | `markdown::parse(source)` into a `Document`, shown by `TextView` | headings, lists, quotes, code blocks (highlighted), tables later |
| Markdown editor | a `CodeEditor` on the source with the Markdown highlighter, beside a `TextView` of it | nothing new: composition |

`TextArea` stays as it is for forms. Once `TextEditor` exists it may be rebuilt on the same
layout, which would bring it large-text speed and bold/italic for free; not needed to start.

Binding follows the table's pattern (a source object, not a `Field` of the whole value):

```cpp
text::Document notes;                                     // a model member
vb.text_editor(notes);
vb.code_editor(source, {.language = text::cpp()});
vb.text_view(text::markdown(readme_source));              // read-only, rendered
```

With reflection, a `text::Document` member shows as a `TextEditor`.

### 7. Markdown

cmark-gfm (GitHub's CommonMark with tables; on WrapDB) parses the source into a tree; one function
walks it into blocks and spans. Code blocks name their language, so they get the matching
highlighter. Images and tables come after text, lists and code.

## Algorithms, and where they come from

The choices below follow what current editors do, each picked for what PRISM already has.

| Problem | Choice | Used by | Why here |
|---|---|---|---|
| Storing a large, edited text | A **sum tree**: a copy-on-write B+ tree of blocks whose inner nodes keep totals (blocks, bytes, newlines, height) | Zed ([rope and SumTree](https://zed.dev/blog/zed-decoded-rope-sumtree)); VS Code uses a piece tree for the same O(log n) lookups | Copy-on-write gives a cheap immutable snapshot: the render thread already works from snapshots. The height total doubles as the height map. |
| Scrolling a document much taller than the window | A **height map**: each block's height is an estimate until it is laid out, then measured; the tree's height totals find the block at a y in O(log n) | CodeMirror 6 (its viewport and height map) | Only the visible blocks, plus a margin, are ever laid out. Kept in the same tree, not a second structure. |
| Syntax colors that stay right while typing | **Per-block end state, with early stop**: highlight a block from the state the previous one ended in; after an edit, go on down only until a block ends in the same state as before | VS Code's TextMate tokenizer ([2017 write-up](https://code.visualstudio.com/blogs/2017/02/08/syntax-highlighting-optimizations)) | Usually one block per key. Blocks off screen are highlighted when idle, never on the key's path. |
| Exact syntax trees | **Incremental parsing**: re-parse only the edited part of the tree | tree-sitter (on WrapDB) | Later, behind the same `Highlighter` interface, for languages where rules are not enough. |
| Shaping, line breaking, bidi, graphemes | **HarfBuzz** (shaping), **UAX #14** line breaking (libunibreak), **UAX #9** bidi (SheenBidi), **UAX #29** clusters | Parley, cosmic-text and browsers do the same | All four are already PRISM dependencies; today they run per line with one font, and become per styled run. |
| Not shaping the same text twice | A **layout cache per block**, keyed by the block's version, the width and the theme, within a memory budget (least recently used dropped first) | cosmic-text caches each line's shaping and layout; parley-based crates do the same | Typing re-shapes one block; scrolling back re-shapes nothing. |
| Formatting that behaves at its edges | **Spans with an expand rule**: typing at the end of bold continues bold, at the end of a link does not | Peritext ([paper](https://www.inkandswitch.com/peritext/)), the rich-text CRDT | The same model later supports editing by several people (a CRDT), without changing what a span is. |
| Undo that feels right | **Inverse operations, grouped**: typing merges until a pause, a word boundary or a caret jump; the selection is restored with the text | every editor | Free from the document's edit records. |
| Find in a large document | **Boyer-Moore-Horspool** over the blocks (`std::boyer_moore_horspool_searcher`); a regex engine later | standard | No dependency for plain search. `std::regex` is too slow for this; a regex comes with its own step. |
| Completion that finds what was meant | **Fuzzy subsequence ranking**: every typed letter in order; a match at the start, then at a word's start (`_`, camelCase), then a run of letters, scores more | VS Code's fuzzy matcher, fzf | Sources only list candidates; one ranking in the editor (`text/complete.hpp`). Greedy for now; fzf's best alignment if it misranks. |
| Markdown | **cmark-gfm**, GitHub's spec-compliant CommonMark with tables | GitHub | On WrapDB; one walk turns its tree into blocks and spans. |

simplify: one block holds a whole line. A single huge line (a minified file) is then laid out at
once; Zed instead cuts text into fixed-size chunks independent of lines. Add chunking inside a
block when such a file is measured to matter.

## Order of work

Each step ends with something visible, tested, and shown in the gallery and the browser.

1. **Measure** `TextArea` on 1k, 10k and 100k lines (typing, scrolling, publish cost). This says
   how urgent the document is, and gives the numbers to beat. Done (2026-11-16,
   `benchmarks/text_scale.cpp`, release, no-font estimate so real fonts cost more). Median ms,
   input handling + publish:

   | lines | type a key | arrow down | wheel |
   |---|---|---|---|
   | 1,000 | 5.8 + 3.7 | 5.8 + 3.7 | 3.5 + 3.7 |
   | 10,000 | 61 + 38 | 61 + 38 | 36 + 38 |
   | 100,000 | 634 + 392 | 633 + 389 | 372 + 384 |

   Every event is O(whole text), scrolling included: the delegate copies and re-wraps the whole
   string, and the publish lays all of it out again. Past a few thousand lines it cannot keep up
   with typing. An unchanged publish costs nothing (the tree skips it).

   The same text in a `text::Document` shown by `vb.text_view` (2026-11-17, same benchmark):

   | lines | first publish | type (edit + publish) | wheel |
   |---|---|---|---|
   | 1,000 | 0.1 | 0.00 + 0.01 | 0.00 + 0.02 |
   | 10,000 | 0.5 | 0.00 + 0.01 | 0.00 + 0.02 |
   | 100,000 | 6.9 | 0.00 + 0.01 | 0.00 + 0.02 |

   Typing and scrolling no longer depend on the document's length; the first publish is the
   height map's estimates, O(n) and cheap.

   `vb.code_editor(doc, "cpp")` on the same text: the first Ctrl+End highlights every line above
   the end (states carry from line to line) once: 0.9 ms at 1,000 lines, 6.7 ms at 10,000, 71 ms
   at 100,000. A key then costs 0.03 ms. Since 2026-11-17 the view highlights ahead between
   frames (4,000 lines a frame, about 3 ms), so after a moment the jump finds the work done.
2. **Fonts**: `FontStyle`, bold and italic faces on desktop and web, the theme's style table.
3. **Document + layout + `TextView`**, with Markdown into a document. Read-only first: it proves
   the document, styled runs, caching and visible-only layout without any editing. Visible
   result: the README rendered in the gallery.
4. **`TextEditor`**: editing on the document, porting `TextArea`'s caret, selection, bidi and
   input method code to `{block, byte}` positions. Find and replace.
5. **`CodeEditor`**: gutter, highlighter interface, rule-based languages, indentation, brackets.
   The data studio's SQL editor and the notes app move to it.
6. **Rich text editing**: commands that add or remove spans (Ctrl+B, Ctrl+I, a link), block kinds
   (heading, list). Markdown editor with live preview in the notes app.

## Built, and what is left (2026-11-17)

- `core::SumTree` (`core/sum_tree.hpp`), `text::Document` (`text/document.hpp`), `layout_block`
  (`text/layout.hpp`), `View` (`text/view.hpp`, behind `vb.text_view`), `Editor`
  (`text/editor.hpp`, `vb.text_editor`), `Highlighter` and the built-in languages
  (`text/highlight.hpp`), `vb.code_editor(doc, "cpp")`, `text::markdown` (`prism-markdown`),
  `text::to_markdown`, rich text editing (`EditorOptions::rich`), `FontStyle` (bold and italic in
  every backend), `.rows(n)` and `.highlights(field)` on the handle `vb` returns. The gallery's
  Document tab edits Markdown and rich text side by side; its Text tab's code sample is a code
  editor; the data studio's query editor is `vb.code_editor(text, "sql")` with its error mark as
  highlights; the notes app edits a `Document`, its search hits as highlights.
- A minimap (`ViewOptions::minimap`, on in `vb.code_editor`): each block a 2 px strip, words as
  bars in their colors from the same highlighter states, a band over what is in sight, scrolling
  along on a long file. Its band is grabbed and dragged like a scrollbar's thumb (line for line
  while the file fits, across the whole file otherwise); a press elsewhere jumps there first.
- Completion (`text/complete.hpp`, `EditorOptions::complete`, `.complete(...)` on the handle): a
  source lists candidates, the editor ranks them against the word at the caret and shows the
  best in a list under it (above when there is no room). Up/Down, Page Up/Down, Enter or Tab,
  Escape; a click takes a row. `vb.code_editor` completes keywords, types and the file's words
  within 2000 lines of the caret.
- Python: `prism.document(text, markdown=False)` with `vb.text_editor`, `code_editor`,
  `text_view` (`python/src/text_bound.hpp`, example 19). Markdown is compiled into the extension.
- Also: `text::find_all`, `find_next`, `replace_all` (Boyer-Moore-Horspool, non-overlapping
  matches, one undo step), undo and redo putting the caret at the change, an input method's
  composition shown at the caret.
- Left, each noted as `simplify:` where the cut is:
  - the mono font's bold and italic are still drawn by SDL_ttf (its faces are 2.4 MB each; the
    UI font's real Bold, Italic and Bold Italic are bundled since 2026-11-17);
  - a scrollbar for the sideways scroll (a swipe and the caret move it; nothing shows how far);
    an input method's composition drawn over the text
    after the caret rather than pushing it along; highlighting past a table's columns sized from all its rows (they
    are equal today);
    the layout cache trimmed least-recently-used;
  - completion: a source answers at once (a language server's slow answers need an async
    source), no snippets with placeholders, the file's words from a window around the caret;
  - stepping by character rescans the block (ui::next_grapheme), so a click or a wrap in one very
    long paragraph is quadratic in its length; per-block grapheme marks, cached with the layout,
    when a long paragraph shows it.

## Open questions

- Spans as a flat list per block (simple, what Markdown and highlighters produce) or a tree (what
  nested rich text formats keep)? Flat is proposed: nesting becomes overlapping spans.
- Copy and paste of rich text: plain text only at first; HTML or Markdown on the clipboard later.
- Very long single lines (a minified file): wrapped layout handles them; unwrapped needs the line
  itself cut into visible pieces. Deferred until measured.
