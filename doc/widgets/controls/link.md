# Link

A link is text that opens a web address in the user's browser.

```cpp
struct About {
    core::Field<ui::Link> docs{{.text = "Read the docs", .url = "https://prism-gui.readthedocs.io"}};

    void view(app::ViewBuilder& vb) { vb.widget(docs); }
};
```

The [label page](label.md) shows a link in its screenshot.

## `ui::Link`

| Member | Type | Default | Meaning |
|---|---|---|---|
| `text` | `std::string` | `""` | what it shows |
| `url` | `std::string` | `""` | the address it opens; empty: none |
| `click_count` | `std::uint64_t` | `0` | counts the clicks, URL or not; each click notifies |

A link with no URL is the app's to act on, as a [button](button.md) is:
listen to it and do what it says.

## Using it

- A click, or Enter while it has the focus, opens the URL and counts the click.

## See also

- [Button](button.md), [Label](label.md).
