// Code: vb.code_editor(doc, language) for each built-in language, a find and replace bar over it,
// and a button that opens a 100,000-line file to show that typing and scrolling stay instant.
#pragma once

#include <prism/prism.hpp>

#include <array>
#include <string_view>
#include <vector>

namespace gallery {

struct CodeSample {
    std::string_view tab;
    std::string_view language;   // text::language's name
    std::string_view code;
};

inline constexpr std::array<CodeSample, 6> code_samples{{
    {"C++", "cpp", R"(// A model is a struct; the window builds itself from it.
#include <prism/prism.hpp>
using namespace prism;

struct Settings {
    core::Field<ui::Slider<>> volume{{.value = 0.6, .label = "Volume"}};
    core::Field<ui::Checkbox> mute{{.label = "Mute"}};
    /* a value computed from the others,
       kept up to date for you */
    core::Derived<std::string> summary{[this] {
        return value(mute) ? "muted" : std::to_string(int(value(volume) * 100)) + " %";
    }};
};

int main() {
    Settings settings;
    return app::run(settings, {.title = "Settings", .width = 420, .height = 160});
}
)"},
    {"Python", "python", R"(import prism


class Notes(prism.Model):
    """Markdown on the left, rendered on the right."""
    source = prism.document("# Notes\n")
    rendered = prism.document("", markdown=True)

    def __init__(self):
        super().__init__()
        self.source.observe(self.render)

    def render(self):
        self.rendered.set_text(self.source.text, markdown=True)

    def view(self, vb):
        vb.hstack(lambda: (vb.code_editor(self.source), vb.text_view(self.rendered)))


if __name__ == "__main__":
    prism.run(Notes(), title="Notes")
)"},
    {"SQL", "sql", R"(-- The busiest hours, and what a ride costs then
SELECT strftime('%H:00', pickup) AS hour,
       COUNT(*)                  AS trips,
       ROUND(AVG(fare), 2)       AS mean_fare
  FROM trips
 WHERE km > 0.5
 GROUP BY hour
HAVING trips > 1000
 ORDER BY trips DESC
 LIMIT 10;
)"},
    {"JSON", "json", R"({
  "name": "prism",
  "version": 1.5,
  "editors": ["text", "code", "rich"],
  "highlighting": {
    "languages": 6,
    "incremental": true,
    "engine": null
  }
}
)"},
    {"TOML", "toml", R"(# What the gallery remembers between runs
[window]
width = 1000
height = 700
maximized = false

[gallery]
theme = "System"   # Light, Dark or System
last_tab = 4
)"},
    {"Shell", "shell", R"(#!/bin/sh
# Build PRISM and run the gallery
set -e
meson setup builddir
ninja -C builddir
if [ "$1" = "--web" ]; then
    echo "building the web page"
    scripts/build_web_site.sh
fi
./builddir/examples/widget_gallery/widget_gallery
)"},
}};

struct CodeTab {
    prism::core::Field<prism::ui::TabBar<>> languages;
    std::array<prism::text::Document, code_samples.size()> docs{
        prism::text::Document{code_samples[0].code}, prism::text::Document{code_samples[1].code},
        prism::text::Document{code_samples[2].code}, prism::text::Document{code_samples[3].code},
        prism::text::Document{code_samples[4].code}, prism::text::Document{code_samples[5].code}};
    std::array<prism::core::Field<std::vector<prism::text::Range>>, code_samples.size()> hits{};
    prism::core::Field<prism::ui::TextField<>> find{{.placeholder = "Find"}};
    prism::core::Field<prism::ui::TextField<>> replacement{{.placeholder = "Replace with"}};
    prism::core::Field<prism::ui::Checkbox> match_case{{.checked = false, .label = "Match case"}};
    prism::core::Field<prism::ui::Button> replace_all{{.text = "Replace all", .primary = false}};
    prism::core::Field<prism::ui::Button> big_file{{.text = "Open a 100,000-line file", .primary = false}};
    prism::core::Field<prism::ui::Label<>> status{{""}};
    prism::core::Wiring wire;   // captures `this`: the tab never moves

    CodeTab();
    void view(prism::app::ViewBuilder& vb);   // code_tab.cpp

private:
    std::size_t current() const;
    void search();
};

} // namespace gallery
