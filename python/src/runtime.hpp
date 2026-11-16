#pragma once
// Logic-thread posting, transaction buffering, field dispatch, and the Slot/PyHolder plumbing.

#include "common.hpp"

// Defined near NB_MODULE below, alongside the rest of the prism.on_error() error hub —
// forward-declared here since every Python callback wrapper in this file uses it.
static void report_python_callback_error();

// Python-facing spelling of a scalar field type, for TypeError messages (validator
// rejects, derived recompute cast failures).
template <typename T>
static const char* py_type_name() {
    if constexpr (std::is_same_v<T, int>) return "int";
    else if constexpr (std::is_same_v<T, double>) return "float";
    else if constexpr (std::is_same_v<T, std::string>) return "str";
    else if constexpr (std::is_same_v<T, bool>) return "bool";
    else return "value";
}

// Global PostHandle for off-thread posting (set during prism.run, cleared after).
// Holds weak_ptrs to the mutation queue so post after model_app returns is not UAF.
static std::mutex g_handle_mutex;
static std::optional<AppContext::PostHandle> g_post_handle;
// The running app's context, for its platform services (file dialogs, message boxes); set by
// run()/_run_headless()'s setup, cleared with g_post_handle. Guarded by g_handle_mutex.
static AppContext* g_ctx = nullptr;
static std::atomic<bool> g_has_handle{false};
static std::atomic<bool> g_run_guard{false};
static std::atomic<bool> g_app_closed{false};
// simplify: g_app_closed is global, not per-generation. After run we clear it
// immediately so next test's pre-run dispatch is NoApp (direct) not Closed.
// This drops the post-close guarantee for late posts from the *old* Model
// after the next run starts — acceptable: production runs once, pytest
// reuses the same process sequentially. Proper per-generation tracking
// would keep Closed for old-gen handles until next gen installs.

// Test-only: makes the next run()/_run_headless() call throw immediately after
// acquiring g_run_guard, before touching the backend/model_app — a stand-in for
// model_app() itself throwing (no real path exists to trigger that from Python;
// every callback model_app calls synchronously is already exception-guarded).
// Exercises RunGuardReset below: a second run() after the forced failure must work.
static std::atomic<bool> g_fail_next_run{false};

// run()/_run_headless() must reset g_run_guard/g_has_handle/g_post_handle/g_app_closed
// on every exit path, including model_app() throwing — otherwise a failed run leaves
// g_run_guard stuck true and every subsequent run()/_run_headless() call raises
// "prism.run already running" forever. Constructed right after the CAS that claims
// g_run_guard, so its destructor always runs exactly once per successful claim,
// success or exception.
// prism.on_file_drop's callback, called on the logic thread for every drop; may be set before a
// run or during it.
static std::mutex g_file_drop_mutex;
static std::shared_ptr<struct PyHolder> g_file_drop;
// Models shown by prism.open_window, kept alive while their windows may exist (until the run ends).
static std::mutex g_window_models_mutex;
static std::vector<std::shared_ptr<struct PyHolder>> g_window_models;
static void call_file_drop(const prism::input::FileDrop& drop);

struct RunGuardReset {
    // Runs as run()/_run_headless() return, with the GIL held again: the windows are gone, so
    // the models prism.open_window kept for them can go too.
    ~RunGuardReset() {
        {
            std::lock_guard<std::mutex> lk(g_window_models_mutex);
            g_window_models.clear();
        }
        {
            std::lock_guard<std::mutex> lk(g_handle_mutex);
            g_post_handle.reset();
            g_ctx = nullptr;
            g_app_closed.store(true, std::memory_order_release);
        }
        g_has_handle.store(false, std::memory_order_release);
        g_run_guard.store(false, std::memory_order_release);
        g_app_closed.store(false, std::memory_order_release);
    }
};

enum class PostResult { Posted, NoApp, Closed };

static void drain_queue_loop(const std::shared_ptr<mpsc_queue<std::function<void()>>>& q,
                             const std::shared_ptr<std::atomic<bool>>& sf,
                             const std::shared_ptr<std::function<void()>>& tp) {
    do {
        prism::app::detail_in_mutation_batch() = true;
        while (auto f = q->pop()) {
            try {
                (*f)();
            } catch (...) {
                prism::core::report_unhandled_error(std::current_exception());
            }
        }
        prism::app::detail_in_mutation_batch() = false;
        if (tp && *tp) {
            try {
                (*tp)();
            } catch (...) {
                prism::core::report_unhandled_error(std::current_exception());
            }
        }
        sf->store(false, std::memory_order_release);
        if (q->empty()) break;
        bool exp = false;
        if (!sf->compare_exchange_strong(exp, true, std::memory_order_acq_rel)) break;
    } while (true);
}

static PostResult try_post_via_handle_impl(std::function<void()> fn, bool allow_logic_thread) {
    if (!allow_logic_thread && prism::app::detail_is_logic_thread()) return PostResult::NoApp;
    // Startup window: g_run_guard true but g_has_handle not yet set (setup hasn't run).
    // Don't fall back to direct unsync write — spin briefly for handle to appear.
    // 1000ms survives slow debug/CI runners; still NoApp/Closed check below handles overflow.
    // Only spin when this thread actually holds the GIL: that's the case where another
    // Python thread is mid-setup and releasing the GIL here lets it finish. The initial
    // view build (registry.add in model_app) calls this without the GIL (it runs under
    // _run_headless's own gil_scoped_release) — there setup can never complete regardless
    // of how long we spin, so skip straight to the direct read/dispatch below instead of
    // burning the full 1000ms.
    if (g_run_guard.load(std::memory_order_acquire) && !g_has_handle.load(std::memory_order_acquire) &&
        Py_IsInitialized() && PyGILState_Check()) {
        nb::gil_scoped_release release;
        for (int i = 0; i < 1000 && !g_has_handle.load(std::memory_order_acquire); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::optional<AppContext::PostHandle> hopt;
    {
        std::lock_guard<std::mutex> lk(g_handle_mutex);
        if (!g_has_handle.load(std::memory_order_acquire) || !g_post_handle) {
            return g_app_closed.load(std::memory_order_acquire) ? PostResult::Closed : PostResult::NoApp;
        }
        hopt = *g_post_handle;
    }
    auto& h = *hopt;
    auto q = h.queue.lock();
    if (!q) return PostResult::Closed;
    auto closed_flag = h.closed.lock();
    if (!closed_flag || closed_flag->load(std::memory_order_acquire)) {
        g_app_closed.store(true, std::memory_order_release);
        return PostResult::Closed;
    }
    auto sched_flag = h.scheduled.lock();
    if (!sched_flag) return PostResult::Closed;
    auto tail = h.drain_publish.lock();
    q->push(std::move(fn));
    bool expected = false;
    if (sched_flag->compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        auto qq = q;
        auto sf = sched_flag;
        auto tp = tail;
        auto sch = h.sched;
        exec::start_detached(stdexec::schedule(sch) | stdexec::then([qq, sf, tp, sch] {
            // AppContext::post's drain is now wrapped via logic_wrapper; keep GIL here too
            // for the try_post path which bypasses AppContext::post. Once the interpreter
            // is finalized, nothing queued here may run — it may hold Python callbacks.
            if (!Py_IsInitialized()) return;
            nb::gil_scoped_acquire gil;
            drain_queue_loop(qq, sf, tp);
        }));
    }
    return PostResult::Posted;
}

static bool try_post_via_handle(std::function<void()> fn) {
    return try_post_via_handle_impl(std::move(fn), false) == PostResult::Posted;
}

static PostResult try_post_any_thread(std::function<void()> fn) {
    return try_post_via_handle_impl(std::move(fn), true);
}

static void ensure_idle_wake() {
    if (prism::app::detail_in_mutation_batch()) return;
    (void)try_post_any_thread([] {});
}

#include <future>

// Serializes every direct (non-posted) dispatch fallback below. Those paths carry
// a "pre-run single-threaded" assumption that only ever held because the GIL
// serialized callers; on free-threaded builds several threads can run model
// mutations concurrently here (e.g. a worker pool storming plot ops before the
// app starts — the 3.14t segfault in test_worker_pool_plot_example). Recursive:
// a direct mutation can fire on_change into Python code that re-enters another
// direct dispatch on the same thread.
static std::recursive_mutex g_direct_dispatch_mutex;

template <typename T>
T dispatch_sync_read(std::function<T()> reader) {
    if (prism::app::detail_is_logic_thread()) return reader();
    // Startup window: spin briefly like the write path, and same GIL-holding
    // condition — see try_post_via_handle_impl. SlotDerived::get() is called from the
    // initial widget tree build (registry.add in model_app), which runs on the calling
    // thread under _run_headless's own gil_scoped_release (no GIL held here); skip the
    // spin there and fall straight through to the direct read below.
    if (g_run_guard.load(std::memory_order_acquire) && !g_has_handle.load(std::memory_order_acquire) &&
        Py_IsInitialized() && PyGILState_Check()) {
        nb::gil_scoped_release release;
        for (int i = 0; i < 1000 && !g_has_handle.load(std::memory_order_acquire); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    // Pre-run or post-close: direct read (single-threaded / drained).
    // Shutdown narrow window: closed=true but a drain continuation may still
    // be queued; wait briefly for the queue to empty before unsync read.
    {
        std::shared_ptr<mpsc_queue<std::function<void()>>> qcopy;
        bool do_direct = false;
        {
            std::lock_guard<std::mutex> lk(g_handle_mutex);
            if (!g_has_handle.load(std::memory_order_acquire) || !g_post_handle) {
                std::lock_guard<std::recursive_mutex> direct_lk(g_direct_dispatch_mutex);
                return reader();
            }
            if (g_app_closed.load(std::memory_order_acquire)) {
                qcopy = g_post_handle->queue.lock();
                do_direct = true;
            } else {
                auto closed_flag = g_post_handle->closed.lock();
                if (!closed_flag || closed_flag->load(std::memory_order_acquire)) {
                    qcopy = g_post_handle->queue.lock();
                    do_direct = true;
                }
            }
        }
        if (do_direct) {
            if (qcopy) {
                for (int i = 0; i < 50 && !qcopy->empty(); ++i)
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            std::lock_guard<std::recursive_mutex> direct_lk(g_direct_dispatch_mutex);
            return reader();
        }
    }
    auto prom = std::make_shared<std::promise<T>>();
    auto fut = prom->get_future();
    PostResult pr = try_post_via_handle_impl([reader = std::move(reader), prom]() mutable {
        try { prom->set_value(reader()); }
        catch (...) { try { prom->set_exception(std::current_exception()); } catch (...) {} }
    }, false);
    if (pr != PostResult::Posted) {
        // fallback: NoApp/Closed handled above, but safety
        std::lock_guard<std::recursive_mutex> direct_lk(g_direct_dispatch_mutex);
        return reader();
    }
    // Block caller (off logic thread) until logic thread runs reader — release GIL while
    // waiting, same guard as the spins above (this thread may not hold the GIL here either).
    {
        std::optional<nb::gil_scoped_release> rel;
        if (Py_IsInitialized() && PyGILState_Check()) rel.emplace();
        fut.wait();
    }
    return fut.get();
}

// Transaction buffering — Python thread-local coalescing into one logic-thread closure.
// Matches doc/design/python-sdk.md §2: `with prism.transaction():` enqueues single closure.
// Uses C++ thread_local(TransactionState) semantics but buffered per-Python-thread pre-dispatch.
inline thread_local int txn_depth = 0;
inline thread_local std::vector<std::function<void()>> txn_queue;
inline thread_local std::vector<size_t> txn_marks;
inline bool txn_active() { return txn_depth > 0; }

inline void txn_flush_batch() {
    if (txn_queue.empty()) return;
    auto batch = std::move(txn_queue);
    txn_queue.clear();
    txn_queue.shrink_to_fit();
    txn_marks.clear();
    if (prism::app::detail_is_logic_thread()) {
        // GIL needed: field->set will emit copying handlers holding Python objects.
        nb::gil_scoped_acquire gil;
        prism::TransactionGuard g;
        for (auto& fn : batch) fn();
        // If inside outer batch drain, defer publish to outer tail; else wake directly via tail if available.
        if (prism::app::detail_in_mutation_batch()) return;
        // Try direct tail if we have handle, else fallback to coalesced wake.
        {
            std::optional<AppContext::PostHandle> hopt;
            {
                std::lock_guard<std::mutex> lk(g_handle_mutex);
                if (g_has_handle.load(std::memory_order_acquire) && g_post_handle) hopt = *g_post_handle;
            }
            if (hopt) {
                auto tail = hopt->drain_publish.lock();
                auto closed_flag = hopt->closed.lock();
                if (tail && *tail && closed_flag && !closed_flag->load(std::memory_order_acquire)) {
                    (*tail)();
                    return;
                }
            }
        }
        ensure_idle_wake();
        return;
    }
    // Off-thread: posted to a live app, dropped once it closed, or applied directly with no
    // app -- decided as a single set decides it (try_post_via_handle_impl), which waits out
    // the startup window where the app runs but cannot take posts yet. Deciding here without
    // that wait applied the batch directly while the logic thread was already running.
    auto shared = std::make_shared<std::vector<std::function<void()>>>(std::move(batch));
    auto apply = [shared] {
        prism::TransactionGuard g;
        for (auto& fn : *shared) fn();
    };
    if (try_post_via_handle_impl(apply, false) != PostResult::NoApp) return;
    std::lock_guard<std::recursive_mutex> direct_lk(g_direct_dispatch_mutex);
    apply();
}

template <typename T>
inline bool txn_buffer_or_dispatch(std::shared_ptr<void> keep, Field<T>* field, const T& v) {
    if (!txn_active()) return false;
    T copy = v;
    txn_queue.emplace_back([keep, field, copy = std::move(copy)]() mutable {
        field->set(std::move(copy));
    });
    return true;
}

// Helper to post or direct-set a Field. `keep` is the owning shared_ptr (standalone
// handle state, or a Bound* handle's SlotBase owner) that the posted closure must
// hold so `field` cannot be freed before the logic thread runs it.
template <typename T>
void field_set_dispatch(std::shared_ptr<void> keep, Field<T>* field, T v) {
    if (txn_buffer_or_dispatch(keep, field, v)) return;
    if (!prism::app::detail_is_logic_thread()) {
        T copy = v;
        auto res = try_post_via_handle_impl([keep, field, copy = std::move(copy)]() mutable {
            field->set(std::move(copy));
        }, false);
        if (res == PostResult::Posted) return;
        if (res == PostResult::Closed) return; // post-close: no-op per spec (no direct fallback)
        // NoApp: fall through to direct (pre-run single-threaded)
    }
    if (prism::app::detail_is_logic_thread() && Py_IsInitialized()) {
        nb::gil_scoped_acquire gil;
        field->set(std::move(v));
    } else {
        std::lock_guard<std::recursive_mutex> direct_lk(g_direct_dispatch_mutex);
        field->set(std::move(v));
    }
}

// apply_validator is defined near NB_MODULE (needs nb::dict/attr access on `self`);
// forward-declared here so field_add_dispatch below can call it.
template <typename T> T apply_validator(nb::object self, T v);

// Releases one nb::object's Python reference, guarded against the two hazards every
// destructor in this file that can run off a posted/queued closure faces: the
// mutation queue is destroyed by run() with the GIL released (model_app.hpp:190),
// and CPython flips Py_IsInitialized() to false partway through Py_FinalizeEx(),
// well before it finishes clearing module globals and running the final GC passes
// — so ordinary, still-GIL-holding teardown (e.g. a module-level Model dropping to
// a zero refcount at shutdown) can run with Py_IsInitialized() already false.
// Checking GIL-held first (not Py_IsInitialized()) is therefore the right signal:
// decref'ing is safe whenever this thread already holds the GIL, regardless of that
// flag — only fall back to leaking (`.release()`) when there's truly no GIL to
// decref under and no interpreter left to acquire one on. Shared by every site that
// used to duplicate this dance: PyHolder, SlotDerived, SlotTree, WeakCallback.
static void release_py_object_guarded(nb::object& obj) {
    if (!obj.ptr()) return;
    if (PyGILState_Check()) { obj.reset(); return; }
    if (!Py_IsInitialized()) { obj.release(); return; }
    nb::gil_scoped_acquire g;
    obj.reset();
}

// Owns exactly one nb::object for a closure that may sit in the mutation queue and
// be destroyed with the GIL released (model_app.hpp:190) or after interpreter
// shutdown. Move-only (copied via shared_ptr<PyHolder> where a closure needs to be
// copy-constructible for std::function). The destructor re-acquires the GIL before
// releasing the Python reference if the interpreter is still alive; otherwise it
// leaks the reference (`.release()`, same idiom as g_observed_handles above) rather
// than decref'ing into a torn-down interpreter.
struct PyHolder {
    nb::object obj;
    PyHolder() = default;
    explicit PyHolder(nb::object o) : obj(std::move(o)) {}
    PyHolder(const PyHolder&) = delete;
    PyHolder& operator=(const PyHolder&) = delete;
    PyHolder(PyHolder&&) = default;
    PyHolder& operator=(PyHolder&&) = default;
    ~PyHolder() { release_py_object_guarded(obj); }
};

// Waits through a run's startup (it began, but its setup has not stored the context yet), giving
// up the GIL meanwhile since startup may need it to build the view. Throws outside a run. Call
// without g_handle_mutex; then check g_ctx again under it (the run may end in between).
static void wait_for_context(const char* who) {
    for (int i = 0; i < 1000; ++i) {
        {
            std::lock_guard<std::mutex> lk(g_handle_mutex);
            if (g_ctx) return;
        }
        if (!g_run_guard.load(std::memory_order_acquire)) break;
        if (PyGILState_Check()) {
            nb::gil_scoped_release release;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    throw std::runtime_error(std::string(who) + " needs a running app (prism.run or prism.headless)");
}

static void call_file_drop(const prism::input::FileDrop& drop) {
    std::shared_ptr<PyHolder> fn;
    {
        std::lock_guard<std::mutex> lk(g_file_drop_mutex);
        fn = g_file_drop;
    }
    if (!fn || !Py_IsInitialized()) return;
    nb::gil_scoped_acquire gil;
    try { fn->obj(drop.paths, drop.position.x.raw(), drop.position.y.raw()); }
    catch (...) { report_python_callback_error(); }
}

// Atomic `field.add(n)` for int/float fields: get()+set() must both happen on the
// logic thread (the only thread allowed to touch Field<T> directly) so concurrent
// add() calls from many threads never race on a read-modify-write — each becomes
// one closure that runs to completion before the next drains. `self` is the Python
// handle (BoundField<T>/FieldHandle<T>) so the field's validator, if any, can be
// applied to the *summed* value — the caller's thread never sees that value, so
// unlike set()/`.value =` it cannot validate before dispatch; a rejection here is
// reported via prism.on_error() instead of raising back to the calling thread.
// simplify: does not participate in prism.transaction() buffering (unlike
// field_set_dispatch) — add() always dispatches its own closure immediately.
//
// The posted closure must never capture `self`/an nb::object directly (queued
// closures are destroyed by run() with the GIL released — model_app.hpp:190). The
// common case (no validator installed) posts a closure holding only `keep`, the raw
// field pointer, and `n` — no Python objects at all. Only when a validator is
// present does the closure capture one, via a PyHolder behind a shared_ptr (so the
// closure itself stays trivially copyable without touching the GIL).
template <typename T>
void field_add_dispatch(nb::object self, std::shared_ptr<void> keep, Field<T>* field, T n) {
    nb::dict d = self.attr("__dict__");
    bool has_validator = d.contains("_prism_validator");
    std::function<void()> do_add;
    if (has_validator) {
        auto held_self = std::make_shared<PyHolder>(self);
        do_add = [held_self, field, n]() {
            T new_value = field->get() + n;
            try {
                new_value = apply_validator<T>(held_self->obj, new_value);
            } catch (...) {
                report_python_callback_error();
                return;
            }
            field->set(std::move(new_value));
        };
    } else {
        do_add = [field, n]() { field->set(field->get() + n); };
    }
    if (prism::app::detail_is_logic_thread()) {
        // No Py_IsInitialized()/gil_scoped_acquire dance needed here (unlike
        // field_set_dispatch/list_op_dispatch): this function takes `self` as
        // nb::object, so the GIL is already held for the `self.attr(...)`
        // access above — every caller reaches this point via a nanobind-bound
        // method, never from a GIL-released C++-only path.
        do_add();
        return;
    }
    auto res = try_post_via_handle_impl([keep, do_add]() mutable { do_add(); }, false);
    if (res == PostResult::Posted) return;
    if (res == PostResult::Closed) return; // post-close: no-op per spec (no direct fallback)
    {   // NoApp: pre-run, serialized with the other direct fallbacks
        std::lock_guard<std::recursive_mutex> direct_lk(g_direct_dispatch_mutex);
        do_add();
    }
}

// Standalone-handle state (Field<T>/List<T> here, Shared<T>/Channel<T> below) is
// heap-allocated and tracked by a live count, exposed to Python as
// _standalone_state_alive_count() — a regression probe for the self-owning-hub leak
// fixed below: state must reach zero once every handle referencing it is gone and
// disconnected, not just have its Python wrapper object collected.
static std::atomic<int64_t> g_standalone_state_alive{0};

template <typename U, typename... Args>
std::shared_ptr<U> make_tracked_state(Args&&... args) {
    g_standalone_state_alive.fetch_add(1, std::memory_order_relaxed);
    return std::shared_ptr<U>(new U(std::forward<Args>(args)...), [](U* p) {
        delete p;
        g_standalone_state_alive.fetch_sub(1, std::memory_order_relaxed);
    });
}

// Wraps a Python observer callback for storage inside a C++ SenderHub::receivers_ entry
// without giving receivers_ its own strong reference to it. The callback's only strong
// reference is meant to live in the observing handle's __dict__["_prism_callbacks"]
// (appended by keep_connection() below) — a second, independent strong nb::callable copy
// held here would be a reference the cyclic GC can't see (plain C++ storage has no
// tp_traverse), which is exactly what used to pin a self-capturing observer's
// Model -> handle -> callback -> Model cycle forever, uncollectable, even once nothing
// else referenced the Model (Python's GC can only reclaim a cycle if every strong
// reference into it is visible via tp_traverse). A weakref doesn't contribute to the
// callback's refcount, so once the handle (and its callbacks list) becomes unreachable
// and is collected, this weakref starts resolving to None and operator() below silently
// no-ops — same observable behavior as an already-disconnected observer. Falls back to
// holding `cb` directly (old behavior) for the rare callable that can't be
// weak-referenced (some C-implemented callables) — such an observer keeps working, just
// without this fix's GC-visibility for that one case. On CPython 3.15 this fallback is
// never actually exercised: builtins, bound methods and partials are all weak-referenceable
// there, so nb::weakref(cb) always succeeds — the strong_ path exists for whatever
// C-implemented callable a future/other interpreter build declines to weak-reference; this
// is version-dependent, not a guarantee.
class WeakCallback {
public:
    explicit WeakCallback(nb::callable cb) {
        try {
            weak_.emplace(cb);
        } catch (nb::python_error&) {
            strong_ = std::move(cb);
        }
    }

    // Copying (e.g. SenderHub::emit()'s receiver snapshot) plain member-copies
    // weak_/strong_, which increfs the underlying PyObject* via nb::object's copy
    // constructor — always safe here because every copy site runs on the logic
    // thread with the GIL already held (construction from a nanobind-bound method,
    // or emit()'s snapshot inside a receiver callback). Declared explicitly
    // (instead of left implicit) only to avoid -Wdeprecated-copy-dtor now that
    // ~WeakCallback is user-declared; move likewise.
    WeakCallback(const WeakCallback&) = default;
    WeakCallback& operator=(const WeakCallback&) = default;
    WeakCallback(WeakCallback&&) = default;
    WeakCallback& operator=(WeakCallback&&) = default;

    template <typename... Args>
    void operator()(Args&&... args) const {
        if (!Py_IsInitialized()) return;
        nb::gil_scoped_acquire acq;
        nb::object target = strong_ ? nb::object(strong_) : (*weak_)();
        if (!target || target.is_none()) return;
        try { target(std::forward<Args>(args)...); } catch (...) { report_python_callback_error(); }
    }

    // A WeakCallback lives inside a SenderHub receiver entry, and a SenderHub (and
    // therefore this object) can be destroyed when the owning SlotBase's last
    // shared_ptr drops from a queued closure's `keep` — destroyed by run() with the
    // GIL released (model_app.hpp:190), same hazard PyHolder/SlotDerived/SlotTree
    // above guard against. Release weak_/strong_ the same guarded way instead of
    // decref'ing unconditionally.
    ~WeakCallback() {
        if (weak_) release_py_object_guarded(*weak_);
        release_py_object_guarded(strong_);
    }

private:
    std::optional<nb::weakref> weak_;
    nb::callable strong_;
};

// Shared one-sentence docstring for every observe*() .def below (see keep_connection()'s
// rationale comment further down for the full ownership story).
static constexpr const char* kObserveDoc =
    "Any thread. The subscription lives as long as this handle (or its Model) does -- "
    "keep a reference to the handle to keep the observer firing; disconnect() on the "
    "returned Connection ends it early.";

// Standalone field (owns storage) — for quick tests / non-model usage. State lives behind
// a shared_ptr (like SharedHandle/ChannelHandle below) so observe()'s Connection can
// keep_alive(field) instead of relying on nanobind's nb::keep_alive<0,1> on the handle
// itself — see the SharedHandle rationale comment below for why that matters.
template <typename T>
struct FieldHandle {
    std::shared_ptr<Field<T>> field;
    FieldHandle(T init) : field(make_tracked_state<Field<T>>(std::move(init))) {}
    FieldHandle(const FieldHandle&) = delete;
    FieldHandle& operator=(const FieldHandle&) = delete;
    FieldHandle(FieldHandle&&) = delete;
    FieldHandle& operator=(FieldHandle&&) = delete;
    T get() const {
        Field<T>* p = field.get();
        return dispatch_sync_read<T>([p](){ return p->get(); });
    }
    void set(T v) { field_set_dispatch(field, field.get(), std::move(v)); }
    Connection observe(nb::callable cb) {
        auto conn = field->on_change().connect(WeakCallback(cb));
        conn.keep_alive(field);
        return conn;
    }
};

// Standalone Shared<T>/Channel<T> handles (built via nb::init<>, not owned by a PyModel) are not
// in any PyModel::slots vector, so PyModel::drain() never reaches them. Register their drain
// functions here so PyModel::drain() can sweep them too — see drain_standalone() below.
//
// The registry holds weak_ptrs, not raw pointers: a Python observer callback invoked mid-sweep
// (GIL held) can drop the last reference to ANOTHER standalone handle, running its destructor
// and freeing its drain_fn while the snapshot below still references it. lock()ing each weak_ptr
// takes a shared_ptr copy that keeps the function alive for the duration of that one call, even
// if the handle dies inside it; an already-expired entry is simply skipped, never dereferenced.
struct StandaloneDrainers {
    std::mutex m;
    std::vector<std::weak_ptr<std::function<void()>>> fns;
    static StandaloneDrainers& instance() {
        static StandaloneDrainers inst;
        return inst;
    }
};

static void register_standalone_drainer(std::shared_ptr<std::function<void()>> fn) {
    auto& reg = StandaloneDrainers::instance();
    std::lock_guard<std::mutex> lk(reg.m);
    reg.fns.push_back(std::move(fn));
}

// Called from a handle's destructor after it has reset its own drain_fn shared_ptr (so its
// entry is now expired); sweeps out every expired entry, not just this one, so the registry
// doesn't accumulate dead weak_ptrs across many short-lived standalone handles.
static void prune_expired_standalone_drainers() {
    auto& reg = StandaloneDrainers::instance();
    std::lock_guard<std::mutex> lk(reg.m);
    reg.fns.erase(std::remove_if(reg.fns.begin(), reg.fns.end(),
                                  [](const auto& w) { return w.expired(); }),
                  reg.fns.end());
}

static void drain_standalone() {
    std::vector<std::weak_ptr<std::function<void()>>> snapshot;
    {
        auto& reg = StandaloneDrainers::instance();
        std::lock_guard<std::mutex> lk(reg.m);
        snapshot = reg.fns;
    }
    for (auto& weak : snapshot) {
        if (auto fn = weak.lock()) (*fn)();
    }
}

// Type-erased slot for PyModel view — defined before Bound* so they can hold shared_ptr to it.
//
// traverse()/clear() let PyModel's tp_traverse/tp_clear (see NB_MODULE below) participate
// in Python's cyclic GC: a slot that holds an nb::object reaching back into its owning
// Model (SlotDerived's py_fn/dep_keepalive_, SlotTree's py_src_holder) is otherwise a
// GC-invisible edge in the Model -> slots -> callback -> Model cycle. Plain Slot/SlotShared/
// SlotChannel/SlotList/SlotPlot hold no nb::object, so the no-op default is correct for them.
struct SlotBase {
    virtual ~SlotBase() = default;
    virtual void build(ViewBuilder& vb) = 0;
    virtual void drain() {}
    virtual void traverse(visitproc /*visit*/, void* /*arg*/) {}
    virtual void clear() {}
};
template <typename T>
struct Slot : SlotBase {
    Field<T> field;
    explicit Slot(T v) : field(std::move(v)) {}
    void build(ViewBuilder& vb) override { vb.widget(field); }
};
template <typename T>
struct SlotShared : SlotBase {
    Shared<T> shared;
    explicit SlotShared(T v) : shared(std::move(v)) {}
    void build(ViewBuilder& vb) override { vb.widget(shared); }
    void drain() override { shared.drain_notifications(); }
};
template <typename T>
struct SlotChannel : SlotBase {
    Channel<T> channel;
    SlotChannel() = default;
    void build(ViewBuilder& vb) override { (void)vb; /* invisible — drain via PyModel::drain */ }
    void drain() override { channel.drain_notifications(); }
};
template <typename T>
struct SlotList : SlotBase {
    List<T> list;
    Field<std::optional<std::size_t>> selected{std::nullopt};   // vb.list(h, selectable=True)
    explicit SlotList(std::vector<T> init = {}) { for (auto v : init) list.push_back(v); }
    void build(ViewBuilder& vb) override { vb.list(list); }
};
// Slider<T,O>/Checkbox (include/prism/ui/delegate.hpp) are plain structs
// (value/min/max/step, checked/label) each held whole inside one Field —
// unlike Slot<T>'s scalar Field<T>, there is no separate Field<double>/
// Field<bool> to point a BoundField at, so these get their own slot types.
//
// Orientation O is Slider<T,O>'s only template parameter and it does not
// affect the struct's data layout (value/min/max/step) at all — it only
// selects which Widget<Slider<T,O>> specialization renders it (delegate.hpp
// already defines that specialization generically over O, so instantiating
// it for Vertical here needs no new widget code). SlotSliderT<O> is
// templated purely so both orientations get their own Field<Slider<double,O>>
// storage; SlotSlider stays the Horizontal alias other code already refers
// to by that name.
template <Orientation O>
struct SlotSliderT : SlotBase {
    Field<Slider<double, O>> field;
    explicit SlotSliderT(Slider<double, O> v) : field(std::move(v)) {}
    void build(ViewBuilder& vb) override { vb.widget(field); }
};
using SlotSlider = SlotSliderT<Orientation::Horizontal>;
using SlotSliderV = SlotSliderT<Orientation::Vertical>;
struct SlotCheckbox : SlotBase {
    Field<Checkbox> field;
    explicit SlotCheckbox(Checkbox v) : field(std::move(v)) {}
    void build(ViewBuilder& vb) override { vb.widget(field); }
};
// Any widget struct held in one Field (radio group, spin box, progress bar).
template <typename S>
struct SlotWidget : SlotBase {
    Field<S> field;
    explicit SlotWidget(S v) : field(std::move(v)) {}
    void build(ViewBuilder& vb) override { vb.widget(field); }
};

// Forward declarations for dispatch helpers used by Plot/Tree handles
inline void list_op_dispatch(std::shared_ptr<void> keep, std::function<void()> fn);
template <typename T> T dispatch_sync_read(std::function<T()> reader);
template <typename T> void field_set_dispatch(std::shared_ptr<void> keep, Field<T>* field, T v);
