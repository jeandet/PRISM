/* newlib's retargetable locking, on FreeRTOS mutexes.

   This toolchain's newlib is built with retargetable locking: its libc_a-lock.o
   provides no-op versions of the ten __retarget_lock_* functions below and of
   the eight __lock___* lock objects. the POSIX layer is linked whole, so defining ALL of
   them here keeps libc_a-lock.o out of the link entirely (a second definition
   would fail the link, so the set must be complete). With them defined, malloc,
   stdio, env, atexit, tz, ... are serialized for real.

   struct __lock is opaque to newlib (its <sys/lock.h> only forward-declares
   it), so we define it: one FreeRTOS mutex handle per lock object. The eight
   static objects start zeroed (.bss) and get their handle lazily, race-free:
   the create runs in a critical section and re-checks, so exactly one mutex
   is made per object however many threads race on it first.

   Before the scheduler starts (static constructors in __libc_init_array, main
   before vTaskStartScheduler) the boot context is single-threaded, so every
   operation is a no-op that succeeds: no handle is created, try_acquire
   returns success. simplify: locks taken from an ISR are not supported (no
   ISR here calls into newlib); the take/give calls would need the FromISR
   variants. */
#include "posix_internal.h"

struct __lock {
    SemaphoreHandle_t handle; /* NULL until created, see lock_handle() */
    int recursive;            /* the family that initialized/created it */
    int allocated;            /* made by __retarget_lock_init*: close frees it (never the statics) */
};

/* The locks newlib's libc locks its shared state with. Zero-initialized. */
struct __lock __lock___malloc_recursive_mutex;
struct __lock __lock___env_recursive_mutex;
struct __lock __lock___sfp_recursive_mutex;
struct __lock __lock___atexit_recursive_mutex;
struct __lock __lock___at_quick_exit_mutex;
struct __lock __lock___tz_mutex;
struct __lock __lock___dd_hash_mutex;
struct __lock __lock___arc4random_mutex;

/* The handle of `l`, creating it on first use. NULL before the scheduler runs
   (nothing to lock against then). `recursive` is the calling family's kind; an
   object already marked recursive stays recursive (a recursive mutex also
   serves single lock/unlock, so this is the safe direction). */
static SemaphoreHandle_t lock_handle(struct __lock *l, int recursive) {
    if (!posix_sched_running()) return NULL;
    taskENTER_CRITICAL();
    if (!l->handle) {
        int rec = recursive || l->recursive;
        l->handle = rec ? xSemaphoreCreateRecursiveMutex() : xSemaphoreCreateMutex();
        l->recursive = rec;
    }
    taskEXIT_CRITICAL();
    return l->handle;
}

/* init allocates a lock for a lock variable newlib owns (a FILE's lock, say) and stores it
   through `slot`; the handle itself still comes lazily, on the first acquire, so init is safe
   before the scheduler starts (heap_4 allocates without it). */
static void lock_init_family(struct __lock **slot, int recursive) {
    struct __lock *l = posix_alloc(sizeof *l);
    if (l) {
        l->handle = NULL;
        l->recursive = recursive;
        l->allocated = 1;
    }
    *slot = l; /* NULL on no memory: acquire/release then do nothing */
}

static void lock_close(struct __lock *l) {
    if (!l) return;
    if (l->handle) vSemaphoreDelete(l->handle);
    l->handle = NULL;
    if (l->allocated) posix_free(l);
}

void __retarget_lock_init(struct __lock **l) { lock_init_family(l, 0); }
void __retarget_lock_init_recursive(struct __lock **l) { lock_init_family(l, 1); }
void __retarget_lock_close(struct __lock *l) { lock_close(l); }
void __retarget_lock_close_recursive(struct __lock *l) { lock_close(l); }

void __retarget_lock_acquire(struct __lock *l) {
    if (!l) return;
    SemaphoreHandle_t h = lock_handle(l, 0);
    if (h) (void)xSemaphoreTake(h, portMAX_DELAY);
}

void __retarget_lock_acquire_recursive(struct __lock *l) {
    if (!l) return;
    SemaphoreHandle_t h = lock_handle(l, 1);
    if (h) (void)xSemaphoreTakeRecursive(h, portMAX_DELAY);
}

int __retarget_lock_try_acquire(struct __lock *l) {
    if (!l) return 1;
    SemaphoreHandle_t h = lock_handle(l, 0);
    if (!h) return 1; /* pre-scheduler: nothing contends, the lock succeeds */
    return xSemaphoreTake(h, 0) == pdTRUE ? 1 : 0;
}

int __retarget_lock_try_acquire_recursive(struct __lock *l) {
    if (!l) return 1;
    SemaphoreHandle_t h = lock_handle(l, 1);
    if (!h) return 1;
    return xSemaphoreTakeRecursive(h, 0) == pdTRUE ? 1 : 0;
}

void __retarget_lock_release(struct __lock *l) {
    if (!l || !l->handle) return;
    (void)xSemaphoreGive(l->handle);
}

void __retarget_lock_release_recursive(struct __lock *l) {
    if (!l || !l->handle) return;
    (void)xSemaphoreGiveRecursive(l->handle);
}
