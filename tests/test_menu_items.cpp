// Menu items beyond a label: keyboard shortcuts, disabled items, separators and check items.
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

namespace {
constexpr Shortcut ctrl_s{keys::s, mods::ctrl};

struct App {
    Field<bool> menus_on{true};
    Field<Menu> file{{.title = "File", .items = {
        {.text = "Open"},
        {.text = "Save", .shortcut = ctrl_s},
        {.separator = true},
        {.text = "Copy path", .shortcut = {keys::c, mods::ctrl}},
        {.text = "Revert", .shortcut = {keys::z, mods::ctrl | mods::shift}, .enabled = false},
        {.text = "Word wrap", .checked = false},
    }}};
    Field<TextField<>> editor{{}};
    Field<Button> ok{{"OK"}};
    Field<Dialog> confirm{{.title = "Sure?", .message = "", .buttons = {"Yes", "No"}}};
    void view(WidgetTree::ViewBuilder& vb) {
        vb.vstack([&] {
            vb.widget(file).enabled(menus_on);
            vb.widget(editor);
            vb.widget(ok);
            vb.dialog(confirm);
        });
    }
};

struct Fixture {
    App model;
    WidgetTree tree{model};
    std::unique_ptr<SceneSnapshot> snap = tree.build_snapshot(500, 400, 1);
    uint64_t v = 1;
    std::vector<WidgetId> ids = tree.leaf_ids();   // file, editor, ok
    void frame() { snap = tree.build_snapshot(500, 400, ++v); }
    void key(int32_t k, uint16_t m = 0) { route_event(tree, snap.get(), KeyPress{k, m}); }
    void click(float x, float y) {
        route_event(tree, snap.get(), MouseButton{Point{X{x}, Y{y}}, buttons::left, true});
        route_event(tree, snap.get(), MouseButton{Point{X{x}, Y{y}}, buttons::left, false});
    }
    const Menu& menu() const { return model.file.get(); }
    bool open() const { return tree.popup_owner() == ids[0]; }
    bool overlay_has(std::string_view text) const {
        for (auto& c : snap->overlay.commands)
            if (auto* t = std::get_if<TextCmd>(&c); t && t->text == text) return true;
        return false;
    }
};
}

TEST_CASE("a shortcut reads as people write it") {
    CHECK(shortcut_text(ctrl_s) == "Ctrl+S");
    CHECK(shortcut_text({keys::s, mods::ctrl | mods::shift}) == "Ctrl+Shift+S");
    CHECK(shortcut_text({keys::f5, 0}) == "F5");
    CHECK(shortcut_text({keys::delete_, 0}) == "Delete");
    CHECK(shortcut_text({'1', mods::alt}) == "Alt+1");
    CHECK(shortcut_text({}) == "");
}

TEST_CASE("Ctrl+S matches Ctrl or Cmd, and only with the same Shift and Alt") {
    CHECK(matches(ctrl_s, KeyPress{keys::s, mods::ctrl}));
    CHECK(matches(ctrl_s, KeyPress{keys::s, mods::gui}));          // Cmd on macOS
    CHECK(matches(ctrl_s, KeyPress{keys::s, 0x0040}));             // left Ctrl alone
    CHECK_FALSE(matches(ctrl_s, KeyPress{keys::s, mods::ctrl | mods::shift}));
    CHECK_FALSE(matches(ctrl_s, KeyPress{keys::s, 0}));
    CHECK_FALSE(matches(ctrl_s, KeyPress{keys::a, mods::ctrl}));
    CHECK_FALSE(matches(Shortcut{}, KeyPress{0, 0}));
}

TEST_CASE("a shortcut chooses its item from anywhere, without opening the menu") {
    Fixture f;
    f.tree.set_focused(f.ids[2]);   // the OK button
    f.key(keys::s, mods::ctrl);
    CHECK(f.menu().chosen == std::optional<std::size_t>{1});
    CHECK(f.menu().activations == 1);
    CHECK_FALSE(f.open());
    CHECK(f.model.ok.get().click_count == 0);
}

TEST_CASE("a text field keeps its editing keys; other shortcuts still reach the menu") {
    Fixture f;
    f.tree.set_focused(f.ids[1]);   // the text field
    f.key(keys::c, mods::ctrl);     // copy: the field's
    CHECK(f.menu().activations == 0);
    f.key(keys::s, mods::ctrl);     // save: the menu's
    CHECK(f.menu().chosen == std::optional<std::size_t>{1});
}

TEST_CASE("the same Ctrl+C reaches the menu when no text widget has the focus") {
    Fixture f;
    f.tree.set_focused(f.ids[2]);
    f.key(keys::c, mods::ctrl);
    CHECK(f.menu().chosen == std::optional<std::size_t>{3});
}

TEST_CASE("a disabled item ignores its shortcut, clicks and the keyboard") {
    Fixture f;
    f.key(keys::z, mods::ctrl | mods::shift);
    CHECK(f.menu().activations == 0);
    f.click(10, 10);
    REQUIRE(f.open());
    // rows: Open 30..58, Save 58..86, separator 86..95, Copy path 95..123, Revert 123..151
    f.click(10, 137);
    CHECK(f.open());
    CHECK(f.menu().activations == 0);
    f.key(keys::down);   // Save
    f.key(keys::down);   // skips the separator: Copy path
    f.key(keys::down);   // skips Revert: Word wrap
    f.key(keys::enter);
    CHECK(f.menu().chosen == std::optional<std::size_t>{5});
}

TEST_CASE("a separator is a thin line that cannot be chosen") {
    Fixture f;
    f.click(10, 10);
    REQUIRE(f.open());
    f.click(10, 90);
    CHECK(f.open());
    CHECK(f.menu().activations == 0);
    f.click(10, 109);   // Copy path, just below the separator
    CHECK(f.menu().chosen == std::optional<std::size_t>{3});
}

TEST_CASE("choosing a check item flips its mark") {
    Fixture f;
    f.click(10, 10);
    f.click(10, 165);   // Word wrap: 151..179
    CHECK(f.menu().chosen == std::optional<std::size_t>{5});
    CHECK(f.menu().items[5].checked == std::optional<bool>{true});
    f.click(10, 10);
    f.click(10, 165);
    CHECK(f.menu().items[5].checked == std::optional<bool>{false});
}

TEST_CASE("an open menu shows each item's shortcut") {
    Fixture f;
    f.click(10, 10);
    f.frame();
    CHECK(f.overlay_has("Ctrl+S"));
    CHECK(f.overlay_has("Ctrl+Shift+Z"));
}

TEST_CASE("a disabled menu's shortcuts do nothing") {
    Fixture f;
    f.model.menus_on.set(false);
    f.key(keys::s, mods::ctrl);
    CHECK(f.menu().activations == 0);
}

TEST_CASE("shortcuts wait while a dialog is open") {
    Fixture f;
    auto d = f.model.confirm.get();
    d.open = true;
    f.model.confirm.set(d);
    f.frame();
    f.key(keys::s, mods::ctrl);
    CHECK(f.menu().activations == 0);
}

TEST_CASE("on_choice fires on choices only, not when the app changes an item") {
    Fixture f;
    std::vector<std::size_t> seen;
    auto c = on_choice(f.model.file, [&](std::size_t i) { seen.push_back(i); });
    f.tree.set_focused(f.ids[2]);
    f.key(keys::s, mods::ctrl);
    auto m = f.model.file.get();
    m.items[1].enabled = false;   // greying out Save after saving
    f.model.file.set(m);
    f.key(keys::c, mods::ctrl);
    CHECK(seen == std::vector<std::size_t>{1, 3});
}
