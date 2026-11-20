/* POSIX reader/writer locks, built on an internal mutex + condition variable. */
#include "posix_internal.h"

struct posix_rwlock *posix_rwlock_new(void) {
    struct posix_rwlock *r = posix_alloc(sizeof *r);
    if (!r) return NULL;
    if (posix_mutex_init_struct(&r->mu, PTHREAD_MUTEX_NORMAL) != 0) {
        posix_free(r);
        return NULL;
    }
    posix_cond_init_struct(&r->cond);
    r->readers = 0;
    r->writer_active = 0;
    return r;
}

void posix_rwlock_free(struct posix_rwlock *r) {
    if (r->mu.handle) vSemaphoreDelete(r->mu.handle);
    posix_free(r);
}

struct posix_rwlock *posix_rwlock_word(volatile pthread_rwlock_t *word) {
    uint32_t w = __atomic_load_n((volatile uint32_t *)word, __ATOMIC_ACQUIRE);
    if (w == POSIX_UNINIT_WORD) {
        struct posix_rwlock *r = posix_rwlock_new();
        if (!r) return NULL;
        uint32_t expected = POSIX_UNINIT_WORD;
        if (__atomic_compare_exchange_n((volatile uint32_t *)word, &expected,
                                        (uint32_t)(uintptr_t)r, 0,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return r;
        posix_rwlock_free(r);
        w = expected;
    }
    if (w == 0) return NULL;
    return (struct posix_rwlock *)(uintptr_t)w;
}

static int rwlock_rdlock_abs(struct posix_rwlock *r, const struct timespec *abs) {
    int rc = pmutex_lock(&r->mu);
    if (rc) return rc;
    while (r->writer_active) {
        rc = pcond_wait(&r->cond, &r->mu, abs); /* recomputes the timeout from abs */
        if (rc) {
            pmutex_unlock(&r->mu);
            return rc;
        }
    }
    r->readers++;
    pmutex_unlock(&r->mu);
    return 0;
}

static int rwlock_wrlock_abs(struct posix_rwlock *r, const struct timespec *abs) {
    int rc = pmutex_lock(&r->mu);
    if (rc) return rc;
    while (r->writer_active || r->readers > 0) {
        rc = pcond_wait(&r->cond, &r->mu, abs);
        if (rc) {
            pmutex_unlock(&r->mu);
            return rc;
        }
    }
    r->writer_active = 1;
    pmutex_unlock(&r->mu);
    return 0;
}

static int rwlock_unlock_internal(struct posix_rwlock *r) {
    int rc = pmutex_lock(&r->mu);
    if (rc) return rc;
    if (r->writer_active) {
        r->writer_active = 0;
        pcond_broadcast(&r->cond); /* wake waiting writers and readers */
    } else if (r->readers > 0) {
        r->readers--;
        if (r->readers == 0) pcond_broadcast(&r->cond); /* last reader out: wake a writer */
    }
    pmutex_unlock(&r->mu);
    return 0;
}

int pthread_rwlock_init(pthread_rwlock_t *rwlock, const pthread_rwlockattr_t *attr) {
    (void)attr;
    if (!rwlock) return EINVAL;
    struct posix_rwlock *r = posix_rwlock_new();
    if (!r) return EAGAIN;
    __atomic_store_n((volatile uint32_t *)rwlock, (uint32_t)(uintptr_t)r, __ATOMIC_RELEASE);
    return 0;
}

int pthread_rwlock_destroy(pthread_rwlock_t *rwlock) {
    if (!rwlock) return EINVAL;
    uint32_t w = __atomic_exchange_n((volatile uint32_t *)rwlock, POSIX_UNINIT_WORD,
                                     __ATOMIC_ACQ_REL);
    if (w == POSIX_UNINIT_WORD || w == 0) return EINVAL;
    posix_rwlock_free((struct posix_rwlock *)(uintptr_t)w);
    return 0;
}

int pthread_rwlock_rdlock(pthread_rwlock_t *rwlock) {
    struct posix_rwlock *r = rwlock ? posix_rwlock_word(rwlock) : NULL;
    return r ? rwlock_rdlock_abs(r, NULL) : EINVAL;
}

int pthread_rwlock_tryrdlock(pthread_rwlock_t *rwlock) {
    struct posix_rwlock *r = rwlock ? posix_rwlock_word(rwlock) : NULL;
    if (!r) return EINVAL;
    int rc = pmutex_lock(&r->mu);
    if (rc) return rc;
    if (r->writer_active) {
        pmutex_unlock(&r->mu);
        return EBUSY;
    }
    r->readers++;
    pmutex_unlock(&r->mu);
    return 0;
}

int pthread_rwlock_timedrdlock(pthread_rwlock_t *rwlock, const struct timespec *abstime) {
    struct posix_rwlock *r = rwlock ? posix_rwlock_word(rwlock) : NULL;
    if (!r || !abstime) return EINVAL;
    return rwlock_rdlock_abs(r, abstime);
}

int pthread_rwlock_wrlock(pthread_rwlock_t *rwlock) {
    struct posix_rwlock *r = rwlock ? posix_rwlock_word(rwlock) : NULL;
    return r ? rwlock_wrlock_abs(r, NULL) : EINVAL;
}

int pthread_rwlock_trywrlock(pthread_rwlock_t *rwlock) {
    struct posix_rwlock *r = rwlock ? posix_rwlock_word(rwlock) : NULL;
    if (!r) return EINVAL;
    int rc = pmutex_lock(&r->mu);
    if (rc) return rc;
    if (r->writer_active || r->readers > 0) {
        pmutex_unlock(&r->mu);
        return EBUSY;
    }
    r->writer_active = 1;
    pmutex_unlock(&r->mu);
    return 0;
}

int pthread_rwlock_timedwrlock(pthread_rwlock_t *rwlock, const struct timespec *abstime) {
    struct posix_rwlock *r = rwlock ? posix_rwlock_word(rwlock) : NULL;
    if (!r || !abstime) return EINVAL;
    return rwlock_wrlock_abs(r, abstime);
}

int pthread_rwlock_unlock(pthread_rwlock_t *rwlock) {
    struct posix_rwlock *r = rwlock ? posix_rwlock_word(rwlock) : NULL;
    return r ? rwlock_unlock_internal(r) : EINVAL;
}
