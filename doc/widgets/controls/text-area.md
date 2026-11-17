# Text area

A text area holds a few lines of text: a note, a message, a small script.
Long lines wrap, and a scrollbar appears when the text is taller than the area.

```cpp
struct Note {
    core::Field<ui::TextArea<>> body{{.placeholder = "Write here", .rows = 8}};
    core::Field<ui::TextArea<>> license{{.value = "MIT License...", .rows = 4,
                                         .read_only = true, .font = render::FontRole::Ui}};

    void view(app::ViewBuilder& vb) { vb.vstack(body, license); }
};
```

The [text field page](text-field.md) shows a text area in its screenshot.

## `ui::TextArea<T>`

| Member | Type | Default | Meaning |
|---|---|---|---|
| `value` | `T` | `""` | the text; lines are apart by `'\n'` |
| `placeholder` | `std::string` | `""` | shown greyed while it is empty |
| `max_length` | `std::size_t` | `0` | the most characters it takes; `0`: no limit |
| `rows` | `std::size_t` | `6` | how many lines tall it is |
| `highlights` | `std::vector<ui::TextRange>` | none | byte ranges drawn behind the text, such as search hits |
| `read_only` | `bool` | `false` | the user can select and copy, not change |
| `font` | `render::FontRole` | `Mono` | `Mono` for code, `Ui` for prose |

A `ui::TextRange` is `{begin, end}`, in bytes of `value`.

## Using it

Everything a [text field](text-field.md) does, plus:

- Enter starts a new line. Up and Down move between lines.
- A paragraph that starts in Hebrew or Arabic is right-aligned, and its wrapped lines keep its
  direction.
- Every edit notifies.

## Text area or document?

A text area keeps its whole text in one string, and lays out every line.
That is right for a form. For a file, a log or a long note, use a `text::Document` with
[a text view or an editor](../text.md): it lays out only what is on screen, so a
100,000-line file types as fast as a ten-line one.

## See also

- [Text field](text-field.md), for one line.
- [Text and code](../text.md), for documents, code and Markdown.
