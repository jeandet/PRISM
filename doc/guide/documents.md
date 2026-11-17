# Documents, code and Markdown

A `TextArea` holds its text in a field, fine for a form. A long text wants a `text::Document`:
blocks (a line of code, a paragraph, a heading, a list item, a table row) with styled spans,
edited in place, observed and undone. Its views lay out only what is in sight, so typing in a
100,000-line file costs what it costs in a ten-line one: 0.01 ms a key, against a second for
`TextArea` (`benchmarks/text_scale.cpp`). Try it in the gallery's Code tab, in the browser.

```cpp
text::Document source{load("main.cpp")};
text::Document notes = text::markdown(readme);   // GitHub-flavoured Markdown, opt-in (-Dmarkdown)

vb.code_editor(source, "cpp");                   // colors, line numbers, brackets, Tab to indent
vb.code_view(snippet, "cpp");                    // the same colors, read-only, for code in a page
vb.text_editor(notes, {.rich = true});           // Ctrl+B, "# " for a heading, "- " for a list
vb.text_view(notes);                             // read-only
save("README.md", text::to_markdown(notes));     // and back, tables included
```

- **Code:** C/C++, Python, SQL, JSON, TOML and shell are built in (`text::language("python")`),
  as rules written as data; another engine (tree-sitter, a language server) implements one
  `Highlighter` function. Colors follow the theme. A minimap shows the whole file in a column on
  the right: drag its band to scroll, or click elsewhere in it to jump there. Long lines scroll sideways; highlighting goes
  on ahead between frames, so a jump to the end of a long file finds it done.
- **Editing:** selection, clipboard, undo that puts the caret back at the change, input methods,
  matching brackets, auto-indent; `.rows(10)` makes an editor ten lines tall inside a scroll area.
- **Completion:** typing a word, or Ctrl+Space, opens a list under the caret, ranked as in VS
  Code (`gw` finds `get_widget`). A code editor completes its language's keywords and the file's
  words; any editor takes a list or a function: `.complete(table_names)`,
  `.complete([&](const text::Document& d, text::Position at) { ... })`.
- **Find and replace:** `text::find_all(doc, "word")` and `replace_all` (one undo step). Ranges the
  app highlights, such as search hits or an error's position, go in a field: `.highlights(hits)`.
- **Rich text:** headings, lists, quotes, code blocks, tables, bold, italic and links, in real Noto
  Sans Bold and Italic faces; Markdown shortcuts as you type.
- **Python:** `prism.document(text, markdown=False)` with the same three `vb` calls
  (`python/examples/19_markdown_notes.py`).

How it works, and which current editors each algorithm comes from: `doc/design/text-stack.md`.
