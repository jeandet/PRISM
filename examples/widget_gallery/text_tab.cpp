#include "text_tab.hpp"

namespace gallery {

void TextTab::view(prism::app::ViewBuilder& vb) {
    vb.widget(title);
    vb.widget(code_label);
    vb.code_editor(code, "cpp").rows(8).tooltip("A text::Document: only the lines in sight are laid out");
    vb.widget(prose_label);
    vb.widget(prose).tooltip("Click inside the Hebrew word: the caret follows the right-to-left run");
    vb.widget(rtl).tooltip("A single-line field with mixed directions");
    vb.widget(notice);
}

} // namespace gallery
