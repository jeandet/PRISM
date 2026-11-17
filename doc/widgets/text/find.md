# Find and replace

`text::find_all` finds every match of a string in a document.
`text::replace_all` replaces them all, as one undo step.
Show the matches by handing them to the editor's `.highlights()`.

```cpp
#include <prism/text/find.hpp>   // text::find_all

struct Search {
    text::Document doc{"one two one three one"};
    core::Field<ui::TextField<>> find{{.placeholder = "Find"}};
    core::Field<std::vector<text::Range>> hits;
    core::Wiring wire;

    Search() {
        wire.on(find, [this](const auto&) { hits = text::find_all(doc, value(find)); });
    }

    void view(app::ViewBuilder& vb) {
        vb.widget(find);
        vb.code_editor(doc).highlights(hits);
    }
};
```

Include `<prism/text/find.hpp>`.

## The functions

| Function | Does |
|---|---|
| `find_all(doc, needle, options)` | every match, as `text::Range`s, in order |
| `replace_all(doc, needle, with, options)` | replaces every match as one undo step; returns how many |

`text::FindOptions`:

| Member | Type | Default | Meaning |
|---|---|---|---|
| `case_sensitive` | `bool` | `true` | `false`: "One" finds "one" |
| `whole_word` | `bool` | `false` | not inside a longer word (letters, digits and `_`) |

## Highlights

`.highlights(field)` shades the ranges in the field, and redraws as the field changes.
Any ranges work, not only search hits: an error's position from a parser, a SQL error's token.

## See also

- [The gallery's Code tab](https://jeandet.github.io/PRISM/widget_gallery.html) has a find
  and replace bar over its editor.
