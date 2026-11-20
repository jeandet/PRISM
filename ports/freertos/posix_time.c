/* POSIX time on the FreeRTOS tick count, refined within a tick by the SysTick counter.
   simplify: CLOCK_REALTIME == CLOCK_MONOTONIC == time since boot (there is no
   wall clock). */
#include "posix_internal.h"
#include <sys/time.h>
#include <unistd.h>

/* SysTick and the interrupt control register are architectural on every Cortex-M (ARMv7-M
   ARM, B3.3 and B3.2.4): the same addresses on the H7B3 and on QEMU's MPS2. */
#define SYST_RVR (*(volatile uint32_t *)0xE000E014u) /* reload value: a tick is RVR + 1 counts */
#define SYST_CVR (*(volatile uint32_t *)0xE000E018u) /* current value: counts down to 0 */
#define SCB_ICSR (*(volatile uint32_t *)0xE000ED04u)
#define ICSR_PENDSTSET (1u << 26)                    /* a SysTick interrupt is pending */

static uint32_t systick_pending(void) { return (SCB_ICSR & ICSR_PENDSTSET) ? 1u : 0u; }

/* Microseconds since boot. A consistent (tick count, counter) pair: retried when the tick
   interrupt ran in between, and a wrap whose interrupt is still pending counts as a tick. */
static uint64_t now_us(void) {
    const uint64_t us_per_tick = 1000000u / configTICK_RATE_HZ;
    const uint32_t reload = SYST_RVR;
    TickType_t ticks, again;
    uint32_t counter, pending, pending_after;
    do {
        ticks = xTaskGetTickCount();
        pending = systick_pending();
        counter = SYST_CVR;
        pending_after = systick_pending();
        again = xTaskGetTickCount();
    } while (ticks != again || pending != pending_after);
    const uint64_t whole = ((uint64_t)ticks + pending) * us_per_tick;
    if (reload == 0u) /* SysTick not started yet (before the scheduler) */
        return whole;
    return whole + (uint64_t)(reload - counter) * us_per_tick / ((uint64_t)reload + 1u);
}

/* The same clock for FreeRTOS's run-time stats (portGET_RUN_TIME_COUNTER_VALUE). */
uint64_t prism_now_us(void) { return now_us(); }

int clock_gettime(clockid_t clock_id, struct timespec *tp) {
    if (!tp) return EINVAL;
    if (clock_id != CLOCK_REALTIME && clock_id != CLOCK_MONOTONIC) return EINVAL;
    const uint64_t us = now_us();
    tp->tv_sec = (time_t)(us / 1000000u);
    tp->tv_nsec = (long)((us % 1000000u) * 1000u);
    return 0;
}

/* Absolute CLOCK_REALTIME deadline -> relative tick timeout (0 if already past,
   rounded up so a 1 ms request never returns early). */
TickType_t posix_abs_to_ticks(const struct timespec *abs) {
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    int64_t now_ms = (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
    int64_t abs_ms = (int64_t)abs->tv_sec * 1000 + abs->tv_nsec / 1000000;
    int64_t delta = abs_ms - now_ms;
    if (delta <= 0) return 0;
    return (TickType_t)((delta * (int64_t)configTICK_RATE_HZ + 999) / 1000);
}

int nanosleep(const struct timespec *rqtp, struct timespec *rmtp) {
    (void)rmtp; /* no signals exist, so nanosleep is never interrupted */
    if (!rqtp || rqtp->tv_sec < 0 || rqtp->tv_nsec < 0 || rqtp->tv_nsec >= 1000000000L)
        return EINVAL;
    if (!posix_sched_running()) return 0; /* cannot sleep before the scheduler runs */
    int64_t ms = (int64_t)rqtp->tv_sec * 1000 + rqtp->tv_nsec / 1000000;
    TickType_t ticks = (TickType_t)((ms * (int64_t)configTICK_RATE_HZ + 999) / 1000);
    if (ms > 0 && ticks == 0) ticks = 1;
    vTaskDelay(ticks);
    return 0;
}

unsigned int sleep(unsigned int seconds) {
    struct timespec ts;
    ts.tv_sec = (time_t)seconds;
    ts.tv_nsec = 0;
    nanosleep(&ts, NULL);
    return 0;
}

int usleep(useconds_t useconds) {
    struct timespec ts;
    ts.tv_sec = (time_t)(useconds / 1000000u);
    ts.tv_nsec = (long)(useconds % 1000000u) * 1000L;
    return nanosleep(&ts, NULL);
}

/* newlib's gettimeofday() calls this; libnosys.a provides a failing one, but our
   strong definition in an object linked before libnosys wins. */
int _gettimeofday(struct timeval *tv, void *tz) {
    (void)tz;
    if (tv) {
        const uint64_t us = now_us();
        tv->tv_sec = (time_t)(us / 1000000u);
        tv->tv_usec = (suseconds_t)(us % 1000000u);
    }
    return 0;
}

int gettimeofday(struct timeval *tv, void *tz) { return _gettimeofday(tv, tz); }

time_t time(time_t *timer) {
    time_t s = (time_t)(now_us() / 1000000u);
    if (timer) *timer = s;
    return s;
}
