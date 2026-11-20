/* POSIX mutexes on FreeRTOS. PTHREAD_MUTEX_RECURSIVE maps to a FreeRTOS
   recursive mutex; NORMAL/ERRORCHECK/DEFAULT map to a plain FreeRTOS mutex
   (which has priority inheritance). Objects behind statically-initialized
   (0xFFFFFFFF) words are created lazily, race-free, on first use. */
#include "posix_internal.h"

int posix_mutex_init_struct(struct posix_mutex *m, int type) {
    m->recursive = (type == PTHREAD_MUTEX_RECURSIVE);
    m->type = type;
    m->handle = m->recursive ? xSemaphoreCreateRecursiveMutex() : xSemaphoreCreateMutex();
    return m->handle ? 0 : -1;
}

struct posix_mutex *posix_mutex_new(int type) {
    struct posix_mutex *m = posix_alloc(sizeof *m);
    if (!m) return NULL;
    if (posix_mutex_init_struct(m, type) != 0) {
        posix_free(m);
        return NULL;
    }
    return m;
}

void posix_mutex_free(struct posix_mutex *m) {
    if (m->handle) vSemaphoreDelete(m->handle);
    posix_free(m);
}

/* Lazy creation behind a 0xFFFFFFFF word: one CAS winner, losers free theirs. */
struct posix_mutex *posix_mutex_word(volatile pthread_mutex_t *word) {
    uint32_t w = __atomic_load_n((volatile uint32_t *)word, __ATOMIC_ACQUIRE);
    if (w == POSIX_UNINIT_WORD) {
        struct posix_mutex *m = posix_mutex_new(PTHREAD_MUTEX_DEFAULT);
        if (!m) return NULL;
        uint32_t expected = POSIX_UNINIT_WORD;
        if (__atomic_compare_exchange_n((volatile uint32_t *)word, &expected,
                                        (uint32_t)(uintptr_t)m, 0,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return m;
        posix_mutex_free(m);
        w = expected;
    }
    if (w == 0) return NULL;
    return (struct posix_mutex *)(uintptr_t)w;
}

int pmutex_lock(struct posix_mutex *m) {
    if (!posix_sched_running()) return 0; /* boot context is single-threaded: nothing to block on */
    BaseType_t r = m->recursive ? xSemaphoreTakeRecursive(m->handle, portMAX_DELAY)
                                 : xSemaphoreTake(m->handle, portMAX_DELAY);
    return r == pdTRUE ? 0 : EAGAIN;
}

int pmutex_trylock(struct posix_mutex *m) {
    if (!posix_sched_running()) return 0;
    BaseType_t r = m->recursive ? xSemaphoreTakeRecursive(m->handle, 0)
                                 : xSemaphoreTake(m->handle, 0);
    return r == pdTRUE ? 0 : EBUSY;
}

int pmutex_timedlock(struct posix_mutex *m, const struct timespec *abs) {
    if (!posix_sched_running()) return 0;
    TickType_t ticks = posix_abs_to_ticks(abs); /* 0 if the deadline already passed */
    BaseType_t r = m->recursive ? xSemaphoreTakeRecursive(m->handle, ticks)
                                 : xSemaphoreTake(m->handle, ticks);
    return r == pdTRUE ? 0 : ETIMEDOUT;
}

int pmutex_unlock(struct posix_mutex *m) {
    if (!posix_sched_running()) return 0;
    BaseType_t r = m->recursive ? xSemaphoreGiveRecursive(m->handle)
                                 : xSemaphoreGive(m->handle);
    return r == pdTRUE ? 0 : EPERM; /* unlocking a mutex this thread does not hold */
}

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr) {
    if (!mutex) return EINVAL;
    int type = PTHREAD_MUTEX_DEFAULT;
    if (attr) {
        if (!attr->is_initialized) return EINVAL;
        type = attr->type;
    }
    if (type != PTHREAD_MUTEX_NORMAL && type != PTHREAD_MUTEX_RECURSIVE &&
        type != PTHREAD_MUTEX_ERRORCHECK && type != PTHREAD_MUTEX_DEFAULT)
        return EINVAL;
    struct posix_mutex *m = posix_mutex_new(type);
    if (!m) return EAGAIN;
    /* POSIX makes re-initialising a live mutex UB; we just replace the word
       (an old object, if any, leaks — std::mutex passes fresh storage). */
    __atomic_store_n((volatile uint32_t *)mutex, (uint32_t)(uintptr_t)m, __ATOMIC_RELEASE);
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *mutex) {
    if (!mutex) return EINVAL;
    uint32_t w = __atomic_exchange_n((volatile uint32_t *)mutex, POSIX_UNINIT_WORD,
                                     __ATOMIC_ACQ_REL);
    if (w == POSIX_UNINIT_WORD || w == 0) return EINVAL;
    posix_mutex_free((struct posix_mutex *)(uintptr_t)w);
    return 0; /* the word is back to the static-initializer value: reusable */
}

int pthread_mutex_lock(pthread_mutex_t *mutex) {
    if (!mutex) return EINVAL;
    struct posix_mutex *m = posix_mutex_word(mutex);
    return m ? pmutex_lock(m) : EINVAL;
}

int pthread_mutex_trylock(pthread_mutex_t *mutex) {
    if (!mutex) return EINVAL;
    struct posix_mutex *m = posix_mutex_word(mutex);
    return m ? pmutex_trylock(m) : EINVAL;
}

int pthread_mutex_timedlock(pthread_mutex_t *mutex, const struct timespec *abstime) {
    if (!mutex || !abstime) return EINVAL;
    struct posix_mutex *m = posix_mutex_word(mutex);
    return m ? pmutex_timedlock(m, abstime) : EINVAL;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex) {
    if (!mutex) return EINVAL;
    struct posix_mutex *m = posix_mutex_word(mutex);
    return m ? pmutex_unlock(m) : EINVAL;
}

/* ---- mutex attributes ---- */

int pthread_mutexattr_init(pthread_mutexattr_t *attr) {
    if (!attr) return EINVAL;
    memset(attr, 0, sizeof *attr);
    attr->is_initialized = 1;
    attr->type = PTHREAD_MUTEX_DEFAULT;
    return 0;
}

int pthread_mutexattr_destroy(pthread_mutexattr_t *attr) {
    if (!attr || !attr->is_initialized) return EINVAL;
    attr->is_initialized = 0;
    return 0;
}

int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type) {
    if (!attr || !attr->is_initialized) return EINVAL;
    if (type != PTHREAD_MUTEX_NORMAL && type != PTHREAD_MUTEX_RECURSIVE &&
        type != PTHREAD_MUTEX_ERRORCHECK && type != PTHREAD_MUTEX_DEFAULT)
        return EINVAL;
    attr->type = type;
    attr->recursive = (type == PTHREAD_MUTEX_RECURSIVE);
    return 0;
}

int pthread_mutexattr_gettype(const pthread_mutexattr_t *attr, int *type) {
    if (!attr || !attr->is_initialized || !type) return EINVAL;
    *type = attr->type;
    return 0;
}
