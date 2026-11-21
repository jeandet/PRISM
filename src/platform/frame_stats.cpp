#include <prism/platform/frame_stats.hpp>
#include <prism/profile/profile.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace prism::platform {
namespace {

// A short field of the line, in a small fixed buffer: "-" when no frame was drawn.
struct Field16 {
    char text[16];
};

Field16 average_ms(std::chrono::microseconds sum, int frames)
{
    Field16 f{"-"};
    if (frames != 0)
        std::snprintf(f.text, sizeof f.text, "%.2f", double(sum.count()) / 1000.0 / frames);
    return f;
}

Field16 per_frame(std::uint32_t count, int frames)
{
    Field16 f{"-"};
    if (frames != 0)
        std::snprintf(f.text, sizeof f.text, "%lu",
                      static_cast<unsigned long>((count + std::uint32_t(frames) / 2) / std::uint32_t(frames)));
    return f;
}

// "CPU 74% · heap 230/420 KB · stacks 120/384 KB · ", or "" without counters from the platform.
// Sized for the widest numbers, so the line is never cut short (and -Wformat-truncation agrees).
struct Field160 {
    char text[160];
};

// The period's system numbers: the line's text, plus the same values for the profile counters
// ("cpu %" and "heap KB", recorded once per stats period in tick). cpu is -1 and the text empty
// without both reads (the constructor's and this tick's); heap_used is valid whenever `now` is.
struct SystemUsage {
    Field160 text{}; // zeroed: read as "%s" even when empty
    long cpu = -1;
    std::size_t heap_used = 0;
    bool has_now = false;
};

SystemUsage system_usage(const std::optional<SystemCounters> &before, const std::optional<SystemCounters> &now,
                         double elapsed_s)
{
    SystemUsage out;
    if (!now)
        return out;
    out.has_now = true;
    out.heap_used = now->heap_used;
    if (!before)
        return out;
    const double idle_s = double(now->idle_us - before->idle_us) / 1e6;
    out.cpu = std::lround(100.0 * std::clamp(1.0 - idle_s / elapsed_s, 0.0, 1.0));
    constexpr std::size_t kb = 1024;
    std::snprintf(out.text.text, sizeof out.text.text, "CPU %ld%% · heap %zu/%zu KB · stacks %zu/%zu KB · ",
                  out.cpu, now->heap_used / kb, now->heap_total / kb, now->stacks_used / kb, now->stacks_total / kb);
    return out;
}

} // namespace

void FrameStats::add(StageTimes t)
{
    sum_.update += t.update;
    sum_.snapshot += t.snapshot;
    sum_.draw += t.draw;
    sum_.present += t.present;
    ++frames_;
}

std::optional<std::string_view> FrameStats::tick(std::chrono::steady_clock::time_point now,
                                                 std::uint32_t allocations,
                                                 std::optional<SystemCounters> system)
{
    const std::chrono::duration<double> elapsed = now - window_start_;
    if (elapsed.count() < 1.0)
        return std::nullopt;
    const long fps = std::lround(frames_ / elapsed.count());
    const SystemUsage usage = system_usage(window_system_, system, elapsed.count());
    const int n = std::snprintf(
        line_.data(), line_.size(),
        "%ld fps · %s%s allocs/frame · update %s · snapshot %s · draw %s · present %s ms",
        fps, usage.text.text, per_frame(allocations - window_allocations_, frames_).text,
        average_ms(sum_.update, frames_).text, average_ms(sum_.snapshot, frames_).text,
        average_ms(sum_.draw, frames_).text, average_ms(sum_.present, frames_).text);
    // Once per stats period: the same numbers the line reports, as counters on the timeline
    // (with profiling off the macros are sizeof no-ops and nothing extra runs here).
    if (usage.has_now) {
        PRISM_PROFILE_COUNTER("heap KB", static_cast<std::int32_t>(usage.heap_used / 1024));
        if (usage.cpu >= 0)
            PRISM_PROFILE_COUNTER("cpu %", static_cast<std::int32_t>(usage.cpu));
    }
    window_start_ = now;
    window_allocations_ = allocations;
    window_system_ = system;
    sum_ = {};
    frames_ = 0;
    return std::string_view(line_.data(), std::size_t(std::min<int>(n, int(line_.size()) - 1)));
}

} // namespace prism::platform
