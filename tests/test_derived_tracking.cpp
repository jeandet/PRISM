// A Derived given no source list follows what its compute reads, and reads again at every
// recompute: nothing to forget, and a branch not taken is not watched. Release builds too.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/derived.hpp>
#include <prism/core/field.hpp>
#include <prism/core/shared.hpp>
#include <prism/core/state.hpp>

using namespace prism::core;

TEST_CASE("a Derived with no list recomputes when anything it read changes") {
    Field<int> a{1};
    State<int> b{2};
    Derived<int> sum{[&] { return a.get() + b.get(); }};
    CHECK(sum.get() == 3);
    a.set(10);
    CHECK(sum.get() == 12);
    b.set(20);
    CHECK(sum.get() == 30);
}

TEST_CASE("it follows the branch it took, not the one it did not") {
    Field<bool> use_a{true};
    Field<int> a{1};
    Field<int> b{2};
    int computes = 0;
    Derived<int> pick{[&] { ++computes; return use_a.get() ? a.get() : b.get(); }};
    CHECK(computes == 1);

    b.set(3);                    // not read: no recompute
    CHECK(computes == 1);
    use_a.set(false);            // now reads b
    CHECK(pick.get() == 3);
    b.set(4);
    CHECK(pick.get() == 4);
    const int before = computes;
    a.set(5);                    // no longer read
    CHECK(computes == before);
}

TEST_CASE("a value read twice is followed once") {
    Field<int> a{1};
    int computes = 0;
    Derived<int> twice{[&] { ++computes; return a.get() + a.get(); }};
    a.set(2);
    CHECK(twice.get() == 4);
    CHECK(computes == 2);
}

TEST_CASE("Derived of Derived, and a Shared once drained") {
    Field<int> a{1};
    Shared<int> s{0};
    Derived<int> doubled{[&] { return 2 * a.get(); }};
    Derived<int> total{[&] { return doubled.get() + s.get(); }};
    a.set(3);
    CHECK(total.get() == 6);
    s.set(4);
    s.drain_notifications();
    CHECK(total.get() == 10);
}

TEST_CASE("untracked() reads without following") {
    Field<int> a{1};
    Field<int> peek{100};
    Derived<int> d{[&] { return a.get() + untracked([&] { return peek.get(); }); }};
    peek.set(200);
    CHECK(d.get() == 101);       // stale on purpose
    a.set(2);
    CHECK(d.get() == 202);
}

TEST_CASE("a Derived built inside another's compute keeps its reads to itself") {
    Field<int> outer_only{1};
    Field<int> inner_only{2};
    int outer_computes = 0;
    Derived<int> outer{[&] {
        ++outer_computes;
        Derived<int> inner{[&] { return inner_only.get(); }};
        return outer_only.get() + 0 * inner.get();
    }};
    // inner.get() is read by outer, but inner dies each time; inner_only is inner's read.
    const int before = outer_computes;
    inner_only.set(3);
    CHECK(outer_computes == before);
    outer_only.set(5);
    CHECK(outer.get() == 5);
}
