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

template <> struct prism::nodes::PortType<Volts> { static constexpr std::string_view name = "volts"; };
template <> struct prism::nodes::PortType<Amps> {
    static constexpr std::string_view name = "amperes";
    static std::string format(const Amps& a) { return std::to_string(a.whole) + " A"; }
};
template <> struct prism::nodes::PortType<Hertz> { static constexpr std::string_view name = "hertz"; };

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

TEST_CASE("built-in types have fixed wire slots") {
    CHECK(wire_slot(type_key<std::vector<double>>()) == 0);
    CHECK(wire_slot(type_key<double>()) == 1);
    CHECK(wire_slot(type_key<bool>()) == 2);
    CHECK(wire_slot(type_key<int>()) == 3);
    CHECK(wire_slot(type_key<std::string>()) == 4);
}

TEST_CASE("an app type takes the first free wire slot and names itself as probe text") {
    // Volts is the first app type the registry sees, so it lands on the slot after the built-ins.
    const auto volts = type_key<Volts>();
    CHECK(wire_slot(volts) == 5);
    CHECK(format_value(volts, std::any{Volts{12.0}}) == "volts");
    CHECK(format_value(volts, std::any{}) == "volts");
}

TEST_CASE("an app type with a format member formats its own probe text") {
    const auto amps = type_key<Amps>();
    CHECK(format_value(amps, std::any{Amps{5}}) == "5 A");
    CHECK(wire_slot(amps) == 0); // app type slots wrap back to the start
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

TEST_CASE("probe text of a long text is cut short") {
    const auto cut = format_value(type_key<std::string>(), std::any{std::string(40, 'a')});
    CHECK(cut.ends_with("…"));
    CHECK(cut.size() < 30);
}

TEST_CASE("probe text of a waveform says how many samples and its peak") {
    std::vector<double> wave(240, 0.0);
    wave[17] = -0.83;
    CHECK(format_value(type_key<std::vector<double>>(), std::any{wave}) == "240 samples · peak 0.83");
}

TEST_CASE("probe text of an empty value is the type's name") {
    CHECK(format_value(type_key<double>(), std::any{}) == "number");
}
