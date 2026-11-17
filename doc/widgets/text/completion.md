# Completion

While the user types a word, an editor can show a list of what could go there.
Up and Down move in it, Enter or Tab takes the highlighted entry, Escape closes it.
Ctrl+Space opens it on demand.

A code editor completes its language's keywords and the file's words with no setup.
Any editor takes a list of words, or a function:

```cpp
struct Console {
    text::Document query;
    text::Document notes;
    std::vector<std::string> tables{"orders", "customers", "products"};

    void view(app::ViewBuilder& vb) {
        vb.code_editor(query, "sql").complete(tables);
        vb.text_editor(notes).complete([](const text::Document&, text::Position) {
            return std::vector<text::Completion>{{.label = "ada", .detail = "user"}};
        });
    }
};
```

## How it ranks

The source lists candidates and never filters them. The editor finds the word being typed,
and ranks the candidates against it, ignoring case:

- the typed characters must all appear in the candidate, in order: "gw" finds `get_widget`;
- a match at the start beats one at a later word's start, which beats one inside a word;
- consecutive characters and the same case count a little more.

So every editor matches the same way, whatever the source.

## `text::Completion`

| Member | Type | Meaning |
|---|---|---|
| `label` | `std::string` | what the list shows, and what typing is matched against |
| `insert` | `std::string` | what replaces the word; empty: the label |
| `detail` | `std::string` | shown muted on the right: "keyword", a type, a signature |

A `text::Completer` is `std::function<std::vector<text::Completion>(const text::Document&, text::Position)>`:
it gets the document and the caret.

## Ready-made sources

| Function | Lists |
|---|---|
| `text::from_list(words)` | the words; what `.complete(vector)` uses |
| `text::document_words(language)` | the document's words near the caret, and the language's keywords |

Include `<prism/text/complete.hpp>`.

## See also

- [Combo box](../controls/combo-box.md): suggestions in a one-line field.
