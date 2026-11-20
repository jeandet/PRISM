/* A running count of heap allocations, for the timing line (app/frame_stats.hpp).
   Every allocation in newlib goes through _malloc_r: malloc, calloc, C++ operator new (via
   malloc), and the allocating case of realloc. The firmware links with -Wl,--wrap=_malloc_r
   (meson.build), which sends those calls here first. */
#include <reent.h>
#include <stddef.h>
#include <stdint.h>

static volatile uint32_t allocations;

/* The guard: once the app's startup is done it arms the guard, and any later allocation is a
   violation. The first one's return address is kept (the immediate caller of malloc, often
   operator new; with PRISM_ALLOC_GUARD_TRAP the debugger stops right there with the whole
   call stack). */
static volatile uint32_t guard_armed;
static volatile uint32_t violations;
static void *volatile first_violation;

void *__real__malloc_r(struct _reent *r, size_t n);

static void note_violation(void *caller) {
    if (__atomic_fetch_add(&violations, 1u, __ATOMIC_RELAXED) == 0u)
        first_violation = caller;
#ifdef PRISM_ALLOC_GUARD_TRAP
    __builtin_trap();
#endif
}

void *__wrap__malloc_r(struct _reent *r, size_t n) {
    __atomic_fetch_add(&allocations, 1u, __ATOMIC_RELAXED);
    if (guard_armed)
        note_violation(__builtin_return_address(0));
    return __real__malloc_r(r, n);
}

uint32_t prism_allocation_count(void) {
    return __atomic_load_n(&allocations, __ATOMIC_RELAXED);
}

void prism_allocation_guard_arm(void) { guard_armed = 1u; }
void prism_allocation_guard_disarm(void) { guard_armed = 0u; }
uint32_t prism_allocation_violations(void) { return violations; }
void *prism_allocation_first_violation(void) { return first_violation; }
