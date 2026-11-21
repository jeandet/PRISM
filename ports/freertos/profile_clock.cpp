// The profile clock on the FreeRTOS port. include/prism/profile/recorder.hpp declares
// prism::platform::profile_ticks()/_per_second, and src/meson.build compiles the hosted
// steady_clock.cpp only when host_machine.system() != 'none' -- so on bare metal the port
// defines them (system_counters.cpp is the port's other prism::platform hook).
//
// A .cpp, not a .c: the two functions are namespaced C++ declarations, which a C file
// cannot define.
//
// The clock is the DWT cycle counter (doc/design/profiling.md, "Clock"), extended to 64
// bits, at configCPU_CLOCK_HZ ticks per second. QEMU does not model the DWT, so the first
// call detects a counter that does not advance and falls back to prism_now_us()
// (posix_time.c) at 1 000 000 ticks per second.
#include <prism/profile/recorder.hpp>

#include "FreeRTOS.h" // configCPU_CLOCK_HZ; FreeRTOSConfig.h declares prism_now_us()

#include <cstdint>

namespace {

// The DWT and CoreDebug registers are architectural on every Cortex-M7 (ARMv7-M ARM,
// B3.2/B3.4): the same addresses on a real M7 and on QEMU's MPS2 -- only the counter's
// behaviour differs, which init_clock() detects.
constexpr std::uintptr_t demcr_addr = 0xE000EDFC;    // CoreDebug monitor control
constexpr std::uintptr_t dwt_ctrl_addr = 0xE0001000; // DWT control
constexpr std::uintptr_t dwt_cyccnt_addr = 0xE0001004;
constexpr std::uintptr_t dwt_lar_addr = 0xE0001FB0;  // the M7's lock access register
constexpr std::uint32_t demcr_trcena = 1u << 24;     // lets the trace units run
constexpr std::uint32_t dwt_cyccntena = 1u;           // DWT_CTRL bit 0: the cycle counter runs
constexpr std::uint32_t dwt_lar_key = 0xC5ACCE55;     // the LAR's unlock key

volatile std::uint32_t& reg(std::uintptr_t addr)
{
    return *reinterpret_cast<volatile std::uint32_t*>(addr);
}

// Turn trace on first (CoreDebug DEMCR.TRCENA): on an M7 the DWT ignores its unlock while
// trace is off. Then unlock it (LAR) and start the cycle counter -- without resetting it, so a
// debugger already using it is not disturbed.
void dwt_start()
{
    reg(demcr_addr) |= demcr_trcena;
    reg(dwt_lar_addr) = dwt_lar_key;
    reg(dwt_ctrl_addr) |= dwt_cyccntena;
}

// QEMU does not model the DWT: spin a short loop and see whether CYCCNT moved.
bool dwt_counts()
{
    const std::uint32_t before = reg(dwt_cyccnt_addr);
    for (std::uint32_t i = 0; i < 1000u; ++i)
        __asm__ volatile(""); // keeps the loop: the optimiser may not drop an asm volatile
    return reg(dwt_cyccnt_addr) != before;
}

// Mask interrupts around a few cycles of clock state: save PRIMASK, cpsid i, restore.
std::uint32_t irq_mask()
{
    std::uint32_t primask;
    __asm__ volatile("mrs %0, primask" : "=r"(primask));
    __asm__ volatile("cpsid i" ::: "memory");
    return primask;
}

void irq_unmask(std::uint32_t primask)
{
    __asm__ volatile("msr primask, %0" : : "r"(primask) : "memory");
}

// The clock's state: whether the DWT counts (a real M7) and the 64-bit extension of CYCCNT.
struct Clock {
    bool dwt = false;   // the cycle counter advances; false under QEMU
    std::uint32_t high = 0; // the high word of the 64-bit extension
    std::uint32_t last = 0; // the last 32-bit CYCCNT reading
};

// The 64-bit extension of the 32-bit CYCCNT: a reading below the last one is a wrap, added
// to the high word. The read-compare-update runs with interrupts masked -- a few cycles,
// no OS call, safe from any task.
// simplify: a wrap is missed when no mark reads the clock for one full 32-bit period,
// 2^32 / configCPU_CLOCK_HZ seconds (15 s at 280 MHz, about 172 s at the QEMU board's
// 25 MHz); the upgrade is a FreeRTOS tick hook that reads the clock once per tick.
std::uint64_t dwt_ticks(Clock& c)
{
    const std::uint32_t primask = irq_mask();
    const std::uint32_t now = reg(dwt_cyccnt_addr);
    if (now < c.last)
        ++c.high;
    c.last = now;
    const std::uint64_t ticks = (static_cast<std::uint64_t>(c.high) << 32) | now;
    irq_unmask(primask); // after composing: another task's wrap must not land in between
    return ticks;
}

Clock dwt_clock;        // .bss: dwt=false until init_clock() runs
bool clock_ready = false;

// First call, from whichever task marks first. Masked so two tasks' first marks cannot both
// start the counter (a second start would reset it under the first); single core, so masking
// interrupts is enough.
void init_clock()
{
    const std::uint32_t primask = irq_mask();
    if (!clock_ready) {
        dwt_start();
        dwt_clock.dwt = dwt_counts();
        dwt_clock.last = reg(dwt_cyccnt_addr);
        clock_ready = true;
    }
    irq_unmask(primask);
}

} // namespace

std::uint64_t prism::platform::profile_ticks()
{
    if (!clock_ready)
        init_clock();
    return dwt_clock.dwt ? dwt_ticks(dwt_clock) : prism_now_us();
}

std::uint64_t prism::platform::profile_ticks_per_second()
{
    if (!clock_ready)
        init_clock();
    return dwt_clock.dwt ? static_cast<std::uint64_t>(configCPU_CLOCK_HZ) : 1'000'000;
}
