// Text: a code editor, prose in many scripts (Hebrew and Arabic mixed with English, read right to
// left), a right-to-left field, and read-only text that can still be selected and copied.
#pragma once

#include <prism/prism.hpp>

namespace gallery {

inline constexpr const char* sample_code = R"(// Edit me: Ctrl+Z undoes, Ctrl+A selects all.
struct Settings {
    core::Field<ui::Slider<>> volume{{.value = 0.6}};
    core::Field<ui::Checkbox> mute{{.label = "Mute"}};
};
// The window builds itself from the struct.
)";

inline constexpr const char* sample_prose =
    "Latin: café, naïve, Ångström, ﬁligree.\n"
    "Greek: Καλημέρα κόσμε. Cyrillic: Привет, мир.\n"
    "Hebrew: \xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d \xd7\xa2\xd7\x95\xd7\x9c\xd7\x9d (shalom olam).\n"
    "Arabic: \xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7 \xd8\xa8\xd8\xa7\xd9\x84\xd8\xb9\xd8\xa7\xd9\x84\xd9\x85 (marhaban bil-alam).\n"
    "Mixed: the word \xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d means peace; \xd8\xb3\xd9\x84\xd8\xa7\xd9\x85 too.\n"
    "Combining marks: e\xcc\x81 is one character for the caret and Backspace.";

struct TextTab {
    prism::core::Field<prism::ui::Label<>> title{{.value = "Text", .heading = true}};
    prism::core::Field<prism::ui::Label<>> code_label{{"Code (JetBrains Mono): editable, undo, search highlights"}};
    prism::core::Field<prism::ui::TextArea<>> code{{.value = sample_code, .rows = 7}};
    prism::core::Field<prism::ui::Label<>> prose_label{{"Prose (Noto Sans): graphemes, line breaks, right-to-left runs"}};
    prism::core::Field<prism::ui::TextArea<>> prose{{.value = sample_prose, .rows = 6,
                                                     .font = prism::render::FontRole::Ui}};
    prism::core::Field<prism::ui::TextField<>> rtl{{.value = "\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d world",
                                                    .placeholder = "Type Hebrew or Arabic"}};
    prism::core::Field<prism::ui::TextArea<>> notice{{
        .value = "Read-only: select and copy this text, but you cannot change it.",
        .rows = 1, .read_only = true, .font = prism::render::FontRole::Ui}};

    void view(prism::app::WidgetTree::ViewBuilder& vb) {
        vb.widget(title);
        vb.widget(code_label);
        vb.widget(code).tooltip("Monospace, with a scrollbar when it grows");
        vb.widget(prose_label);
        vb.widget(prose).tooltip("Click inside the Hebrew word: the caret follows the right-to-left run");
        vb.widget(rtl).tooltip("A single-line field with mixed directions");
        vb.widget(notice);
    }
};

} // namespace gallery
