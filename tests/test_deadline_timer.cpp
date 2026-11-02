#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>
#include <prism/app/deadline_timer.hpp>

#include <atomic>
#include <thread>

using namespace prism::app;
using namespace std::chrono_literals;
using clock_type = DeadlineTimer::clock;

namespace {
// Polls instead of sleeping a fixed time: fast when it works, bounded when it doesn't.
bool eventually(const std::atomic<int>& n, int expected, std::chrono::milliseconds limit = 2s) {
    auto end = clock_type::now() + limit;
    while (clock_type::now() < end) {
        if (n.load() == expected) return true;
        std::this_thread::sleep_for(1ms);
    }
    return n.load() == expected;
}
}

TEST_CASE("deadline timer: fires once, not before its deadline") {
    std::atomic<int> fired{0};
    clock_type::time_point at{};
    DeadlineTimer timer([&] { at = clock_type::now(); ++fired; });
    auto due = clock_type::now() + 30ms;
    timer.arm(due);
    REQUIRE(eventually(fired, 1));
    CHECK(at >= due);
    std::this_thread::sleep_for(50ms);
    CHECK(fired.load() == 1);
}

TEST_CASE("deadline timer: the earlier of two deadlines wins, and the later one is dropped") {
    std::atomic<int> fired{0};
    DeadlineTimer timer([&] { ++fired; });
    auto start = clock_type::now();
    timer.arm(start + 10s);
    timer.arm(start + 20ms);
    REQUIRE(eventually(fired, 1));
    CHECK(clock_type::now() - start < 5s);
    timer.arm(start + 10s + 1ms);   // a later deadline after firing: armed anew
    std::this_thread::sleep_for(30ms);
    CHECK(fired.load() == 1);
}

TEST_CASE("deadline timer: a deadline in the past fires right away") {
    std::atomic<int> fired{0};
    DeadlineTimer timer([&] { ++fired; });
    timer.arm(clock_type::now() - 1s);
    CHECK(eventually(fired, 1, 500ms));
}

TEST_CASE("deadline timer: destroyed before its deadline, it never fires") {
    std::atomic<int> fired{0};
    {
        DeadlineTimer timer([&] { ++fired; });
        timer.arm(clock_type::now() + 50ms);
    }
    std::this_thread::sleep_for(80ms);
    CHECK(fired.load() == 0);
}
