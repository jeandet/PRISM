// Document: Markdown on the left, rendered on the right as you type. Both sides are a
// text::Document: the source in vb.text_editor, its rendering in vb.text_view. Each lays out only
// what is in sight, so a long document types and scrolls as fast as this one.
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
    prism::text::Document source{sample_markdown};
    prism::text::Document rendered;
    prism::core::Wiring wire;   // captures `this`: the tab never moves

    DocumentTab();
    void view(prism::app::ViewBuilder& vb);   // document_tab.cpp
};

} // namespace gallery
