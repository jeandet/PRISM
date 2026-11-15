// core::Latest<T>: a real-time thread hands its newest value to another thread, with no lock
// and no allocation (Shared<T> allocates on set, so it cannot run in an audio callback).
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/latest.hpp>

#include <atomic>
#include <thread>

using prism::core::Latest;

namespace {
struct Pair {
    int a = 0, b = 0;   // written together: a reader must never see them from two publishes
    bool operator==(const Pair&) const = default;
};
} // namespace

TEST_CASE("Latest hands back the newest value, and the same one until the next publish") {
    Latest<Pair> latest{};
    CHECK(latest.read() == Pair{});
    latest.publish({1, 1});
    CHECK(latest.read() == Pair{1, 1});
    CHECK(latest.read() == Pair{1, 1});
    latest.publish({2, 2});
    latest.publish({3, 3});   // the reader missed one: it gets the newest, not the one in between
    CHECK(latest.read() == Pair{3, 3});
}

TEST_CASE("Latest starts from the value it was given") {
    Latest<Pair> latest{Pair{7, 7}};
    CHECK(latest.read() == Pair{7, 7});
}

TEST_CASE("a reader racing a writer only ever sees whole values, never going back") {
    Latest<Pair> latest{};
    std::atomic<bool> done{false};
    std::jthread writer([&] {
        for (int i = 1; i <= 200000; ++i) latest.publish({i, i});
        done.store(true);
    });
    int last = 0;
    bool torn = false, backwards = false;
    while (!done.load()) {
        const Pair p = latest.read();
        torn |= p.a != p.b;
        backwards |= p.a < last;
        last = p.a;
    }
    CHECK_FALSE(torn);
    CHECK_FALSE(backwards);
    CHECK(latest.read() == Pair{200000, 200000});
}
