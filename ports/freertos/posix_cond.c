/* POSIX condition variables on FreeRTOS task notifications. A waiter pushes
   itself on a list (critical section), releases the caller's mutex, blocks in
   xTaskNotifyWait (task notifications are stateful, so a signal that arrives
   before the waiter blocks is not lost), then removes itself and re-takes the
   mutex. signal wakes one waiter, broadcast wakes all. */
#include "posix_internal.h"

void posix_cond_init_struct(struct posix_cond *c) { c->waiters = NULL; }

struct posix_cond *posix_cond_new(void) {
    struct posix_cond *c = posix_alloc(sizeof *c);
    if (c) c->waiters = NULL;
    return c;
}

void posix_cond_free(struct posix_cond *c) { posix_free(c); }

struct posix_cond *posix_cond_word(volatile pthread_cond_t *word) {
    uint32_t w = __atomic_load_n((volatile uint32_t *)word, __ATOMIC_ACQUIRE);
    if (w == POSIX_UNINIT_WORD) {
        struct posix_cond *c = posix_cond_new();
        if (!c) return NULL;
        uint32_t expected = POSIX_UNINIT_WORD;
        if (__atomic_compare_exchange_n((volatile uint32_t *)word, &expected,
                                        (uint32_t)(uintptr_t)c, 0,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return c;
        posix_cond_free(c);
        w = expected;
    }
    if (w == 0) return NULL;
    return (struct posix_cond *)(uintptr_t)w;
}

/* abs == NULL means wait forever. */
int pcond_wait(struct posix_cond *c, struct posix_mutex *m, const struct timespec *abs) {
    if (!posix_sched_running()) return 0; /* boot context: no waiting possible */
    TickType_t ticks = abs ? posix_abs_to_ticks(abs) : portMAX_DELAY;
    struct posix_cond_waiter w;
    w.task = xTaskGetCurrentTaskHandle();
    w.next = NULL;
    taskENTER_CRITICAL();
    w.next = c->waiters;
    c->waiters = &w;
    taskEXIT_CRITICAL();
    (void)pmutex_unlock(m); /* release the caller's mutex */
    uint32_t value;
    BaseType_t notified = xTaskNotifyWait(0, 0xFFFFFFFFu, &value, ticks);
    taskENTER_CRITICAL();
    struct posix_cond_waiter **pp = &c->waiters;
    while (*pp && *pp != &w) pp = &(*pp)->next;
    if (*pp) *pp = w.next; /* still listed (timed out): remove ourselves */
    taskEXIT_CRITICAL();
    (void)pmutex_lock(m); /* re-acquire the caller's mutex */
    return notified == pdPASS ? 0 : ETIMEDOUT;
}

int pcond_signal(struct posix_cond *c) {
    taskENTER_CRITICAL();
    struct posix_cond_waiter *w = c->waiters;
    if (w) {
        c->waiters = w->next;
        xTaskNotify(w->task, 1, eSetValueWithOverwrite); /* pending if not blocked yet */
    }
    taskEXIT_CRITICAL();
    return 0;
}

int pcond_broadcast(struct posix_cond *c) {
    taskENTER_CRITICAL();
    struct posix_cond_waiter *w = c->waiters;
    c->waiters = NULL;
    while (w) {
        struct posix_cond_waiter *next = w->next;
        xTaskNotify(w->task, 1, eSetValueWithOverwrite);
        w = next;
    }
    taskEXIT_CRITICAL();
    return 0;
}

int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr) {
    (void)attr; /* only the default clock (CLOCK_REALTIME) is supported */
    if (!cond) return EINVAL;
    struct posix_cond *c = posix_cond_new();
    if (!c) return EAGAIN;
    __atomic_store_n((volatile uint32_t *)cond, (uint32_t)(uintptr_t)c, __ATOMIC_RELEASE);
    return 0;
}

int pthread_cond_destroy(pthread_cond_t *cond) {
    if (!cond) return EINVAL;
    uint32_t w = __atomic_exchange_n((volatile uint32_t *)cond, POSIX_UNINIT_WORD,
                                     __ATOMIC_ACQ_REL);
    if (w == POSIX_UNINIT_WORD || w == 0) return EINVAL;
    posix_cond_free((struct posix_cond *)(uintptr_t)w);
    return 0;
}

int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex) {
    struct posix_cond *c = cond ? posix_cond_word(cond) : NULL;
    struct posix_mutex *m = mutex ? posix_mutex_word(mutex) : NULL;
    if (!c || !m) return EINVAL;
    /* simplify: the mutex is released and re-taken once — condition variables
       are only used with non-recursive mutexes, as libstdc++ requires */
    return pcond_wait(c, m, NULL);
}

int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                           const struct timespec *abstime) {
    struct posix_cond *c = cond ? posix_cond_word(cond) : NULL;
    struct posix_mutex *m = mutex ? posix_mutex_word(mutex) : NULL;
    if (!c || !m || !abstime) return EINVAL;
    return pcond_wait(c, m, abstime);
}

int pthread_cond_signal(pthread_cond_t *cond) {
    struct posix_cond *c = cond ? posix_cond_word(cond) : NULL;
    return c ? pcond_signal(c) : EINVAL;
}

int pthread_cond_broadcast(pthread_cond_t *cond) {
    struct posix_cond *c = cond ? posix_cond_word(cond) : NULL;
    return c ? pcond_broadcast(c) : EINVAL;
}

/* ---- cond attributes (only the clock attribute is meaningful here) ---- */

int pthread_condattr_init(pthread_condattr_t *attr) {
    if (!attr) return EINVAL;
    memset(attr, 0, sizeof *attr);
    attr->is_initialized = 1;
    attr->clock = CLOCK_REALTIME;
    return 0;
}

int pthread_condattr_destroy(pthread_condattr_t *attr) {
    if (!attr || !attr->is_initialized) return EINVAL;
    attr->is_initialized = 0;
    return 0;
}

int pthread_condattr_setclock(pthread_condattr_t *attr, clockid_t clock_id) {
    if (!attr || !attr->is_initialized) return EINVAL;
    /* simplify: CLOCK_REALTIME == CLOCK_MONOTONIC == uptime, both accepted */
    if (clock_id != CLOCK_REALTIME && clock_id != CLOCK_MONOTONIC) return EINVAL;
    attr->clock = clock_id;
    return 0;
}

int pthread_condattr_getclock(const pthread_condattr_t *attr, clockid_t *clock_id) {
    if (!attr || !attr->is_initialized || !clock_id) return EINVAL;
    *clock_id = attr->clock;
    return 0;
}
