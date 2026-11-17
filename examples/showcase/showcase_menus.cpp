#include "showcase_common.hpp"

using namespace prism;

struct MenuShowcase {
    core::Field<ui::Menu> file{{.title = "&File", .items = {
        {.text = "&Open…", .shortcut = {input::keys::o, input::mods::ctrl}},
        {.text = "&Save", .shortcut = {input::keys::s, input::mods::ctrl}},
        {.separator = true},
        {.text = "&Word wrap", .checked = true},
    }}};
    core::Field<ui::Menu> edit{{.title = "&Edit", .items = {{"Undo"}, {"Redo"}}}};
    core::Field<ui::Button> open{{.text = "Open", .primary = false}};
    core::Field<ui::Button> bold{{.text = "Bold", .checked = false}};
    core::Field<ui::TextArea<>> body{{.value = "Alt+F opens the File menu.", .rows = 3}};
    core::Field<ui::Label<>> status{{"Ready"}};

    void view(app::ViewBuilder& vb) {
        vb.hstack([&] { vb.widget(file); vb.widget(edit); vb.spacer(); });
        vb.toolbar([&] { vb.widget(open); vb.widget(bold); vb.spacer(); });
        vb.widget(body);
        vb.status_bar([&] { vb.widget(status); vb.spacer(); });
    }
};

int main(int argc, char* argv[]) {
    MenuShowcase model;
    return showcase(argc, argv, model, 420, 200);
}
