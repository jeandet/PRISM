# Views and editors

Four calls show a `text::Document`. They differ in whether the user can type, and in how
the text looks.

| Call | Edits | Looks |
|---|---|---|
| `vb.text_view(doc, options)` | no | prose: wrapped, proportional font |
| `vb.text_editor(doc, options)` | yes | prose, unless the options say otherwise |
| `vb.code_view(doc, language)` | no, but selectable | code: mono, colors, long lines scroll sideways |
| `vb.code_editor(doc, language)` | yes | code: colors, line numbers, the caret's line shaded, a minimap, brackets matched, Tab indents, completion |

```cpp
struct Editor {
    text::Document source{"int main() {}\n"};
    text::Document notes{"Some notes."};
    text::Document readme{"Read only."};

    void view(app::ViewBuilder& vb) {
        vb.code_editor(source, "cpp");
        vb.text_editor(notes, {.rich = true});
        vb.text_view(readme);
    }
};
```

A view or editor fills the space it is given. Inside a scroll area, where there is no space
to fill, give it a height with `.rows(n)`.

## The handle

| Method | Does |
|---|---|
| `.rows(n)` | `n` lines tall, instead of filling its space |
| `.highlights(field)` | shades the ranges in a `Field<std::vector<text::Range>>`, as they change: search hits, an error |
| `.complete(source)` | completion from a function or a list of words (editors only); see [Completion](completion.md) |
| `.tooltip(text)` | a tooltip |
| `.focus_on_open()` | the focused widget when its popup opens |

## `text::ViewOptions`

| Member | Type | Default | Meaning |
|---|---|---|---|
| `size` | `float` | `14` | the font size, in points |
| `family` | `render::FontRole` | `Ui` | `Ui` for prose with spaced paragraphs, `Mono` for code |
| `wrap` | `bool` | `true` | long lines wrap; `false`: they scroll sideways |
| `padding` | `float` | `8` | space around the text |
| `highlighter` | `const text::Highlighter*` | none | the colors of code; see [Code](code.md) |
| `line_numbers` | `bool` | `false` | a gutter numbering the blocks |
| `current_line` | `bool` | `false` | the caret's line shaded |
| `minimap` | `bool` | `false` | the whole document, shrunk, in a column on the right |

## `text::EditorOptions`

| Member | Type | Default | Meaning |
|---|---|---|---|
| `view` | `text::ViewOptions` | | how it looks |
| `read_only` | `bool` | `false` | selectable and copyable, not changed |
| `tab_inserts` | `bool` | `false` | Tab types an indent instead of moving the focus |
| `auto_indent` | `bool` | `false` | Enter keeps the line's leading whitespace |
| `match_brackets` | `bool` | `false` | the bracket at the caret and its match are highlighted |
| `rich` | `bool` | `false` | rich-text keys and shortcuts; see [Markdown and rich text](../documents.md) |
| `indent` | `std::string` | four spaces | what Tab types |
| `complete` | `text::Completer` | none | what could go at the caret; none: no completion |

`code_editor(doc, language)` is `text_editor` with `Mono`, no wrap, the language's colors,
line numbers, the current line, the minimap, `tab_inserts`, `auto_indent`, `match_brackets`,
and completion from the language's keywords and the file's words.
For anything else, call `text_editor` with your own options.

## Using an editor

- Typing, selection, clipboard and input methods work as in a [text field](../controls/text-field.md).
- Ctrl+Z undoes and puts the caret back where the change was; Ctrl+Y or Ctrl+Shift+Z redoes.
  Typing between pauses undoes as one step.
- Page Up and Page Down move by a screen.
- In a code editor: Tab and Shift+Tab indent and unindent, Enter keeps the indent, and the
  caret beside a bracket shows its match.
- The minimap: drag its band to scroll, or click elsewhere in it to jump there.
- A click focuses the editor; so does Tab, except in a code editor, where Tab indents.

Every edit notifies the document's `on_change()`.

## See also

- [The document](document.md), [Code and syntax colors](code.md), [Find and replace](find.md).
