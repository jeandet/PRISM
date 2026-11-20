// The recorder behind the PRISM_PROFILE_* marks (include/prism/profile/recorder.hpp,
// doc/design/profiling.md): per-thread rings in one storage region, a full ring keeps the
// newest events and counts the lost ones, a thread beyond the table is counted, a paused
// recorder drops marks, and a thread name is copied and cut at 16 characters. A mark takes a
// call site (Site), recorded by pointer: the tests build theirs with make_site where a
// constexpr site is possible, and as a plain std::array where a test needs many distinct ones
// (Recorder takes const Site&; only the PRISM_PROFILE_* macros require constexpr).
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/profile/profile.hpp>
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
using prism::profile::Site;
using prism::profile::make_site;

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

// n distinct sites whose names stay alive: sites are recorded by pointer, so the names must
// outlive the recorder's use of them. Built at runtime -- only the macros need a constexpr
// site.
struct SiteArray {
    std::vector<std::string> names;
    std::vector<Site> sites;

    explicit SiteArray(std::size_t n, const std::string& prefix) : names(n), sites(n)
    {
        for (std::size_t i = 0; i < n; ++i) {
            names[i] = prefix + std::to_string(i);
            sites[i] = Site{names[i].c_str(), "test_fn", "test_profile_recorder.cpp",
                            static_cast<std::uint32_t>(i + 1)};
        }
    }
};

} // namespace

TEST_CASE("a block records begin and end with increasing ticks on its thread")
{
    std::vector<Event> storage(64);
    Recorder r{storage, 4};
    static constexpr Site a = make_site("a");
    r.begin(a);
    r.end(a);
    const auto threads = threads_of(r);
    REQUIRE(threads.size() == 1);
    const auto events = events_of(threads[0]);
    REQUIRE(events.size() == 2);
    CHECK(events[0].kind == Kind::begin);
    CHECK(events[0].site == &a);
    CHECK(std::string_view(events[0].site->name) == "a");
    CHECK(events[1].kind == Kind::end);
    CHECK(events[1].site == &a);
    CHECK(std::string_view(events[1].site->name) == "a");
    CHECK(events[0].ticks <= events[1].ticks);
}

TEST_CASE("a full ring keeps the newest events and counts the lost ones")
{
    std::vector<Event> storage(64);
    Recorder r{storage, 4}; // 64 / 4: a 16-event ring
    SiteArray sites{20, "e"};
    for (std::size_t i = 0; i < sites.sites.size(); ++i)
        r.instant(sites.sites[i]);
    const auto threads = threads_of(r);
    REQUIRE(threads.size() == 1);
    CHECK(threads[0].lost == 4); // 20 recorded, 16 kept
    const auto events = events_of(threads[0]);
    REQUIRE(events.size() == 16);
    for (std::size_t i = 0; i < events.size(); ++i) {
        CHECK(events[i].kind == Kind::instant);
        CHECK(events[i].site == &sites.sites[i + 4]); // the last 16, in order
        CHECK(std::string_view(events[i].site->name) == sites.names[i + 4]);
    }
}

TEST_CASE("each thread gets its own ring")
{
    std::vector<Event> storage(64);
    Recorder r{storage, 4};
    static constexpr Site a1 = make_site("a1");
    static constexpr Site a2 = make_site("a2");
    static constexpr Site a3 = make_site("a3");
    static constexpr Site b1 = make_site("b1");
    static constexpr Site b2 = make_site("b2");
    static constexpr Site b3 = make_site("b3");
    std::jthread a([&] {
        r.instant(a1);
        r.instant(a2);
        r.instant(a3);
    });
    std::jthread b([&] {
        r.instant(b1);
        r.instant(b2);
        r.instant(b3);
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
        firsts.emplace_back(events[0].site->name);
    }
    std::sort(firsts.begin(), firsts.end());
    CHECK(firsts == std::vector<std::string>{"a1", "b1"});
}

TEST_CASE("a thread beyond the table is counted, not recorded")
{
    std::vector<Event> storage(64);
    Recorder r{storage, 1}; // one ring
    static constexpr Site mine = make_site("mine");
    static constexpr Site theirs = make_site("theirs");
    r.instant(mine);
    std::jthread other([&] { r.instant(theirs); }); // no ring left: counted, not recorded
    other.join();
    CHECK(r.threads_dropped() == 1);
    const auto threads = threads_of(r);
    REQUIRE(threads.size() == 1);
    const auto events = events_of(threads[0]);
    REQUIRE(events.size() == 1);
    CHECK(events[0].site == &mine);
    CHECK(std::string_view(events[0].site->name) == "mine");
}

TEST_CASE("a paused recorder drops marks")
{
    std::vector<Event> storage(64);
    Recorder r{storage, 4};
    static constexpr Site x = make_site("x");
    static constexpr Site y = make_site("y");
    r.pause();
    r.instant(x);
    r.resume();
    r.instant(y);
    const auto threads = threads_of(r);
    REQUIRE(threads.size() == 1);
    const auto events = events_of(threads[0]);
    REQUIRE(events.size() == 1);
    CHECK(events[0].site == &y);
    CHECK(std::string_view(events[0].site->name) == "y");
}

TEST_CASE("a thread name is copied and cut at 16 characters")
{
    std::vector<Event> storage(64);
    Recorder r{storage, 4};
    static constexpr Site x = make_site("x");
    {
        const std::string long_name = "render-thread-0123456789"; // 24 characters
        r.name_thread(long_name);
    } // the source string is gone: the name in the slot must be a copy
    r.instant(x);
    const auto threads = threads_of(r);
    REQUIRE(threads.size() == 1);
    CHECK(threads[0].name == "render-thread-01"); // the first 16 characters
    CHECK(threads[0].name.size() == 16);
}

TEST_CASE("a recorder built where an old one lived starts with no thread claimed")
{
    std::vector<Event> storage(64);
    std::optional<Recorder> r;
    static constexpr Site other = make_site("other");
    static constexpr Site dropped = make_site("dropped");
    static constexpr Site mine = make_site("mine");
    r.emplace(storage, 1);
    std::jthread([&] { r->instant(other); }).join(); // takes the only ring
    r->instant(dropped);                              // this thread: beyond the table
    r.reset();
    r.emplace(storage, 1); // same address
    r->instant(mine);
    const auto threads = threads_of(*r);
    REQUIRE(threads.size() == 1);
    const auto events = events_of(threads[0]);
    REQUIRE(events.size() == 1);
    CHECK(events[0].site == &mine);
    CHECK(std::string_view(events[0].site->name) == "mine");
}

TEST_CASE("a thread that has used many recorders still keeps one ring per recorder")
{
    std::vector<Event> storage(64);
    std::optional<Recorder> r;
    static constexpr Site warm_up = make_site("warm-up");
    static constexpr Site a = make_site("a");
    static constexpr Site b = make_site("b");
    for (int i = 0; i < 20; ++i) {
        r.emplace(storage, 4);
        r->instant(warm_up);
    }
    r.emplace(storage, 4);
    r->instant(a);
    r->instant(b);
    const auto threads = threads_of(*r);
    REQUIRE(threads.size() == 1);
    const auto events = events_of(threads[0]);
    REQUIRE(events.size() == 2);
    CHECK(events[0].site == &a);
    CHECK(events[1].site == &b);
}

#if defined(PRISM_PROFILING)
// The macros themselves (profile.hpp): each builds its call site -- a static constexpr Site at
// the macro's expansion, from std::source_location -- and records an event pointing at it.
// Compiled only in a -Dprofiling=enabled build: global() exists only there.
TEST_CASE("the PRISM_PROFILE_* macros build their call site and record events pointing at it")
{
    PRISM_PROFILE_INSTANT("macro-instant");
    PRISM_PROFILE_COUNTER("macro-counter", 7);
    {
        PRISM_PROFILE_SCOPE("macro-scope");
    }
    PRISM_PROFILE_FRAME();
    bool saw_instant = false;
    bool saw_counter = false;
    bool saw_scope = false;
    bool saw_frame = false;
    prism::profile::global().for_each_thread([&](const Recorder::ThreadView& tv) {
        auto check = [&](const Event& e) {
            const std::string_view name = e.site->name; // never null: frame() has its own site
            if (e.kind == Kind::instant && name == "macro-instant")
                saw_instant = true;
            if (e.kind == Kind::counter && name == "macro-counter" && e.value == 7)
                saw_counter = true;
            if ((e.kind == Kind::begin || e.kind == Kind::end) && name == "macro-scope")
                saw_scope = true;
            if (e.kind == Kind::frame && name == "frame")
                saw_frame = true;
        };
        for (const Event& e : tv.first)
            check(e);
        for (const Event& e : tv.second)
            check(e);
    });
    CHECK(saw_instant);
    CHECK(saw_counter);
    CHECK(saw_scope);
    CHECK(saw_frame);
}
#endif
