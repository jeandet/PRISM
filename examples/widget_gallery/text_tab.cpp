#include "text_tab.hpp"

namespace gallery {

void TextTab::view(prism::app::ViewBuilder& vb) {
    vb.widget(title);
    vb.widget(code_label);
    vb.widget(code).tooltip("Monospace, with a scrollbar when it grows");
    vb.widget(prose_label);
    vb.widget(prose).tooltip("Click inside the Hebrew word: the caret follows the right-to-left run");
    vb.widget(rtl).tooltip("A single-line field with mixed directions");
    vb.widget(notice);
}

} // namespace gallery
