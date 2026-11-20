// The timing line's CPU and memory figures on a FreeRTOS + newlib port: the idle task's run
// time (configGENERATE_RUN_TIME_STATS, counted in microseconds by posix_time.c), newlib's heap
// (malloc, from `end` up to _heap_end: the board's linker script) and FreeRTOS's heap_4.

#include <prism/platform/alloc_count.hpp>
#include <prism/platform/frame_stats.hpp>

#include "FreeRTOS.h"
#include "task.h"

#include <malloc.h>

extern "C" char end[];       // the linker script: where the newlib heap starts
extern "C" char _heap_end[]; // and where it must stop (sbrk.c)
extern "C" std::uint32_t prism_allocation_count(void); // alloc_count.c

std::uint32_t prism::platform::allocation_count()
{
    return prism_allocation_count();
}

std::optional<prism::platform::SystemCounters> prism::platform::system_counters()
{
    const struct mallinfo heap = mallinfo();
    return SystemCounters{
        .idle_us = ulTaskGetIdleRunTimeCounter(),
        .heap_used = std::size_t(heap.uordblks),
        .heap_total = std::size_t(_heap_end - end),
        .stacks_used = configTOTAL_HEAP_SIZE - xPortGetFreeHeapSize(),
        .stacks_total = configTOTAL_HEAP_SIZE,
    };
}
