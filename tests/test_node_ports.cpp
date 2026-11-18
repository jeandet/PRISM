#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/nodes/port.hpp>

#include <any>
#include <string>
#include <string_view>
#include <vector>

using namespace prism::nodes;

struct Volts {
    double v;
};

struct Amps {
    int whole;
};

struct Hertz {
    double hz;
};

struct Tesla {
    double t;
};

struct Weber {
    double w;
};

struct Gauss {
    double g;
};

template <> struct prism::nodes::PortType<Volts> { static constexpr std::string_view name = "volts"; };
template <> struct prism::nodes::PortType<Amps> {
    static constexpr std::string_view name = "amperes";
    static std::string format(const Amps& a) { return std::to_string(a.whole) + " A"; }
};
template <> struct prism::nodes::PortType<Hertz> { static constexpr std::string_view name = "hertz"; };
template <> struct prism::nodes::PortType<Tesla> { static constexpr std::string_view name = "tesla"; };
template <> struct prism::nodes::PortType<Weber> { static constexpr std::string_view name = "weber"; };
template <> struct prism::nodes::PortType<Gauss> {
    static constexpr std::string_view name = "gauss";
    static std::string format(const Gauss&) { return "1 G"; }
};

std::string repeated(const std::string& unit, int times) {
    std::string out;
    for (int i = 0; i < times; ++i) out += unit;
    return out;
}

TEST_CASE("a type's key is the same however it is asked for") {
    CHECK(type_key<double>() == type_key("number"));
    CHECK(type_key<double>() != type_key<int>());
    CHECK(type_name(type_key<std::vector<double>>()) == "waveform");
}

TEST_CASE("an output computes from an input and follows it") {
    In<double> a{"a", 1.0};
    Out<double> twice{"twice", [&] { return a.get() * 2; }};
    CHECK(twice.get() == 2.0);
    a.set_value(std::any(4.0));
    CHECK(twice.get() == 8.0);
}

TEST_CASE("an input reverts to its default on reset") {
    In<int> n{"n", 7};
    n.set_value(std::any(3));
    n.reset();
    CHECK(n.get() == 7);
}

TEST_CASE("an erased port reports its direction, type and value") {
    In<double> a{"a", 1.0};
    Out<double> twice{"twice", [&] { return a.get() * 2; }};
    Port& p = twice;
    CHECK(p.dir() == Dir::Out);
    CHECK(p.type() == type_key<double>());
    CHECK(std::any_cast<double>(p.value()) == 2.0);
    CHECK(p.name() == "twice");
}

TEST_CASE("on_change fires when the value changes") {
    In<double> a{"a", 1.0};
    Out<double> twice{"twice", [&] { return a.get() * 2; }};
    int calls = 0;
    auto c = static_cast<Port&>(twice).on_change([&] { ++calls; });
    a.set_value(std::any(4.0));
    CHECK(calls == 1);
}

TEST_CASE("built-in types hold their slot and app types take the next ones in order") {
    CHECK(wire_slot(type_key<std::vector<double>>()) == 0);
    CHECK(wire_slot(type_key<double>()) == 1);
    CHECK(wire_slot(type_key<bool>()) == 2);
    CHECK(wire_slot(type_key<int>()) == 3);
    CHECK(wire_slot(type_key<std::string>()) == 4);

    // Relative, not absolute: another test may have registered app types before these two.
    const auto first = type_key<Tesla>();
    const auto second = type_key<Weber>();
    CHECK(wire_slot(first) < wire_slots);
    CHECK(wire_slot(second) < wire_slots);
    CHECK(wire_slot(second) == (wire_slot(first) + 1) % wire_slots);
}

TEST_CASE("an app type without a format member names itself as probe text") {
    const auto volts = type_key<Volts>();
    CHECK(format_value(volts, std::any{Volts{12.0}}) == "volts");
    CHECK(format_value(volts, std::any{}) == "volts");
}

TEST_CASE("an app type with a format member formats its own probe text") {
    const auto amps = type_key<Amps>();
    CHECK(format_value(amps, std::any{Amps{5}}) == "5 A");
}

TEST_CASE("an explicit formatter is not replaced by the type's own formatting") {
    const auto key = type_key("gauss"); // the name overload: no sugar runs yet
    set_formatter(key, [](const std::any&) { return std::string{"explicit"}; });
    CHECK(type_key<Gauss>() == key);
    CHECK(format_value(key, std::any{Gauss{1.0}}) == "explicit");
}

TEST_CASE("a value of the wrong type shows the type's name instead of throwing") {
    CHECK(format_value(type_key<double>(), std::any{std::string{"nope"}}) == "number");
    CHECK(format_value(type_key<std::string>(), std::any{2.0}) == "text");
    CHECK(format_value(type_key<Amps>(), std::any{Hertz{3.0}}) == "amperes");
}

TEST_CASE("a registered formatter replaces the type name") {
    const auto hertz = type_key<Hertz>();
    set_formatter(hertz, [](const std::any&) { return std::string{"3 Hz"}; });
    CHECK(format_value(hertz, std::any{Hertz{3.0}}) == "3 Hz");
}

TEST_CASE("probe text of built-in scalars") {
    CHECK(format_value(type_key<double>(), std::any{0.123456}) == "0.123");
    CHECK(format_value(type_key<int>(), std::any{42}) == "42");
    CHECK(format_value(type_key<bool>(), std::any{true}) == "true");
    CHECK(format_value(type_key<bool>(), std::any{false}) == "false");
    CHECK(format_value(type_key<std::string>(), std::any{std::string{"ada"}}) == "\"ada\"");
}

TEST_CASE("probe text of a long text keeps 24 characters, whatever their size in bytes") {
    const auto text_key = type_key<std::string>();
    const std::string quarter = repeated("a", 24);
    CHECK(format_value(text_key, std::any{quarter}) == "\"" + quarter + "\"");
    CHECK(format_value(text_key, std::any{quarter + "a"}) == "\"" + quarter + "…");
    // 'é' is two bytes in UTF-8: 24 of them must survive whole, not 12.
    CHECK(format_value(text_key, std::any{repeated("é", 30)}) == "\"" + repeated("é", 24) + "…");
    CHECK(format_value(text_key, std::any{repeated("é", 24)}) == "\"" + repeated("é", 24) + "\"");
}

TEST_CASE("probe text of a waveform says how many samples and its peak") {
    std::vector<double> wave(240, 0.0);
    wave[17] = -0.83;
    CHECK(format_value(type_key<std::vector<double>>(), std::any{wave}) == "240 samples · peak 0.83");
}

TEST_CASE("probe text of an empty value is the type's name") {
    CHECK(format_value(type_key<double>(), std::any{}) == "number");
}
