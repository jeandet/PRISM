// The recorder behind the PRISM_PROFILE_* marks (include/prism/profile/recorder.hpp,
// doc/design/profiling.md): per-thread rings in one storage region, a full ring keeps the
// newest events and counts the lost ones, a thread beyond the table is counted, a paused
// recorder drops marks, and a thread name is copied and cut at 16 characters.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/profile/recorder.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using prism::profile::Event;
using prism::profile::Kind;
using prism::profile::Recorder;

namespace {

std::vector<Recorder::ThreadView> threads_of(const Recorder& r)
{
    std::vector<Recorder::ThreadView> views;
    r.for_each_thread([&](const Recorder::ThreadView& tv) { views.push_back(tv); });
    return views;
}

// A thread's ring, oldest first, as one flat vector.
std::vector<Event> events_of(const Recorder::ThreadView& tv)
{
    std::vector<Event> out(tv.first.begin(), tv.first.end());
    out.insert(out.end(), tv.second.begin(), tv.second.end());
    return out;
}

} // namespace

TEST_CASE("a block records begin and end with increasing ticks on its thread")
{
    std::vector<Event> storage(64);
    Recorder r{storage, 4};
    r.begin("a");
    r.end("a");
    const auto threads = threads_of(r);
    REQUIRE(threads.size() == 1);
    const auto events = events_of(threads[0]);
    REQUIRE(events.size() == 2);
    CHECK(events[0].kind == Kind::begin);
    CHECK(std::string_view(events[0].name) == "a");
    CHECK(events[1].kind == Kind::end);
    CHECK(std::string_view(events[1].name) == "a");
    CHECK(events[0].ticks <= events[1].ticks);
}

TEST_CASE("a full ring keeps the newest events and counts the lost ones")
{
    std::vector<Event> storage(64);
    Recorder r{storage, 4}; // 64 / 4: a 16-event ring
    std::array<std::string, 20> names; // stable addresses: names are recorded by pointer
    for (int i = 0; i < 20; ++i) {
        names[std::size_t(i)] = "e" + std::to_string(i);
        r.instant(names[std::size_t(i)].c_str());
    }
    const auto threads = threads_of(r);
    REQUIRE(threads.size() == 1);
    CHECK(threads[0].lost == 4); // 20 recorded, 16 kept
    const auto events = events_of(threads[0]);
    REQUIRE(events.size() == 16);
    for (std::size_t i = 0; i < events.size(); ++i) {
        CHECK(events[i].kind == Kind::instant);
        CHECK(std::string_view(events[i].name) == names[i + 4]); // the last 16, in order
    }
}

TEST_CASE("each thread gets its own ring")
{
    std::vector<Event> storage(64);
    Recorder r{storage, 4};
    std::jthread a([&] {
        r.instant("a1");
        r.instant("a2");
        r.instant("a3");
    });
    std::jthread b([&] {
        r.instant("b1");
        r.instant("b2");
        r.instant("b3");
    });
    a.join();
    b.join();
    const auto threads = threads_of(r);
    REQUIRE(threads.size() == 2);
    std::vector<std::string> firsts;
    for (const auto& tv : threads) {
        const auto events = events_of(tv);
        REQUIRE(events.size() == 3);
        for (const auto& e : events)
            CHECK(e.kind == Kind::instant);
        firsts.emplace_back(events[0].name);
    }
    std::sort(firsts.begin(), firsts.end());
    CHECK(firsts == std::vector<std::string>{"a1", "b1"});
}

TEST_CASE("a thread beyond the table is counted, not recorded")
{
    std::vector<Event> storage(64);
    Recorder r{storage, 1}; // one ring
    r.instant("mine");
    std::jthread other([&] { r.instant("theirs"); }); // no ring left: counted, not recorded
    other.join();
    CHECK(r.threads_dropped() == 1);
    const auto threads = threads_of(r);
    REQUIRE(threads.size() == 1);
    const auto events = events_of(threads[0]);
    REQUIRE(events.size() == 1);
    CHECK(std::string_view(events[0].name) == "mine");
}

TEST_CASE("a paused recorder drops marks")
{
    std::vector<Event> storage(64);
    Recorder r{storage, 4};
    r.pause();
    r.instant("x");
    r.resume();
    r.instant("y");
    const auto threads = threads_of(r);
    REQUIRE(threads.size() == 1);
    const auto events = events_of(threads[0]);
    REQUIRE(events.size() == 1);
    CHECK(std::string_view(events[0].name) == "y");
}

TEST_CASE("a thread name is copied and cut at 16 characters")
{
    std::vector<Event> storage(64);
    Recorder r{storage, 4};
    {
        const std::string long_name = "render-thread-0123456789"; // 24 characters
        r.name_thread(long_name);
    } // the source string is gone: the name in the slot must be a copy
    r.instant("x");
    const auto threads = threads_of(r);
    REQUIRE(threads.size() == 1);
    CHECK(threads[0].name == "render-thread-01"); // the first 16 characters
    CHECK(threads[0].name.size() == 16);
}

TEST_CASE("a recorder built where an old one lived starts with no thread claimed")
{
    std::vector<Event> storage(64);
    std::optional<Recorder> r;
    r.emplace(storage, 1);
    std::jthread([&] { r->instant("other"); }).join(); // takes the only ring
    r->instant("dropped");                             // this thread: beyond the table
    r.reset();
    r.emplace(storage, 1); // same address
    r->instant("mine");
    const auto threads = threads_of(*r);
    REQUIRE(threads.size() == 1);
    const auto events = events_of(threads[0]);
    REQUIRE(events.size() == 1);
    CHECK(std::string_view(events[0].name) == "mine");
}

TEST_CASE("a thread that has used many recorders still keeps one ring per recorder")
{
    std::vector<Event> storage(64);
    std::optional<Recorder> r;
    for (int i = 0; i < 20; ++i) {
        r.emplace(storage, 4);
        r->instant("warm-up");
    }
    r.emplace(storage, 4);
    r->instant("a");
    r->instant("b");
    const auto threads = threads_of(*r);
    REQUIRE(threads.size() == 1);
    CHECK(events_of(threads[0]).size() == 2);
}
