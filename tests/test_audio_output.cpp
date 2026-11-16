// audio::Output (include/prism/audio/output.hpp): the device path on SDL's dummy driver, the
// timer path when no driver opens, and no allocation around the render function. SDL reads the
// environment once, so tests/meson.build runs this twice, one SDL_AUDIO_DRIVER per run, each
// with the test cases for that driver (doctest -tc).
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/audio/output.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <new>
#include <thread>

using namespace prism;
using namespace std::chrono_literals;

namespace {
// Counts allocations made on the thread that renders, once it has rendered once.
thread_local bool counting = false;
std::atomic<int> render_thread_allocations{0};
}

void* operator new(std::size_t n) {
    if (counting) render_thread_allocations.fetch_add(1);
    if (void* p = std::malloc(n == 0 ? 1 : n)) return p;
    throw std::bad_alloc{};
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

namespace {
struct Calls {
    std::atomic<int> count{0};
    std::atomic<bool> wrong_size{false};
};

audio::RenderFn counting_render(Calls& calls, int channels) {
    return [&calls, channels](std::span<float> out) {
        counting = true;   // from the first call on, this thread's allocations count
        if (out.empty() || out.size() % static_cast<std::size_t>(channels) != 0
            || out.size() > static_cast<std::size_t>(channels * audio::kMaxBlockFrames))
            calls.wrong_size = true;
        for (float& s : out) s = 0.f;
        calls.count.fetch_add(1);
    };
}

void wait_for(const Calls& calls, int n) {
    for (auto waited = 0ms; calls.count.load() < n && waited < 3000ms; waited += 5ms)
        std::this_thread::sleep_for(5ms);
}
}

TEST_CASE("timer: with no audio driver, the timer renders blocks of the asked size") {
    Calls calls;
    {
        audio::Output out{{.sample_rate = 48000, .channels = 2, .block_frames = 256},
                          counting_render(calls, 2)};
        CHECK_FALSE(out.playing());
        CHECK_FALSE(out.error().empty());
        wait_for(calls, 10);
        CHECK(calls.count.load() >= 10);
        CHECK(out.load().budget_ms == doctest::Approx(256.0 / 48.0));
    }
    const int after_close = calls.count.load();
    std::this_thread::sleep_for(30ms);
    CHECK(calls.count.load() == after_close);   // nothing renders once it is gone
    CHECK_FALSE(calls.wrong_size.load());
}

TEST_CASE("device: on a device (SDL's dummy driver), SDL's thread renders") {
    Calls calls;
    audio::Output out{{.sample_rate = 48000, .channels = 2, .block_frames = 512},
                      counting_render(calls, 2)};
    INFO("SDL: ", out.error());
    CHECK(out.playing());
    CHECK(out.error().empty());
    wait_for(calls, 5);
    CHECK(calls.count.load() >= 5);
    CHECK_FALSE(calls.wrong_size.load());
    CHECK(out.load().calls > 0);
}

void check_no_allocation_between_blocks() {
    Calls calls;
    audio::Output out{{.sample_rate = 48000, .channels = 2, .block_frames = 256}, counting_render(calls, 2)};
    wait_for(calls, 3);
    render_thread_allocations = 0;
    const int before = calls.count.load();
    wait_for(calls, before + 20);
    CHECK(calls.count.load() >= before + 20);
    CHECK(render_thread_allocations.load() == 0);
}

TEST_CASE("timer: nothing allocates on the rendering thread between blocks") { check_no_allocation_between_blocks(); }
TEST_CASE("device: nothing allocates on the rendering thread between blocks") { check_no_allocation_between_blocks(); }
