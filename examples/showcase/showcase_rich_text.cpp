#include "showcase_common.hpp"

using namespace prism;

struct RichText {
    text::Document notes{"Release notes\n"
                         "PRISM shows documents as rich text.\n"
                         "Headings, lists and bold text\n"
                         "come from the document's blocks and spans."};

    RichText() {
        notes.set_kind(0, text::BlockKind::heading, 1);
        notes.style({{1, 0}, {1, 5}}, text::Style::strong);
        notes.set_kind(2, text::BlockKind::bullet, 0, "•");
        notes.set_kind(3, text::BlockKind::bullet, 0, "•");
    }

    void view(app::ViewBuilder& vb) { vb.text_view(notes); }
};

int main(int argc, char* argv[]) {
    RichText model;
    return showcase(argc, argv, model, 420, 170);
}
