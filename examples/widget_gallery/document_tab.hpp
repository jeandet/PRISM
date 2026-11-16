// Document: Markdown on the left, rendered on the right as you type. The rendering is a
// text::Document shown by vb.text_view, which lays out only what is in sight.
#pragma once

#include <prism/prism.hpp>

namespace gallery {

inline constexpr const char* sample_markdown = R"(# A document

Write **Markdown** on the left: it is rendered *here*, as you type.
Inline `code`, ~~struck~~ text and [links](https://github.com/jeandet/PRISM) work too.

## Lists

- Blocks: headings, paragraphs, quotes, lists, code, rules
  - nested items get their own marker
- [x] task lists
- [ ] tables, as rows of cells

1. Numbered
2. lists

> A quote stays in the margin,
> drawn in a quieter color.

```cpp
struct Settings {
    core::Field<ui::Slider<>> volume{{.value = 0.6}};
};
```

| Widget | Lays out |
|---|---|
| TextArea | every line |
| TextView | what is in sight |

---

The view keeps a height map of its blocks, so a long document scrolls as fast as this one.
)";

struct DocumentTab {
    prism::core::Field<prism::ui::TextArea<>> source{{.value = sample_markdown, .rows = 30}};
    prism::text::Document rendered;
    prism::core::Wiring wire;   // captures `this`: the tab never moves

    DocumentTab();
    void view(prism::app::ViewBuilder& vb);   // document_tab.cpp
};

} // namespace gallery
