#pragma once

#include <prism/text/document.hpp>

#include <string>

// A document written as Markdown: what a rich text editor saves, and what text::markdown reads
// back to the same blocks and spans. Part of libprism: writing needs no parser.
//
//   save(path, text::to_markdown(doc));
//
// Headings, paragraphs, quotes, list items (nested, numbered, task boxes), code blocks (consecutive
// code lines of one language become one fence), rules; strong, emphasis, code, strikethrough and
// links. Characters Markdown would read as markup are escaped outside code.
// simplify: a table row (text::markdown reads one as a block of cells) is written as a paragraph;
// keep tables as their own kind of block when a rich editor needs to edit them.
namespace prism::text {

[[nodiscard]] std::string to_markdown(const Document& doc);

} // namespace prism::text
