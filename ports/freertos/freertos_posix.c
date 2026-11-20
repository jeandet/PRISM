/* POSIX threads on FreeRTOS: pthread_create/join/detach/self/equal/cancel/exit,
   pthread_once, thread-specific data keys, sched_yield and the pthread_attr_*
   functions. A pthread_t is a pointer (as uint32) to a struct posix_thread,
   kept in the task's thread-local-storage pointer 0 (index 0). */
#include "posix_internal.h"

/* All posix-layer allocations come from the FreeRTOS heap, so that heap
   accounting (xPortGetFreeHeapSize) covers control blocks, TLS blocks and
   kernel objects alike. */
void *posix_alloc(size_t n) { return pvPortMalloc(n); }
void posix_free(void *p) { vPortFree(p); }

int posix_sched_running(void) { return xTaskGetSchedulerState() == taskSCHEDULER_RUNNING; }

/* ---- current thread's control block ---- */

static struct posix_thread g_boot_thread; /* pre-scheduler context: single-threaded */
static int g_boot_ready;

static void thread_block_init(struct posix_thread *t, TaskHandle_t task, int with_sem) {
    memset(t, 0, sizeof *t);
    t->task = task;
    t->done = with_sem ? xSemaphoreCreateBinary() : NULL;
    posix_tls_setup(t); /* every thread, including the first task, gets a TLS block */
}

static struct posix_thread *thread_block_new(TaskHandle_t task) {
    struct posix_thread *t = posix_alloc(sizeof *t);
    if (!t) return NULL;
    thread_block_init(t, task, 1);
    if (!t->done) {
        posix_free(t->tls);
        posix_free(t);
        return NULL;
    }
    return t;
}

static void thread_block_free(struct posix_thread *t) {
    if (t->done) vSemaphoreDelete(t->done);
    posix_free(t->tls);
    posix_free(t);
}

struct posix_thread *posix_current_thread(void) {
    if (posix_sched_running()) {
        struct posix_thread *t = pvTaskGetThreadLocalStoragePointer(NULL, 0);
        if (t) return t;
        /* A task not created by pthread_create (e.g. the first task): create its
           control block lazily. Only the owning task touches its own slot. */
        t = thread_block_new(xTaskGetCurrentTaskHandle());
        if (!t) return NULL;
        vTaskSetThreadLocalStoragePointer(NULL, 0, t);
        return t;
    }
    if (!g_boot_ready) {
        thread_block_init(&g_boot_thread, NULL, 0);
        g_boot_ready = 1;
    }
    return &g_boot_thread;
}

/* ---- thread exit ---- */

static void run_atexit_list(struct posix_thread *t) {
    struct posix_atexit *n = t->atexit;
    while (n) {
        struct posix_atexit *next = n->next;
        n->dtor(n->obj);
        posix_free(n);
        n = next;
    }
    t->atexit = NULL;
}

static struct {
    volatile int in_use;
    void (*dtor)(void *);
} g_keys[POSIX_MAX_KEYS];

static void run_key_destructors(struct posix_thread *t) {
    for (int i = 0; i < POSIX_MAX_KEYS; i++) {
        if (!__atomic_load_n(&g_keys[i].in_use, __ATOMIC_ACQUIRE)) continue;
        void *v = t->keys[i];
        if (!v) continue;
        void (*d)(void *) = g_keys[i].dtor;
        t->keys[i] = NULL;
        if (d) d(v);
    }
    /* simplify: a single pass; PTHREAD_DESTRUCTOR_ITERATIONS re-runs are not done */
}

void posix_thread_exit(struct posix_thread *t, void *result) {
    t->result = result;
    run_atexit_list(t);
    run_key_destructors(t);
    if (t->done) xSemaphoreGive(t->done);
    if (__atomic_fetch_or(&t->state, POSIX_THREAD_EXITED, __ATOMIC_ACQ_REL) & POSIX_THREAD_DETACHED)
        thread_block_free(t);
    vTaskDelete(NULL); /* noreturn */
    for (;;) { }
}

static void pthread_trampoline(void *pv) {
    struct posix_thread *t = pv;
    vTaskSetThreadLocalStoragePointer(NULL, 0, t);
    posix_thread_exit(t, t->start(t->arg));
}

/* ---- threads ---- */

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start)(void *), void *arg) {
    if (!thread || !start) return EINVAL;
    size_t stack_bytes = POSIX_DEFAULT_STACK_BYTES;
    int detached = 0;
    if (attr) {
        if (!attr->is_initialized) return EINVAL;
        if (attr->stacksize > 0) stack_bytes = (size_t)attr->stacksize; /* honour setstacksize */
        if (attr->detachstate == PTHREAD_CREATE_DETACHED) detached = 1;
        else if (attr->detachstate != PTHREAD_CREATE_JOINABLE) return EINVAL;
    }
    struct posix_thread *t = thread_block_new(NULL);
    if (!t) return EAGAIN;
    t->start = start;
    t->arg = arg;
    t->state = detached ? POSIX_THREAD_DETACHED : 0;
    UBaseType_t prio = uxTaskPriorityGet(NULL); /* the creator's priority */
    configSTACK_DEPTH_TYPE depth =
        (configSTACK_DEPTH_TYPE)((stack_bytes + sizeof(StackType_t) - 1) / sizeof(StackType_t));
    if (depth < configMINIMAL_STACK_SIZE) depth = configMINIMAL_STACK_SIZE;
    TaskHandle_t h = NULL;
    if (xTaskCreate(pthread_trampoline, "pthread", depth, t, prio, &h) != pdPASS) {
        if (t->done) vSemaphoreDelete(t->done);
        posix_free(t->tls);
        posix_free(t);
        return EAGAIN;
    }
    t->task = h;
    *thread = (pthread_t)(uintptr_t)t;
    return 0;
}

int pthread_join(pthread_t thread, void **value_ptr) {
    struct posix_thread *t = (struct posix_thread *)(uintptr_t)thread;
    if (!t || !posix_sched_running()) return EINVAL;
    if (t == posix_current_thread()) return EDEADLK;
    if (__atomic_load_n(&t->state, __ATOMIC_ACQUIRE) & POSIX_THREAD_DETACHED) return EINVAL; /* joining a detached thread */
    if (xSemaphoreTake(t->done, portMAX_DELAY) != pdTRUE) return EINVAL;
    if (value_ptr) *value_ptr = t->result;
    vSemaphoreDelete(t->done);
    posix_free(t->tls);
    posix_free(t);
    return 0;
}

int pthread_detach(pthread_t thread) {
    struct posix_thread *t = (struct posix_thread *)(uintptr_t)thread;
    if (!t) return EINVAL;
    if (__atomic_fetch_or(&t->state, POSIX_THREAD_DETACHED, __ATOMIC_ACQ_REL) & POSIX_THREAD_EXITED)
        thread_block_free(t);   /* already exited while joinable: nobody will join */
    return 0;
}

pthread_t pthread_self(void) {
    struct posix_thread *t = posix_current_thread();
    return t ? (pthread_t)(uintptr_t)t : (pthread_t)0;
}

int pthread_equal(pthread_t a, pthread_t b) { return a == b; }

void pthread_exit(void *retval) { posix_thread_exit(posix_current_thread(), retval); }

int pthread_cancel(pthread_t thread) {
    (void)thread;
    return ENOSYS; /* no cancellation support */
}

/* ---- pthread_once ---- */

static pthread_mutex_t g_once_mutex = _PTHREAD_MUTEX_INITIALIZER;

int pthread_once(pthread_once_t *once_control, void (*init_routine)(void)) {
    if (!once_control || !init_routine) return EINVAL;
    if (__atomic_load_n(&once_control->init_executed, __ATOMIC_ACQUIRE)) return 0;
    int rc = pthread_mutex_lock(&g_once_mutex);
    if (rc) return rc;
    if (!__atomic_load_n(&once_control->init_executed, __ATOMIC_ACQUIRE)) {
        init_routine();
        __atomic_store_n(&once_control->init_executed, 1, __ATOMIC_RELEASE);
    }
    pthread_mutex_unlock(&g_once_mutex);
    return 0;
}

/* ---- thread-specific data keys ---- */

int pthread_key_create(pthread_key_t *key, void (*destructor)(void *)) {
    if (!key) return EINVAL;
    for (int i = 0; i < POSIX_MAX_KEYS; i++) {
        int expected = 0;
        if (__atomic_compare_exchange_n(&g_keys[i].in_use, &expected, 1, 0,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            g_keys[i].dtor = destructor;
            *key = (pthread_key_t)i;
            return 0;
        }
    }
    return EAGAIN; /* fixed table of POSIX_MAX_KEYS keys is full */
}

int pthread_key_delete(pthread_key_t key) {
    if (key >= (pthread_key_t)POSIX_MAX_KEYS) return EINVAL;
    if (!__atomic_load_n(&g_keys[key].in_use, __ATOMIC_ACQUIRE)) return EINVAL;
    g_keys[key].dtor = NULL;
    __atomic_store_n(&g_keys[key].in_use, 0, __ATOMIC_RELEASE);
    return 0; /* POSIX: existing values are not destructed on key deletion */
}

int pthread_setspecific(pthread_key_t key, const void *value) {
    if (key >= (pthread_key_t)POSIX_MAX_KEYS) return EINVAL;
    if (!__atomic_load_n(&g_keys[key].in_use, __ATOMIC_ACQUIRE)) return EINVAL;
    struct posix_thread *t = posix_current_thread();
    if (!t) return ENOMEM;
    t->keys[key] = (void *)value;
    return 0;
}

void *pthread_getspecific(pthread_key_t key) {
    if (key >= (pthread_key_t)POSIX_MAX_KEYS) return NULL;
    if (!__atomic_load_n(&g_keys[key].in_use, __ATOMIC_ACQUIRE)) return NULL;
    struct posix_thread *t = posix_current_thread();
    return t ? t->keys[key] : NULL;
}

/* ---- sched_yield ---- */

int sched_yield(void) {
    if (posix_sched_running()) taskYIELD();
    return 0;
}

/* ---- thread attributes ---- */

int pthread_attr_init(pthread_attr_t *attr) {
    if (!attr) return EINVAL;
    memset(attr, 0, sizeof *attr);
    attr->is_initialized = 1;
    attr->stacksize = POSIX_DEFAULT_STACK_BYTES;
    attr->detachstate = PTHREAD_CREATE_JOINABLE;
    attr->contentionscope = PTHREAD_SCOPE_SYSTEM;
    attr->inheritsched = PTHREAD_INHERIT_SCHED;
    attr->schedpolicy = SCHED_OTHER;
    return 0;
}

int pthread_attr_destroy(pthread_attr_t *attr) {
    if (!attr || !attr->is_initialized) return EINVAL;
    attr->is_initialized = 0;
    return 0;
}

int pthread_attr_setstacksize(pthread_attr_t *attr, size_t stacksize) {
    if (!attr || !attr->is_initialized) return EINVAL;
    if (stacksize < 1024) return EINVAL; /* keep a sane minimum */
    attr->stacksize = (int)stacksize;
    return 0;
}

int pthread_attr_getstacksize(const pthread_attr_t *attr, size_t *stacksize) {
    if (!attr || !attr->is_initialized || !stacksize) return EINVAL;
    *stacksize = (size_t)attr->stacksize;
    return 0;
}

int pthread_attr_setdetachstate(pthread_attr_t *attr, int detachstate) {
    if (!attr || !attr->is_initialized) return EINVAL;
    if (detachstate != PTHREAD_CREATE_DETACHED && detachstate != PTHREAD_CREATE_JOINABLE)
        return EINVAL;
    attr->detachstate = detachstate;
    return 0;
}

int pthread_attr_getdetachstate(const pthread_attr_t *attr, int *detachstate) {
    if (!attr || !attr->is_initialized || !detachstate) return EINVAL;
    *detachstate = attr->detachstate;
    return 0;
}

/* ---- __cxa_thread_atexit (Itanium ABI, used for thread_local destructors) ---- */

int __cxa_thread_atexit(void (*dtor)(void *), void *obj, void *dso_handle) {
    (void)dso_handle;
    struct posix_atexit *n = posix_alloc(sizeof *n);
    if (!n) return -1;
    n->dtor = dtor;
    n->obj = obj;
    struct posix_thread *t = posix_current_thread();
    if (!t) {
        posix_free(n);
        return -1;
    }
    n->next = t->atexit;
    t->atexit = n; /* LIFO, as the ABI requires; run by posix_thread_exit */
    return 0;
}
