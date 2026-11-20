#include <prism/platform/frame_stats.hpp>

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

Field160 system_usage(const std::optional<SystemCounters> &before, const std::optional<SystemCounters> &now,
                     double elapsed_s)
{
    Field160 f{""};
    if (!before || !now)
        return f;
    const double idle_s = double(now->idle_us - before->idle_us) / 1e6;
    const long cpu = std::lround(100.0 * std::clamp(1.0 - idle_s / elapsed_s, 0.0, 1.0));
    constexpr std::size_t kb = 1024;
    std::snprintf(f.text, sizeof f.text, "CPU %ld%% · heap %zu/%zu KB · stacks %zu/%zu KB · ", cpu,
                  now->heap_used / kb, now->heap_total / kb, now->stacks_used / kb, now->stacks_total / kb);
    return f;
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
    const int n = std::snprintf(
        line_.data(), line_.size(),
        "%ld fps · %s%s allocs/frame · update %s · snapshot %s · draw %s · present %s ms",
        fps, system_usage(window_system_, system, elapsed.count()).text, per_frame(allocations - window_allocations_, frames_).text,
        average_ms(sum_.update, frames_).text, average_ms(sum_.snapshot, frames_).text,
        average_ms(sum_.draw, frames_).text, average_ms(sum_.present, frames_).text);
    window_start_ = now;
    window_allocations_ = allocations;
    window_system_ = system;
    sum_ = {};
    frames_ = 0;
    return std::string_view(line_.data(), std::size_t(std::min<int>(n, int(line_.size()) - 1)));
}

} // namespace prism::platform
