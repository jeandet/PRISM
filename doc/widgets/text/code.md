# Code and syntax colors

A code editor colors its text by language: keywords, types, strings, numbers, comments.

```cpp
struct Query {
    text::Document sql{"SELECT name, total FROM orders WHERE total > 100;"};

    void view(app::ViewBuilder& vb) { vb.code_editor(sql, "sql"); }
};
```

The [Text and code page](../text.md) shows a C++ editor.

## Built-in languages

`text::language(name)` returns a built-in language by its name or a common alias:
| Language | Names |
|---|---|
| C and C++ | `"cpp"`, `"c++"`, `"cxx"`, `"hpp"`, `"c"`, `"h"` |
| Python | `"python"`, `"py"` |
| SQL | `"sql"`, `"sqlite"` |
| JSON | `"json"` |
| TOML and INI | `"toml"`, `"ini"` |
| Shell | `"shell"`, `"sh"`, `"bash"`, `"zsh"` |

A name it does not know gives a language with no rules: plain text.

The languages are rules written as data: a list of keywords, a list of types, what a comment
and a string look like. A word followed by `(` is a function call.
Colors follow the theme, so a dark theme never gets a color it cannot show.

After an edit, only the lines whose coloring changed are colored again: typing usually costs
one line. Coloring goes on ahead between frames, so a jump to the end of a long file finds it
done.

## Your own highlighter

A highlighter is one function: it takes a line and the state the line before ended in
(a comment or a string still open), and adds the line's spans.

```cpp
struct Shouting final : text::Highlighter {
    State highlight(std::string_view line, State before, std::vector<text::Span>& out) const override {
        for (std::size_t i = 0; i < line.size(); ++i)
            if (std::isupper(static_cast<unsigned char>(line[i])))
                out.push_back({.begin = i, .end = i + 1, .style = text::Style::keyword});
        return before;   // nothing stays open across lines
    }
};

Shouting shouting;   // must outlive the editor
// in view():
vb.text_editor(doc, {.view = {.family = render::FontRole::Mono, .highlighter = &shouting}});
```

A tree-sitter grammar or a language server's semantic tokens fit the same function.

You can also build a `text::Language` with your own lists:

| Member | Meaning |
|---|---|
| `name` | the language's name |
| `keywords`, `types` | words to color |
| `case_insensitive` | keywords match in any case (the lists are lowercase) |
| `line_comment` | `"//"`, `"#"`, `"--"` |
| `block_open`, `block_close` | `"/*"` and `"*/"` |
| `quotes` | the characters that open a one-line string |
| `long_string` | a string that may span lines, such as Python's `"""` |
| `preprocessor` | a line starting with `#` is a directive |
| `calls` | a word followed by `(` is a function |
| `keys` | JSON: a string followed by `:` is a key |
| `sections` | TOML and INI: `[a section]`, and a key before `=` |

## See also

- [Views and editors](views-and-editors.md), [Completion](completion.md).
