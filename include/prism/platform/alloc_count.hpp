#pragma once

// Heap allocations, for the timing line and the allocation guard.

#include <cstdint>

namespace prism::platform {

// A running count of heap allocations by any thread (it wraps around). Defined by whoever links
// the runtime: a port counts its malloc, a test program its operator new.
std::uint32_t allocation_count();

} // namespace prism::platform

// A port's allocation guard, built in with PRISM_ALLOC_GUARD: armed once startup is done, any
// later allocation is a violation. Defined by the port (C, beside its malloc wrapper).
extern "C" void prism_allocation_guard_arm(void);
extern "C" void prism_allocation_guard_disarm(void);
extern "C" std::uint32_t prism_allocation_violations(void);
extern "C" void *prism_allocation_first_violation(void);
