#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/event_routing.hpp>
#include <prism/app/widget_tree.hpp>

using namespace prism;
using namespace prism::core;
using namespace prism::input;
using namespace prism::render;
using namespace prism::ui;
using prism::app::WidgetTree;
using prism::app::widget_detail::route_event;

TEST_CASE("parse_mnemonic: & marks the letter, && is a literal &") {
    auto m = menu_detail::parse_mnemonic("&File");
    CHECK(m.text == "File");
    CHECK(m.underline == 0);
    CHECK(menu_detail::parse_mnemonic("Fi&le").underline == 2);
    auto amp = menu_detail::parse_mnemonic("Save && Quit");
    CHECK(amp.text == "Save & Quit");
    CHECK(amp.underline == std::string::npos);
    CHECK(menu_detail::parse_mnemonic("Plain").text == "Plain");
    CHECK(menu_detail::parse_mnemonic("Trailing&").text == "Trailing&");
    CHECK(menu_detail::mnemonic_key("&File") == keys::f);
    CHECK(menu_detail::mnemonic_key("E&xit") == keys::x);
    CHECK(menu_detail::mnemonic_key("Plain") == 0);
}

namespace {
struct Doc {
    Field<Menu> file{{.title = "&File", .items = {"&Open", "&Save", "Plain"}}};
    Field<Menu> edit{{.title = "&Edit", .items = {"&Copy"}}};
    Field<TextField<>> name{{}};
    Field<Dialog> ask{{.title = "Sure?", .message = "", .buttons = {"Yes", "No"}}};
    Field<ContextMenu> ctx{{.items = {"&Select all"}}};
    void view(WidgetTree::ViewBuilder& vb) {
        vb.vstack([&] {
            vb.hstack(file, edit);
            vb.widget(name);
            vb.dialog(ask);
            vb.context_menu(ctx);
        });
    }
};

struct Fixture {
    Doc model;
    WidgetTree tree{model};
    std::unique_ptr<SceneSnapshot> snap = tree.build_snapshot(400, 300, 1);
    void frame() { snap = tree.build_snapshot(400, 300, 2); }
    void key(int32_t k, uint16_t m = 0) { route_event(tree, snap.get(), KeyPress{k, m}); }
    WidgetId id_of(int i) const { return tree.leaf_ids()[static_cast<std::size_t>(i)]; }
};

bool draws_text(const DrawList& dl, std::string_view s) {
    for (auto& c : dl.commands)
        if (auto* t = std::get_if<TextCmd>(&c); t && t->text == s) return true;
    return false;
}
}

TEST_CASE("Alt+letter opens the menu marked with it, wherever the focus is") {
    Fixture f;
    f.key(keys::tab);   // focus somewhere
    f.key(keys::e, mods::alt);
    CHECK(f.tree.popup_owner() == f.id_of(1));
    f.key(keys::f, mods::alt);   // another menu's letter switches to it
    CHECK(f.tree.popup_owner() == f.id_of(0));
}

TEST_CASE("Alt with a letter no menu has does nothing") {
    Fixture f;
    f.key(keys::x, mods::alt);
    CHECK(f.tree.popup_owner() == 0);
}

TEST_CASE("in an open menu, an item's letter chooses it") {
    Fixture f;
    f.key(keys::f, mods::alt);
    f.frame();
    f.key(keys::s);
    CHECK(f.tree.popup_owner() == 0);
    CHECK(f.model.file.get().chosen == std::optional<std::size_t>{1});
}

TEST_CASE("Alt+letter does nothing while a modal dialog is open") {
    Fixture f;
    auto d = f.model.ask.get();
    d.open = true;
    f.model.ask.set(d);
    f.frame();
    auto dialog = f.tree.popup_owner();
    REQUIRE(dialog != 0);
    f.key(keys::f, mods::alt);
    CHECK(f.tree.popup_owner() == dialog);
}

TEST_CASE("labels are drawn without the &") {
    Fixture f;
    f.key(keys::f, mods::alt);
    f.frame();
    auto* file = f.tree.popup_node();
    REQUIRE(file);
    CHECK(draws_text(file->draws, "File"));
    CHECK(draws_text(file->overlay_draws, "Save"));
    CHECK_FALSE(draws_text(file->overlay_draws, "&Save"));
}

TEST_CASE("the letter that chose an item is not also typed into the focused field") {
    Fixture f;
    auto r = find_widget_rect(*f.snap, f.id_of(2));
    REQUIRE(r);
    Point in_name{r->origin.x + DX{5}, r->origin.y + DY{5}};
    route_event(f.tree, f.snap.get(), MouseButton{in_name, buttons::left, true});
    route_event(f.tree, f.snap.get(), MouseButton{in_name, buttons::left, false});
    REQUIRE(f.tree.focused_id() == f.id_of(2));
    route_event(f.tree, f.snap.get(), MouseButton{in_name, buttons::right, true});
    REQUIRE(f.tree.popup_owner() != 0);
    f.frame();
    f.key(keys::s);                                          // SDL sends the key...
    route_event(f.tree, f.snap.get(), TextInput{"s"});      // ...then the text
    CHECK(f.model.ctx.get().chosen == std::optional<std::size_t>{0});
    CHECK(f.model.name.get().value.empty());
    route_event(f.tree, f.snap.get(), TextInput{"t"});      // later typing still works
    CHECK(f.model.name.get().value == "t");
}

TEST_CASE("menu_text gives a label as shown, for comparing a chosen item") {
    CHECK(menu_text("&Save") == "Save");
    CHECK(menu_text("Save && Quit") == "Save & Quit");
}

TEST_CASE("a non-letter key in a popup does not drop text that arrives later on its own") {
    Fixture f;
    auto r = find_widget_rect(*f.snap, f.id_of(2));
    REQUIRE(r);
    Point in_name{r->origin.x + DX{5}, r->origin.y + DY{5}};
    route_event(f.tree, f.snap.get(), MouseButton{in_name, buttons::left, true});
    route_event(f.tree, f.snap.get(), MouseButton{in_name, buttons::left, false});
    route_event(f.tree, f.snap.get(), MouseButton{in_name, buttons::right, true});
    REQUIRE(f.tree.popup_owner() != 0);
    f.frame();
    f.key(keys::escape);                                    // closes the menu
    route_event(f.tree, f.snap.get(), TextInput{"é"});      // an input method commits text, no key
    CHECK(f.model.name.get().value == "é");
}

namespace {
struct SameLetter {
    Field<Menu> first{{.title = "&File", .items = {"x"}}};
    Field<Menu> second{{.title = "&Format", .items = {"y"}}};
    void view(WidgetTree::ViewBuilder& vb) { vb.hstack(first, second); }
};
}

TEST_CASE("two menus with the same letter: the first in the bar opens") {
    for (int round = 0; round < 5; ++round) {   // the old lookup depended on hash order
        SameLetter m;
        WidgetTree tree{m};
        auto snap = tree.build_snapshot(400, 300, 1);
        route_event(tree, snap.get(), KeyPress{keys::f, mods::alt});
        CHECK(tree.popup_owner() == tree.leaf_ids()[0]);
    }
}

TEST_CASE("a mnemonic whose key cannot trigger it is not underlined") {
    DrawList dl;
    menu_detail::draw_label(dl, "&Über", Point{X{0}, Y{0}}, Color::rgba(255, 255, 255), nullptr);
    for (auto& c : dl.commands) CHECK_FALSE(std::holds_alternative<Line>(c));
    DrawList ok;
    menu_detail::draw_label(ok, "&File", Point{X{0}, Y{0}}, Color::rgba(255, 255, 255), nullptr);
    bool underlined = false;
    for (auto& c : ok.commands) underlined |= std::holds_alternative<Line>(c);
    CHECK(underlined);
}
