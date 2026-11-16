// A Derived only recomputes from the sources it was given, and leaving one out is silent: the
// value goes stale (the synth stayed silent once because the power button was not in the list).
// A debug build watches the first compute and reports every value read that the list does not
// cover.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/derived.hpp>
#include <prism/core/field.hpp>
#include <prism/core/shared.hpp>
#include <prism/core/state.hpp>

#include <cstdio>
#include <string>
#include <string_view>
#include <unistd.h>

using namespace prism;
using namespace prism::core;

namespace {

#ifndef NDEBUG

constexpr std::string_view kWarning = "reads a value it does not list as a source";

// Same trick as test_placed.cpp: the warning goes to stderr, so run `build` with stderr on a
// temporary file and hand back what it said.
std::string said_while(auto&& build) {
    std::FILE* sink = std::tmpfile();
    const int saved = dup(fileno(stderr));
    std::fflush(stderr);
    dup2(fileno(sink), fileno(stderr));
    build();
    std::fflush(stderr);
    dup2(saved, fileno(stderr));
    close(saved);
    std::rewind(sink);
    std::string out;
    for (int c; (c = std::fgetc(sink)) != EOF;) out.push_back(static_cast<char>(c));
    std::fclose(sink);
    return out;
}

#endif

} // namespace

TEST_CASE("a Derived recomputes from the sources it lists") {
    Field<int> a{2};
    Field<int> b{3};
    Derived<int> sum{[&] { return a.get() + b.get(); }, a, b};

    CHECK(sum.get() == 5);

    a.set(10);
    CHECK(sum.get() == 13);

    b.set(7);
    CHECK(sum.get() == 17);
}

#ifndef NDEBUG

TEST_CASE("a Derived that reads a Field it does not list says so") {
    Field<int> listed{1};
    Field<int> forgotten{2};
    const std::string said = said_while([&] {
        Derived<int> sum{[&] { return listed.get() + forgotten.get(); }, listed};
        CHECK(sum.get() == 3);
    });
    CHECK(said.find("prism: a Derived") != std::string::npos);
    CHECK(said.find(kWarning) != std::string::npos);
}

TEST_CASE("a Derived that lists every value it reads says nothing") {
    Field<int> a{1};
    Field<int> b{2};
    const std::string said = said_while([&] {
        Derived<int> sum{[&] { return a.get() + b.get(); }, a, b};
        CHECK(sum.get() == 3);
    });
    CHECK(said.find(kWarning) == std::string::npos);
}

TEST_CASE("a State or a Shared listed as a source counts as listed") {
    State<int> state{1};
    Shared<int> shared{2};
    const std::string said = said_while([&] {
        Derived<int> sum{[&] { return state.get() + shared.get(); }, state, shared};
        CHECK(sum.get() == 3);
    });
    CHECK(said.find(kWarning) == std::string::npos);
}

TEST_CASE("a Derived listed as a source counts as listed") {
    Field<int> x{2};
    Derived<int> doubled{[&] { return x.get() * 2; }, x};
    const std::string said = said_while([&] {
        Derived<int> quadrupled{[&] { return doubled.get() * 2; }, doubled};
        CHECK(quadrupled.get() == 8);
    });
    CHECK(said.find(kWarning) == std::string::npos);
}

TEST_CASE("a Derived that reads another Derived without listing it says so") {
    Field<int> x{2};
    Derived<int> doubled{[&] { return x.get() * 2; }, x};
    const std::string said = said_while([&] {
        // Subscribing to `x` does not make `doubled` a source: nothing recomputes when it changes.
        Derived<int> quadrupled{[&] { return doubled.get() * 2; }, x};
        CHECK(quadrupled.get() == 8);
    });
    CHECK(said.find(kWarning) != std::string::npos);
}

TEST_CASE("a Derived built inside another one's compute keeps its own reads") {
    Field<int> outer_listed{1};
    Field<int> inner_only{2};
    bool inner_built = false;
    const std::string said = said_while([&] {
        Derived<int> outer{
            [&] {
                // inner lists what it reads: if the log were not saved and restored, its read of
                // inner_only would be blamed on outer, which never read it itself.
                Derived<int> inner{[&] { return inner_only.get(); }, inner_only};
                inner_built = true;
                (void)inner;
                return outer_listed.get();
            },
            outer_listed};
        CHECK(outer.get() == 1);
    });
    CHECK(inner_built);
    CHECK(said.find(kWarning) == std::string::npos);
}

#endif
