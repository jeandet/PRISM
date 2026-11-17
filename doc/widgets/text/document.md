# The document

A `text::Document` is a text made of blocks. A block is a line of code, a paragraph, a
heading, a list item or a table row. Each block holds its text and its styled spans.
Views show a document; the document itself knows nothing of the screen.

```cpp
text::Document doc{"first line\nsecond line"};   // one block per line

doc.insert({1, 0}, "the ");                      // "the second line"
doc.style({{0, 0}, {0, 5}}, text::Style::strong); // "first" in bold
doc.undo();                                       // the bold goes
std::string all = doc.text();
```

A document lives on the logic thread, like every model value.

## Positions and ranges

| Type | Members | Meaning |
|---|---|---|
| `text::Position` | `block`, `byte` | a place in the text: the block's index, and a byte offset in its text |
| `text::Range` | `begin`, `end` | the text between two positions; `empty()` when they are equal |

Positions compare with `<`, `==` and the rest.
Bytes, not characters: a UTF-8 character can take several bytes.

## Reading

| Method | Returns |
|---|---|
| `size()` | the number of blocks |
| `doc[i]` | block `i`: its `text`, `spans`, `kind`, `level` and `info` |
| `text()` | the whole text, blocks joined by `'\n'` |
| `text(range)` | the text of a range |
| `end()` | the position after the last character |
| `bytes()` | the size in bytes, newlines included |
| `offset_of(pos)`, `position_at(offset)` | between a position and a byte offset in the whole text |

A `core::Derived` that reads a document follows it, as it follows a field.

## Editing

Each edit returns where it ends, and is one undo step unless it joins the one before.
Text may hold `'\n'`: it splits blocks.

| Method | Does |
|---|---|
| `insert(pos, text)` | inserts text |
| `erase(range)` | removes text |
| `replace(range, text)` | replaces text |
| `style(range, style, target = "")` | adds a style to a range; `target` is a link's address |
| `unstyle(range, style)` | takes a style off a range |
| `set_kind(block, kind, level = 0, info = "")` | makes a block a heading, a list item, a quote... |
| `assign(other)` | everything replaced by `other`'s blocks, as one undoable edit |
| `reset(other)` | the same with no undo back: for a file just opened |
| `undo()`, `redo()` | step back and forth; false when there is nothing to undo or redo |
| `can_undo()`, `can_redo()` | whether there is |
| `break_undo_group()` | the next edit starts a new undo step |

Use `assign` or `reset` to load new text. Assigning a new `Document` over the old one would
leave the views on the old one.

## Blocks and styles

`text::BlockKind` is `paragraph`, `heading`, `quote`, `bullet`, `ordered`, `code`, `rule`
or `table_row`.
A block's `level` is a heading's level (1 to 6) or a list item's depth (0 at the top).
Its `info` is a code block's language, or a list item's marker (`"•"`, `"3."`).

`text::Style` names what a span is, not how it looks. The theme picks the look:

| Styles | For |
|---|---|
| `strong`, `emphasis`, `code`, `strike`, `link` | rich text |
| `keyword`, `type`, `string`, `number`, `comment`, `preprocessor`, `function` | code colors |
| `match`, `current_match`, `error`, `warning` | decorations |

## Watching changes

`doc.on_change()` reports each edit as a `text::Change`: the blocks from `first`,
`removed` of them replaced by `inserted` new ones.

```cpp
struct Notes {
    text::Document doc;
    core::Field<std::string> status{""};
    core::Wiring wire;

    Notes() {
        wire.on(doc, [this](const text::Change&) { status = std::to_string(doc.size()) + " lines"; });
    }
};
```

## See also

- [Views and editors](views-and-editors.md): showing and editing a document.
- [Markdown and rich text](../documents.md): documents from and to Markdown.
