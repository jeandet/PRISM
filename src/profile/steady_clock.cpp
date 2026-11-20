// The profile clock on a hosted system: std::chrono::steady_clock (monotonic; nanosecond
// resolution on mainstream platforms). Bare metal defines these two functions in the port
// instead -- src/meson.build compiles this file only when host_machine.system() != 'none'.
#include <prism/profile/recorder.hpp>

#include <chrono>
#include <cstdint>

namespace prism::platform {

std::uint64_t profile_ticks()
{
    return static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
}

std::uint64_t profile_ticks_per_second()
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::seconds{1}).count());
}

} // namespace prism::platform
