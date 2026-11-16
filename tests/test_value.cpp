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
