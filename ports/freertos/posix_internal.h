/* Internal plumbing shared by the POSIX layer. The public entry points are the
   POSIX functions declared by newlib's <pthread.h>, <sched.h> and <time.h>
   (the toolchain driver predefines _POSIX_THREADS etc., so those declarations
   are always visible). Types are exactly newlib's: pthread_t, pthread_mutex_t,
   pthread_cond_t and pthread_rwlock_t are __uint32_t words whose static
   initializer value 0xFFFFFFFF means "create the real object lazily on first
   use" (race-free, via an atomic compare-exchange on the word). */
#ifndef POSIX_INTERNAL_H
#define POSIX_INTERNAL_H

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#ifndef POSIX_DEFAULT_STACK_BYTES
/* Default pthread stack, in bytes. 32 KiB, not 16: a worker thread that builds PRISM
   snapshots (tests/prism_qemu) needs the headroom. */
#define POSIX_DEFAULT_STACK_BYTES (32 * 1024)
#endif

#define POSIX_MAX_KEYS 16
#define POSIX_UNINIT_WORD 0xFFFFFFFFu /* newlib's _PTHREAD_*_INITIALIZER value */

/* One per POSIX thread. A pthread_t is this pointer, as a uint32. The task
   keeps it in its thread-local-storage pointer 0. */
#define POSIX_THREAD_DETACHED 1
#define POSIX_THREAD_EXITED 2

struct posix_thread {
    void *(*start)(void *);
    void *arg;
    void *result;
    SemaphoreHandle_t done; /* binary semaphore, given once at thread exit */
    TaskHandle_t task;
    void *tls; /* thread pointer: 8 bytes before this thread's TLS data */
    /* POSIX_THREAD_DETACHED | POSIX_THREAD_EXITED, each set with one atomic fetch-or: the side
       that sees the other's bit already set frees the block, so exactly one side does. */
    int state;
    void *keys[POSIX_MAX_KEYS];
    struct posix_atexit *atexit; /* __cxa_thread_atexit registrations, LIFO */
};

struct posix_atexit {
    struct posix_atexit *next;
    void (*dtor)(void *);
    void *obj;
};

struct posix_mutex {
    SemaphoreHandle_t handle; /* plain (priority-inheriting) or recursive mutex */
    int type;                 /* PTHREAD_MUTEX_* */
    int recursive;
};

struct posix_cond_waiter {
    TaskHandle_t task;
    struct posix_cond_waiter *next;
};

struct posix_cond {
    struct posix_cond_waiter *waiters; /* guarded by a critical section */
};

struct posix_rwlock {
    struct posix_mutex mu;
    struct posix_cond cond;
    int readers;
    int writer_active;
};

/* freertos_posix.c */
struct posix_thread *posix_current_thread(void);
int posix_sched_running(void);
void *posix_alloc(size_t n);
void posix_free(void *p);
void posix_thread_exit(struct posix_thread *t, void *result) __attribute__((noreturn));

/* posix_mutex.c — struct-pointer ("internal") variants, also used by rwlocks */
int posix_mutex_init_struct(struct posix_mutex *m, int type);
struct posix_mutex *posix_mutex_new(int type);
void posix_mutex_free(struct posix_mutex *m);
struct posix_mutex *posix_mutex_word(volatile pthread_mutex_t *word);
int pmutex_lock(struct posix_mutex *m);
int pmutex_trylock(struct posix_mutex *m);
int pmutex_timedlock(struct posix_mutex *m, const struct timespec *abs);
int pmutex_unlock(struct posix_mutex *m);

/* posix_cond.c */
void posix_cond_init_struct(struct posix_cond *c);
struct posix_cond *posix_cond_new(void);
void posix_cond_free(struct posix_cond *c);
struct posix_cond *posix_cond_word(volatile pthread_cond_t *word);
int pcond_wait(struct posix_cond *c, struct posix_mutex *m, const struct timespec *abs);
int pcond_signal(struct posix_cond *c);
int pcond_broadcast(struct posix_cond *c);

/* posix_rwlock.c */
struct posix_rwlock *posix_rwlock_new(void);
void posix_rwlock_free(struct posix_rwlock *r);
struct posix_rwlock *posix_rwlock_word(volatile pthread_rwlock_t *word);

/* posix_time.c */
TickType_t posix_abs_to_ticks(const struct timespec *abs);

/* tls.c */
void posix_tls_setup(struct posix_thread *t);
void *posix_current_tp(void);

#endif /* POSIX_INTERNAL_H */
