// The profile recorder + dump round trip in QEMU (mps2-an500, Cortex-M7): two tasks each
// mark 50 nested blocks into a Recorder over one static 1024-event region (4 rings), then
// the test task dumps it into an io::MemoryStream and reads the bytes back with prism-trace's
// read_trace (tools/prism-trace/trace_reader.cpp, compiled into this firmware). Checks:
// per thread the ticks never go backwards and at least one tick increases (the clock moves);
// the dump reads back as found, 2 threads, header pointer size 4, every event's site
// resolving with the right name. Output through printf (semihosting _write, as prism_qemu);
// the exit code is the number of failed checks, printed as "failures: N".
//
// Task 5 step 1 builds this against the stub profile clock (ports/freertos/profile_clock.cpp
// returns 0): the firmware links and runs, and the "clock moves" checks are the red that
// step 2's real clock turns green.
#include <prism/io/memory_stream.hpp>
#include <prism/profile/dump.hpp>
#include <prism/profile/recorder.hpp>
#include <prism/profile/trace_format.hpp>
#include <prism-trace/trace_reader.hpp>

#include <array>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string_view>
#include <thread>

#include "FreeRTOS.h"
#include "task.h"

extern "C" {
#include "qemu_log.h"
}

using prism::profile::Event;
using prism::profile::Kind;
using prism::profile::Recorder;
using prism::profile::Site;
using prism::profile::make_site;
using prism_trace::Trace;
using prism_trace::read_trace;

namespace {

constexpr std::size_t kEvents = 1024; // the static region, split into 4 rings of 256
constexpr std::size_t kRings = 4;     // events: the 100 marks per task below never wrap
constexpr int kBlocks = 50;           // nested blocks each task marks

std::array<Event, kEvents> storage;   // static: the recorder allocates nothing
Recorder recorder{storage, kRings};

constexpr Site kOuter = make_site("worker-outer");
constexpr Site kInner = make_site("worker-inner");

int failures = 0;

void check(const char *name, bool ok) {
    std::printf("%-52s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}

std::uint64_t ptr_of(const Site &site) { return reinterpret_cast<std::uintptr_t>(&site); }

// One task's marks: kBlocks nested blocks, the site alternating with depth.
void mark_blocks() {
    for (int i = 0; i < kBlocks; ++i)
        recorder.begin(i % 2 == 0 ? kOuter : kInner);
    for (int i = kBlocks - 1; i >= 0; --i)
        recorder.end(i % 2 == 0 ? kOuter : kInner);
}

// The site the event at flat index i must point at: the begin of depth i for i < kBlocks,
// the end closing depth kBlocks-1-j for i = kBlocks+j.
const Site &site_at(std::size_t i) {
    if (i < static_cast<std::size_t>(kBlocks))
        return i % 2 == 0 ? kOuter : kInner;
    const int depth = kBlocks - 1 - static_cast<int>(i - kBlocks);
    return depth % 2 == 0 ? kOuter : kInner;
}

// The header chunk's pointer-size byte (trace_format.hpp: version u16 | pointer size u8 | ...).
// The dump starts with the sync marker, so the header chunk sits right after it.
bool header_pointer_size(std::span<const std::byte> bytes, std::uint8_t &out) {
    constexpr auto marker = prism::profile::sync_marker;
    if (bytes.size() < marker.size() + 5 + 3)
        return false;
    for (std::size_t i = 0; i < marker.size(); ++i)
        if (bytes[i] != marker[i])
            return false;
    const std::size_t at = marker.size();
    if (static_cast<std::uint8_t>(bytes[at]) != static_cast<std::uint8_t>(prism::profile::Chunk::header))
        return false;
    out = static_cast<std::uint8_t>(bytes[at + 5 + 2]); // payload: version u16, then pointer size
    return true;
}

// Per thread (oldest first, across the ring's two spans): the ticks never go backwards, and
// at least one tick increases -- the clock moves.
void check_ticks(std::string_view thread_name, const Recorder::ThreadView &tv) {
    char name[64];
    bool non_decreasing = true;
    bool increases = false;
    std::uint64_t prev = 0;
    bool first = true;
    const std::size_t total = tv.first.size() + tv.second.size();
    for (std::size_t i = 0; i < total; ++i) {
        const Event &e = i < tv.first.size() ? tv.first[i] : tv.second[i - tv.first.size()];
        if (!first) {
            if (e.ticks < prev)
                non_decreasing = false;
            if (e.ticks > prev)
                increases = true;
        }
        prev = e.ticks;
        first = false;
    }
    std::snprintf(name, sizeof name, "%.*s: ticks never go backwards", int(thread_name.size()),
                  thread_name.data());
    check(name, non_decreasing);
    std::snprintf(name, sizeof name, "%.*s: the clock moves (a tick increases)", int(thread_name.size()),
                  thread_name.data());
    check(name, increases);
}

void profile_test_task(void *) {
    std::setvbuf(stdout, nullptr, _IONBF, 0); // one _write per printf: whole lines
    qemu_log("PRISM profile round trip in QEMU (mps2-an500)\n");

    // Two tasks, each marking kBlocks nested blocks into its own ring.
    std::thread worker0([] {
        recorder.name_thread("worker0");
        mark_blocks();
    });
    std::thread worker1([] {
        recorder.name_thread("worker1");
        mark_blocks();
    });
    worker0.join();
    worker1.join();

    // After joining, per thread: the event count, nothing lost, and the ticks.
    std::array<Recorder::ThreadView, Recorder::max_threads> views{};
    std::size_t thread_count = 0;
    recorder.for_each_thread([&](const Recorder::ThreadView &tv) { views[thread_count++] = tv; });
    check("recorder: 2 threads claimed a ring", thread_count == 2);
    for (std::size_t i = 0; i < thread_count; ++i) {
        const Recorder::ThreadView &tv = views[i];
        const std::size_t count = tv.first.size() + tv.second.size();
        char name[64];
        std::snprintf(name, sizeof name, "%.*s: %d events (50 nested blocks)", int(tv.name.size()),
                      tv.name.data(), int(count));
        check(name, count == 2 * static_cast<std::size_t>(kBlocks));
        std::snprintf(name, sizeof name, "%.*s: no events lost", int(tv.name.size()), tv.name.data());
        check(name, tv.lost == 0);
        check_ticks(tv.name, tv);
    }

    // The dump into memory, and the host tool's reader over the bytes.
    prism::io::MemoryStream out;
    check("dump into an io::MemoryStream succeeds", prism::profile::dump(recorder, out).has_value());
    std::printf("  dump: %zu bytes\n", out.bytes().size());

    const Trace trace = read_trace(out.bytes());
    check("read_trace: a dump is found", trace.found);
    check("read_trace: no warnings", trace.warnings.empty());
    check("read_trace: 2 threads", trace.threads.size() == 2);
    check("read_trace: ticks per second carried over",
          trace.ticks_per_second == static_cast<double>(prism::platform::profile_ticks_per_second()));
    std::uint8_t pointer_size = 0;
    check("read_trace: header pointer size 4",
          header_pointer_size(out.bytes(), pointer_size) && pointer_size == 4 && sizeof(void *) == 4);
    check("read_trace: 2 sites", trace.sites.size() == 2);

    const Trace::Thread *traced0 = nullptr, *traced1 = nullptr;
    for (const Trace::Thread &t : trace.threads) {
        if (t.name == "worker0")
            traced0 = &t;
        if (t.name == "worker1")
            traced1 = &t;
    }
    check("read_trace: threads named worker0/worker1", traced0 != nullptr && traced1 != nullptr);

    // Every event's site resolves, with the right name, and the kinds are 50 begins then 50 ends.
    bool sites_ok = true;
    bool kinds_ok = true;
    for (const Trace::Thread *t : {traced0, traced1}) {
        if (t == nullptr)
            continue;
        if (t->events.size() != 2 * static_cast<std::size_t>(kBlocks)) {
            sites_ok = false;
            continue;
        }
        for (std::size_t i = 0; i < t->events.size(); ++i) {
            const prism_trace::TraceEvent &ev = t->events[i];
            const auto site = trace.sites.find(ev.site);
            const Site &expect = site_at(i);
            if (site == trace.sites.end() || ev.site != ptr_of(expect) || site->second.name != expect.name)
                sites_ok = false;
            if (i < static_cast<std::size_t>(kBlocks) ? ev.kind != Kind::begin : ev.kind != Kind::end)
                kinds_ok = false;
        }
    }
    check("read_trace: every event's site resolves with the right name", sites_ok);
    check("read_trace: 50 begins then 50 ends per thread", kinds_ok);

    std::printf("failures: %d\n", failures);
    exit_qemu(failures); // exit code = number of failed checks
}

void watchdog_task(void *) {
    vTaskDelay(pdMS_TO_TICKS(60000));
    qemu_log("WATCHDOG: profile tests did not finish in 60 s\n");
    exit_qemu(99);
}

} // namespace

int main() {
    xTaskCreate(profile_test_task, "profile", 16384, nullptr, 2, nullptr); // 64 KiB stack
    xTaskCreate(watchdog_task, "watchdog", 512, nullptr, configMAX_PRIORITIES - 1, nullptr);
    vTaskStartScheduler();
    for (;;) { }
}
