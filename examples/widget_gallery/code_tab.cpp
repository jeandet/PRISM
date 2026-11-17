#include "code_tab.hpp"

#include <prism/text/find.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <string>

namespace gallery {

namespace {
// A long C++ file, to show that a code editor's cost does not grow with the text.
std::string long_file(std::size_t lines) {
    std::string text = "// 100,000 lines: type anywhere, scroll, Ctrl+End -- each costs what it costs in ten.\n";
    text.reserve(lines * 48);
    for (std::size_t i = 1; i < lines; ++i)
        text += fmt::format("int value_{} = compute(input[{}], {}); // line {}\n", i, i % 97, i * 3, i + 1);
    return text;
}
} // namespace

CodeTab::CodeTab() {
    for (auto& doc : docs) wire.on(doc, [this](const prism::text::Change&) { search(); });
    wire.on_any([this] { search(); }, find, match_case, languages);
    wire.on(replace_all, [this](const auto&) {
        prism::text::replace_all(docs[current()], value(find), value(replacement), {.case_sensitive = value(match_case)});
    });
    wire.on(big_file, [this](const auto&) {
        languages.set({.selected = 0});
        docs[0].reset(prism::text::Document{long_file(100000)});
    });
    search();
}

std::size_t CodeTab::current() const { return std::min(languages.get().selected, docs.size() - 1); }

// The hits of the find text in the shown document (the editor draws them), and the status line.
void CodeTab::search() {
    const std::size_t i = current();
    for (std::size_t k = 0; k < docs.size(); ++k)
        hits[k].set(k == i ? prism::text::find_all(docs[k], value(find), {.case_sensitive = value(match_case)})
                           : std::vector<prism::text::Range>{});
    const std::string found = value(find).empty() ? std::string{} : fmt::format("  ·  {} matches", value(hits[i]).size());
    status.set({fmt::format("{}  ·  {} lines{}  ·  Tab indents, Ctrl+Z undoes, the caret beside a bracket shows its "
                            "match, long lines scroll sideways",
                            code_samples[i].tab, docs[i].size(), found)});
}

void CodeTab::view(prism::app::ViewBuilder& vb) {
    vb.toolbar([&] {
        vb.widget(find).tooltip("Every match is highlighted in the editor");
        vb.widget(replacement);
        vb.widget(match_case);
        vb.widget(replace_all).tooltip("One undo step: Ctrl+Z in the editor brings them all back");
        vb.spacer();
        vb.widget(big_file).tooltip("A text::Document lays out only the lines in sight");
    });
    vb.tabs(languages, [&] {
        for (std::size_t i = 0; i < code_samples.size(); ++i)
            vb.tab(std::string(code_samples[i].tab), [this, i](prism::app::ViewBuilder& t) {
                t.code_editor(docs[i], code_samples[i].language).highlights(hits[i]);
            });
    });
    vb.widget(status);
}

} // namespace gallery
