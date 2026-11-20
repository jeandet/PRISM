// The global recorder behind the PRISM_PROFILE_* marks: one static region of
// PRISM_PROFILING_EVENTS * PRISM_PROFILING_THREADS events, split evenly into
// PRISM_PROFILING_THREADS rings. Compiled only with -Dprofiling=enabled (src/meson.build),
// which also puts the three defines on prism_header_args.
#include <prism/profile/recorder.hpp>

#include <cstddef>

namespace prism::profile {
namespace {
// Static storage: zero-initialized, usable before the first mark, no allocation, and a board
// can place the region with a linker section (e.g. into SDRAM).
Event storage[PRISM_PROFILING_EVENTS * PRISM_PROFILING_THREADS];
} // namespace

static_assert(static_cast<std::size_t>(PRISM_PROFILING_THREADS) <= Recorder::max_threads,
              "the profiling_threads option exceeds Recorder::max_threads");

Recorder& global()
{
    static Recorder recorder{storage, PRISM_PROFILING_THREADS};
    return recorder;
}

} // namespace prism::profile
