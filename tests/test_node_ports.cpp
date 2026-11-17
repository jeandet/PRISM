#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/nodes/port.hpp>

#include <any>
#include <vector>

using namespace prism::nodes;

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
