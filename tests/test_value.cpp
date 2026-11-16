// prism::value(x): what a field, derived or shared value holds, unwrapped from its widget.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/derived.hpp>
#include <prism/core/field.hpp>
#include <prism/core/shared.hpp>
#include <prism/core/value.hpp>
#include <prism/ui/delegate.hpp>

#include <string>
#include <type_traits>

using namespace prism;

TEST_CASE("a widget's main member: a slider's number, a text field's text, a check") {
    core::Field<ui::Slider<>> volume{{.value = 0.6}};
    core::Field<ui::TextField<>> name{{.value = "Ada"}};
    core::Field<ui::Checkbox> mute{{.checked = true, .label = "Mute"}};
    CHECK(value(volume) == 0.6);
    CHECK(value(name) == "Ada");
    CHECK(value(mute));
    static_assert(std::is_same_v<decltype(value(name)), const std::string&>);
}

TEST_CASE("a toggle button reads as on or off, never as 'is a toggle'") {
    core::Field<ui::Button> power{{.text = "Power", .checked = false}};
    CHECK_FALSE(value(power));
    core::Field<ui::Button> plain{{.text = "Go"}};
    CHECK_FALSE(value(plain));
}

TEST_CASE("a plain value is itself; Derived and Shared work too") {
    core::Field<int> count{3};
    core::Derived<int> twice{[&] { return 2 * value(count); }};
    core::Shared<std::string> latest{"x"};
    CHECK(value(count) == 3);
    CHECK(value(twice) == 6);
    CHECK(value(latest) == "x");
    static_assert(std::is_same_v<decltype(value(latest)), std::string>);   // a copy, not a dangling ref
    count.set(5);
    CHECK(value(twice) == 10);   // value() is a read the Derived follows
}

TEST_CASE("set_value changes the main member and keeps every other one") {
    core::Field<ui::Slider<>> volume{{.value = 0.6, .min = 0.0, .max = 2.0, .label = "Volume"}};
    set_value(volume, 1.5);
    CHECK(value(volume) == 1.5);
    CHECK(volume.get().max == 2.0);
    CHECK(volume.get().label == "Volume");

    core::Field<ui::TextField<>> name{{.value = "", .placeholder = "Your name"}};
    set_value(name, "Ada");
    CHECK(value(name) == "Ada");
    CHECK(name.get().placeholder == "Your name");
}

TEST_CASE("set_value on a check, a toggle and a plain value") {
    core::Field<ui::Checkbox> mute{{.checked = false, .label = "Mute"}};
    set_value(mute, true);
    CHECK(value(mute));
    CHECK(mute.get().label == "Mute");

    core::Field<ui::Button> power{{.text = "Power", .checked = false}};
    set_value(power, true);
    CHECK(value(power));

    core::Field<int> count{1};
    set_value(count, 7);
    CHECK(value(count) == 7);
}

TEST_CASE("update changes several members in one change, and none when nothing changed") {
    core::Field<ui::Slider<>> volume{{.value = 0.6, .label = "Volume"}};
    int changes = 0;
    volume.observe([&](const ui::Slider<>&) { ++changes; });
    volume.update([](auto& s) { s.value = 0.2; s.label = "Gain"; });
    CHECK(changes == 1);
    CHECK(value(volume) == 0.2);
    CHECK(volume.get().label == "Gain");
    volume.update([](auto& s) { s.value = 0.2; });
    CHECK(changes == 1);
}

template <typename T>
concept can_set_value = requires(T& t) { set_value(t, 2); };
template <typename T>
concept can_update = requires(T& t) { t.update([](int&) {}); };

TEST_CASE("a computed value cannot be set; a wrong type does not compile either") {
    static_assert(!can_set_value<core::Derived<int>>);
    static_assert(!can_update<core::Derived<int>>);
    static_assert(can_set_value<core::Field<int>>);
    static_assert(!core::SettableTo<core::Field<ui::TextField<>>, int>);
    core::Derived<int> computed{[] { return 1; }};
    CHECK(value(computed) == 1);
}

TEST_CASE("show() opens a dialog with no answer yet, and keeps what it says") {
    core::Field<ui::Dialog> about{{.title = "About", .message = "PRISM", .buttons = {"OK"}, .chosen = 0}};
    show(about);
    CHECK(about.get().open);
    CHECK_FALSE(about.get().chosen.has_value());   // last time's answer is not this time's
    CHECK(about.get().title == "About");
}

TEST_CASE("show() opens a popup and keeps it non-modal") {
    core::Field<ui::Popup> options{{.open = false, .modal = false}};
    show(options);
    CHECK(options.get().open);
    CHECK_FALSE(options.get().modal);
}
