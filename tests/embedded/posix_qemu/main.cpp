// QEMU firmware tests for the POSIX layer on FreeRTOS (mps2-an500, Cortex-M7).
// One FreeRTOS task with a big stack runs the tests, a watchdog task exits with
// code 99 after 20 s, and the exit code is the number of failed tests.
// Output goes through printf (newlib stdio: per-task reent via
// configUSE_NEWLIB_REENTRANT, locks from ports/freertos/newlib_locks.c, _write to
// semihosting); the semihosting qemu_log() is kept for the banner and the watchdog.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <thread>

#include <prism/platform/frame_stats.hpp>

#include "FreeRTOS.h"
#include "task.h"

extern "C" {
#include "qemu_log.h"
size_t xPortGetFreeHeapSize(void);
}

using namespace std::chrono_literals;

static int failures = 0;

static void check(const char *name, bool ok) {
    std::printf("%-48s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}

// 1. std::thread runs a lambda and join() returns; the lambda saw the captured value.
static void test_thread_join() {
    int captured = 0;
    std::thread t([&captured] { captured = 42; });
    t.join();
    check("1 std::thread join", captured == 42 && !t.joinable());
}

// 2. 8 threads each increment a shared counter 10000 times under std::mutex.
static void test_mutex_counter() {
    std::mutex m;
    long counter = 0;
    constexpr int kThreads = 8, kIters = 10000;
    std::thread ts[kThreads];
    for (int i = 0; i < kThreads; i++)
        ts[i] = std::thread([&] {
            for (int j = 0; j < kIters; j++) {
                std::lock_guard<std::mutex> l(m);
                ++counter;
            }
        });
    for (auto &t : ts) t.join();
    check("2 8x10000 increments under std::mutex", counter == kThreads * kIters);
}

// 3. condition_variable ping-pong between two threads, 1000 round trips.
static void test_condvar_pingpong() {
    std::mutex m;
    std::condition_variable cv;
    int turn = 0;
    long trips = 0;
    constexpr int kRounds = 1000;
    std::thread pong([&] {
        std::unique_lock<std::mutex> l(m);
        for (int i = 0; i < kRounds; i++) {
            cv.wait(l, [&] { return turn == 1; });
            turn = 0;
            ++trips;
            cv.notify_one();
        }
    });
    {
        std::unique_lock<std::mutex> l(m);
        for (int i = 0; i < kRounds; i++) {
            turn = 1;
            cv.notify_one();
            cv.wait(l, [&] { return turn == 0; });
        }
    }
    pong.join();
    check("3 condition_variable ping-pong x1000", trips == kRounds);
}

// 4. wait_for with nothing notifying times out after ~50 ms.
static void test_wait_for_timeout() {
    std::mutex m;
    std::condition_variable cv;
    std::unique_lock<std::mutex> l(m);
    auto t0 = std::chrono::steady_clock::now();
    bool got = cv.wait_for(l, 50ms, [] { return false; });
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - t0).count();
    check("4 wait_for(50ms) times out in 40..200ms", !got && ms >= 40 && ms <= 200);
}

// 5. sleep_for(20ms) sleeps at least 20 ms.
static void test_sleep_for() {
    auto t0 = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(20ms);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - t0).count();
    check("5 sleep_for(20ms) >= 20ms", ms >= 20);
}

// 6. thread_local: initializer per thread, independence, non-trivial ctor/dtor.
struct Counted {
    static std::atomic<int> ctored;
    static std::atomic<int> destroyed;
    Counted() { ++ctored; }
    ~Counted() { ++destroyed; }
};
std::atomic<int> Counted::ctored{0};
std::atomic<int> Counted::destroyed{0};
thread_local int tl_value = 1;
thread_local Counted tl_counted;

static void test_thread_local() {
    tl_value = 5;
    (void)tl_counted; // construct in this thread
    int seen = -1, other = -1;
    std::thread t([&] {
        seen = tl_value; // a fresh thread sees the initializer, not our 5
        tl_value = 7;
        (void)tl_counted;
        other = tl_value;
    });
    t.join();
    bool values_ok = (seen == 1 && other == 7 && tl_value == 5);
    bool ctor_ok = (Counted::ctored.load() == 2); // once per thread that touches it
    bool dtor_ok = (Counted::destroyed.load() >= 1); // the joined thread's copy ran
    check("6 thread_local values per thread", values_ok);
    check("6 thread_local ctor once/thread, dtor at exit", ctor_ok && dtor_ok);
}

// 7. shared_ptr copied and destroyed 100000 times across 4 threads.
static void test_shared_ptr() {
    auto p = std::make_shared<int>(42);
    constexpr int kThreads = 4, kIters = 25000;
    std::thread ts[kThreads];
    for (int i = 0; i < kThreads; i++)
        ts[i] = std::thread([&p] {
            for (int j = 0; j < kIters; j++) {
                auto q = p;
                (void)q;
            }
        });
    for (auto &t : ts) t.join();
    check("7 shared_ptr 100000 copies, use_count==1", p.use_count() == 1);
}

// 8. recursive_mutex locked twice by one thread; timed_mutex::try_lock_for fails
//    after ~30 ms when held by another thread.
static void test_recursive_and_timed_mutex() {
    std::recursive_mutex rm;
    rm.lock();
    rm.lock(); // must not deadlock
    rm.unlock();
    rm.unlock();
    check("8a std::recursive_mutex locks twice", true); // reaching here = no deadlock

    std::timed_mutex tm;
    tm.lock();
    std::atomic<bool> got{true};
    std::atomic<long> ms{0};
    std::thread t([&] {
        auto t0 = std::chrono::steady_clock::now();
        got = tm.try_lock_for(30ms);
        ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                 std::chrono::steady_clock::now() - t0).count();
    });
    t.join();
    tm.unlock();
    check("8b timed_mutex try_lock_for fails ~30ms", !got && ms >= 25 && ms <= 150);
}

// 9. A detached thread runs and its control block is freed (heap free bytes back
//    to within 64 bytes of before).
static void test_detached_thread_frees() {
    // Earlier tests' threads may still await the idle task's cleanup: let it run first, so
    // their memory does not come back in the middle of the measure.
    vTaskDelay(pdMS_TO_TICKS(200));
    size_t before = xPortGetFreeHeapSize();
    std::atomic<bool> done{false};
    {
        std::thread t([&done] { done = true; });
        t.detach();
    }
    while (!done.load()) std::this_thread::sleep_for(1ms);
    vTaskDelay(pdMS_TO_TICKS(200)); // the idle task frees the deleted task's stack
    long lost = (long)before - (long)xPortGetFreeHeapSize();
    std::printf("  heap lost: %ld bytes\n", lost);
    check("9 detached thread frees its block (<=64B)", lost <= 64);
}

// 10. shared_mutex: two readers hold it together; a writer waits for them.
static void test_shared_mutex() {
    std::shared_mutex sm;
    std::atomic<bool> reader_in{false}, release{false}, writer_got{false};
    sm.lock_shared(); // reader 1: this thread
    std::thread reader([&] {
        sm.lock_shared(); // reader 2
        reader_in = true;
        while (!release.load()) std::this_thread::sleep_for(1ms);
        sm.unlock_shared();
    });
    while (!reader_in.load()) std::this_thread::sleep_for(1ms);
    std::thread writer([&] {
        sm.lock(); // must wait until both readers are done
        writer_got = true;
        sm.unlock();
    });
    std::this_thread::sleep_for(50ms); // give the writer time to (not) acquire
    bool writer_blocked = !writer_got.load();
    release = true;
    sm.unlock_shared();
    reader.join();
    writer.join();
    check("10 shared_mutex: 2 readers, writer waits", writer_blocked && writer_got.load());
}

// 11. newlib's malloc under threads: 4 threads each doing 2000 malloc/free of
//     random sizes in 16..512 bytes, a pattern written into each block and
//     checked byte for byte before free. newlib serializes malloc with
//     __lock___malloc_recursive_mutex (ports/freertos/newlib_locks.c), so no corruption.
static void test_malloc_stress() {
    constexpr int kThreads = 4, kIters = 2000;
    std::atomic<int> bad{0};
    std::thread ts[kThreads];
    for (int i = 0; i < kThreads; i++)
        ts[i] = std::thread([&bad, i] {
            unsigned rng = 0x9e3779b9u * static_cast<unsigned>(i + 1); // per-thread seed, deterministic
            for (int j = 0; j < kIters; j++) {
                rng = rng * 1664525u + 1013904223u;
                size_t size = 16 + (rng >> 16) % (512 - 16 + 1); // 16..512 bytes
                auto *p = static_cast<unsigned char *>(std::malloc(size));
                if (!p) {
                    bad.fetch_add(1);
                    continue;
                }
                unsigned char pattern = static_cast<unsigned char>(i * 61 + j);
                std::memset(p, pattern, size);
                for (size_t k = 0; k < size; k++)
                    if (p[k] != pattern) {
                        bad.fetch_add(1);
                        break;
                    }
                std::free(p);
            }
        });
    for (auto &t : ts) t.join();
    check("11 4x2000 malloc/free 16..512B, patterns intact", bad.load() == 0);
}

// 12. printf from two threads at once: 50 short lines each. Whole lines are
//     what the host should read on QEMU's stdout (each printf is one locked
//     vfprintf); the check here is that both threads got all their lines out
//     without crashing, garbling the stdio state, or deadlocking.
static void test_printf_two_threads() {
    constexpr int kThreads = 2, kLines = 50;
    std::atomic<int> done{0};
    std::thread ts[kThreads];
    for (int i = 0; i < kThreads; i++)
        ts[i] = std::thread([&done, i] {
            for (int j = 0; j < kLines; j++)
                std::printf("thread %d line %02d: the quick brown fox jumps\n", i, j);
            done.fetch_add(1);
        });
    for (auto &t : ts) t.join();
    check("12 printf from 2 threads x50 lines, all out", done.load() == kThreads);
}

// 13b. join returns the moment the thread signals "done", and frees the thread's block then: a
//     joiner of higher priority runs at once. The ending thread must be finished with the block
//     before it signals. The test zeroes the freed block while the thread is still parked
//     between its signal and its end, lets it end, and checks nothing wrote there.
static void test_join_while_the_thread_ends() {
    const UBaseType_t prio = uxTaskPriorityGet(NULL);
    bool untouched = true;
    for (int i = 0; i < 10 && untouched; ++i) {
        std::thread t([] {});
        auto *block = reinterpret_cast<unsigned char *>(t.native_handle());
        vTaskPrioritySet(NULL, prio + 1);
        t.join();
        std::memset(block, 0, 32);   // freed, and nothing else ran to reuse it
        vTaskPrioritySet(NULL, prio);
        vTaskDelay(pdMS_TO_TICKS(5)); // the thread finishes ending
        for (int b = 0; b < 32; ++b) untouched = untouched && block[b] == 0;
    }
    check("13b a joined thread never writes its freed block", untouched);
}

// The stage timings (app/frame_stats) measure sub-millisecond work: steady_clock must step by
// less than a tick, and must never go backwards across a tick boundary.
static void test_clock_resolution() {
    using Clock = std::chrono::steady_clock;
    const auto first = Clock::now();
    auto next = first;
    while (next == first)
        next = Clock::now();
    const auto step = std::chrono::duration_cast<std::chrono::microseconds>(next - first).count();
    bool monotonic = true;
    auto prev = Clock::now();
    for (int i = 0; i < 200000; ++i) {
        const auto now = Clock::now();
        monotonic = monotonic && now >= prev;
        prev = now;
    }
    std::printf("   clock step %lld us\n", static_cast<long long>(step));
    check("13 steady_clock steps < 1 ms and never goes back", step > 0 && step < 1000 && monotonic);
}

// The heap allocation counter the timing line reads (ports/freertos/alloc_count.c): every C++ new and
// C malloc/calloc goes through newlib's _malloc_r once.
extern "C" uint32_t prism_allocation_count(void);

static void test_allocation_count() {
    const uint32_t before = prism_allocation_count();
    // escape(): the compiler may drop an allocation whose pointer is never used.
    const auto escape = [](void *ptr) { asm volatile("" : : "r"(ptr) : "memory"); };
    int *p = new int(7);
    escape(p);
    const uint32_t after_new = prism_allocation_count();
    void *m = std::malloc(100);
    escape(m);
    void *c = std::calloc(4, 25);
    escape(c);
    const uint32_t after_c = prism_allocation_count();
    delete p;
    std::free(m);
    std::free(c);
    const uint32_t after_free = prism_allocation_count();
    std::printf("   allocations: new %u, malloc+calloc %u, free %u\n", unsigned(after_new - before),
                unsigned(after_c - after_new), unsigned(after_free - after_c));
    check("14 allocation count: new +1, malloc+calloc +2, free +0",
          after_new - before == 1 && after_c - after_new == 2 && after_free == after_c);
}

// The allocation guard (ports/freertos/alloc_count.c): while armed, every allocation is a violation,
// and the first one's caller is recorded. Built without the trap here, so the test continues.
extern "C" void prism_allocation_guard_arm(void);
extern "C" void prism_allocation_guard_disarm(void);
extern "C" uint32_t prism_allocation_violations(void);
extern "C" void *prism_allocation_first_violation(void);

static void test_allocation_guard() {
    const auto escape = [](void *ptr) { asm volatile("" : : "r"(ptr) : "memory"); };
    const uint32_t before = prism_allocation_violations();
    prism_allocation_guard_arm();
    int *p = new int(3);
    escape(p);
    prism_allocation_guard_disarm();
    int *q = new int(4);
    escape(q);
    const bool counted = prism_allocation_violations() - before == 1;
    const bool located = prism_allocation_first_violation() != nullptr;
    delete p;
    delete q;
    check("15 allocation guard: armed new is a violation, with its caller", counted && located);
}

// The platform counters the timing line shows (ports/freertos/system_counters.cpp).
static void test_system_counters() {
    const auto before = prism::platform::system_counters();
    std::this_thread::sleep_for(100ms);
    const auto after = prism::platform::system_counters();
    const bool sane = before && after && after->heap_total > 0 &&
                      after->stacks_total == configTOTAL_HEAP_SIZE;
    const bool idle_grows = sane && after->idle_us > before->idle_us;
    check("16 system_counters: heaps reported, idle time grows over a 100 ms sleep",
          sane && idle_grows);
}

static void test_task(void *) {
    std::setvbuf(stdout, nullptr, _IONBF, 0); // one _write per printf: whole lines, no buffering races
    qemu_log("posix-on-FreeRTOS tests (mps2-an500)\n");
    test_thread_join();
    test_mutex_counter();
    test_condvar_pingpong();
    test_wait_for_timeout();
    test_sleep_for();
    test_thread_local();
    test_shared_ptr();
    test_recursive_and_timed_mutex();
    test_detached_thread_frees();
    test_shared_mutex();
    test_malloc_stress();
    test_printf_two_threads();
    test_clock_resolution();
    test_join_while_the_thread_ends();
    test_allocation_count();
    test_allocation_guard();
    test_system_counters();
    std::printf("failures: %d\n", failures);
    exit_qemu(failures); // exit code = number of failures
}

static void watchdog_task(void *) {
    vTaskDelay(pdMS_TO_TICKS(20000));
    qemu_log("WATCHDOG: tests did not finish in 20 s\n");
    exit_qemu(99);
}

int main() {
    xTaskCreate(test_task, "tests", 16384, nullptr, 2, nullptr); // 64 KiB stack
    xTaskCreate(watchdog_task, "watchdog", 512, nullptr, configMAX_PRIORITIES - 1, nullptr);
    vTaskStartScheduler();
    for (;;) { }
}
