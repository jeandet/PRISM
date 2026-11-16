#pragma once

#include <prism/text/document.hpp>

#include <string_view>

// GitHub-flavoured Markdown into a text::Document, parsed by cmark-gfm (GitHub's CommonMark
// implementation, with tables, strikethrough, autolinks and task lists). Opt-in like HTTP: built
// as prism-markdown when meson finds or fetches cmark-gfm (-Dmarkdown=auto).
//
//   auto doc = text::markdown(readme);
//   vb.text_view(doc);
//
// Headings, paragraphs, quotes, list items (nested, numbered, task boxes), code block lines (with
// their language), rules and table rows each become a block; emphasis, strong, code, links and
// strikethrough become spans.
// simplify: a hard line break inside a paragraph is a space, a table row is one line of cells,
// an image shows its alt text. Each when a document needs it.
namespace prism::text {

[[nodiscard]] Document markdown(std::string_view source);

} // namespace prism::text
