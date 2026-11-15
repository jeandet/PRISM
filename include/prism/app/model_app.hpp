#pragma once

#include <prism/ui/animation.hpp>
#include <prism/app/backend.hpp>
#include <prism/core/exec.hpp>
#include <prism/input/input_event.hpp>
#include <prism/app/widget_tree.hpp>
#include <prism/ui/window_chrome.hpp>
#include <prism/app/event_routing.hpp>
#include <prism/app/window_registry.hpp>
#include <prism/app/window_geometry.hpp>
#include <prism/app/deadline_timer.hpp>
#ifdef PRISM_DEBUG_TOOLS_ENABLED
#include <prism/widgets/debug/tree_inspector.hpp>
#endif

#include <cstdint>
#include <optional>
#include <thread>
#include <variant>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <prism/core/error_hub.hpp>
#include <prism/core/mpsc_queue.hpp>

namespace prism::app {
using namespace prism::core;
using namespace prism::input;
using namespace prism::ui;

inline thread_local bool detail_is_logic_thread = false;
inline thread_local bool detail_in_mutation_batch = false;

class AppContext {
public:
    using scheduler_type = decltype(std::declval<stdexec::run_loop>().get_scheduler());

    explicit AppContext(scheduler_type s, AnimationClock& c, Window& w, Backend& b,
                         WindowRegistry& r, std::function<void(const KeyPress&)>& key_handler,
                         std::function<void()>& post_dispatch_hook,
                         std::shared_ptr<mpsc_queue<std::function<void()>>> q,
                         std::shared_ptr<std::atomic<bool>> scheduled,
                         std::shared_ptr<std::atomic<bool>> closed,
                         std::shared_ptr<std::function<void()>> drain_publish,
                         std::shared_ptr<std::function<void(std::function<void()>)>> logic_wrapper = nullptr)
        : sched_(s), clock_(&c), window_(&w), backend_(&b), registry_(&r),
          key_handler_(&key_handler), post_dispatch_hook_(&post_dispatch_hook),
          queue_(std::move(q)), scheduled_(std::move(scheduled)),
          closed_(std::move(closed)), drain_publish_(std::move(drain_publish)),
          logic_wrapper_(std::move(logic_wrapper)) {}

    // Keep old 3-arg ctor for tests that construct BackendBase directly without queue —
    // delegates to the full ctor with empty queue (post becomes no-op).
    explicit AppContext(scheduler_type s, AnimationClock& c, Window& w, Backend& b,
                         WindowRegistry& r, std::function<void(const KeyPress&)>& key_handler,
                         std::function<void()>& post_dispatch_hook)
        : AppContext(s, c, w, b, r, key_handler, post_dispatch_hook,
                     std::make_shared<mpsc_queue<std::function<void()>>>(),
                     std::make_shared<std::atomic<bool>>(false),
                     std::make_shared<std::atomic<bool>>(false),
                     std::make_shared<std::function<void()>>()) {}

    AppContext(const AppContext&) = delete;              // wake_gate_ points at this object
    AppContext& operator=(const AppContext&) = delete;
    ~AppContext() { wake_gate_->close(); }

    scheduler_type scheduler() const { return sched_; }
    AnimationClock& clock() { return *clock_; }
    Window& window() { return *window_; }
    Backend& backend() { return *backend_; }
    WindowRegistry& registry() { return *registry_; }
    void set_global_key_handler(std::function<void(const KeyPress&)> fn) {
        *key_handler_ = std::move(fn);
    }
    void set_post_dispatch_hook(std::function<void()> fn) {
        *post_dispatch_hook_ = std::move(fn);
    }
    // Shows a file dialog (see PlatformServices); `done` runs on the logic thread with the
    // chosen paths -- empty when cancelled, or with `error` set when the platform has no file
    // dialogs or the dialog failed. Does not block. Not called if the app quits first.
    void choose_files(FileDialog dialog, std::function<void(FileDialogResult)> done) {
        auto on_logic_thread = [gate = wake_gate_, done = std::move(done)](FileDialogResult r) mutable {
            gate->post([done = std::move(done), r = std::move(r)]() mutable { done(std::move(r)); });
        };
        auto* services = backend_->platform_services();
        if (!services) return on_logic_thread({{}, "this backend has no file dialogs"});
        services->show_file_dialog(window_->id(), std::move(dialog), std::move(on_logic_thread));
    }

    // Called on the logic thread with every file drop (the paths and where they were dropped),
    // after the widget under the drop has seen it.
    void on_file_drop(std::function<void(const FileDrop&)> fn) { file_drop_ = std::move(fn); }

    // Opens a window showing `model` with its own view, and returns its id (0 if the backend
    // could not make one). The model may read other models' fields; it must outlive the window.
    // When the user closes it, it goes; the app goes on. Logic thread.
    template <typename Model>
    WindowId open_window(Model& model, WindowConfig config) {
        Window* win = backend_->request_window(std::move(config));
        if (!win) return 0;
        WindowId id = registry_->add(*win, model);
        if (auto* e = registry_->find(id)) e->tree->set_theme(theme_);   // also gets it published
        return id;
    }
    [[nodiscard]] bool window_open(WindowId id) const { return registry_->find(id) != nullptr; }
    // Closes a window open_window opened (not the main one: closing that ends the app).
    void close_window(WindowId id) {
        if (id == window_->id() || !window_open(id)) return;
        registry_->remove(id);
        backend_->close_window(id);
        requested_.erase(id);   // a widget's window (below): its widget asked for this, no callback
        on_window_closed_.erase(id);
    }

    // Lets the widgets of `tree` ask for windows of their own (a dock's floating panels,
    // WidgetTree::request_window). Requests run after the current frame, never inside one.
    void serve_window_requests(WidgetTree& tree) {
        tree.set_window_opener(
            [this](WidgetTree::WindowRequest r) { post([this, r = std::move(r)] { open_requested(r); }); },
            [this](WindowId id) { post([this, id] { close_window(id); }); });
    }
    // From the event loop: the user closed window `id` (already gone from the registry).
    void window_closed(WindowId id) {
        auto it = on_window_closed_.find(id);
        if (it == on_window_closed_.end()) return;
        auto done = std::move(it->second);
        on_window_closed_.erase(it);
        requested_.erase(id);
        if (done) done();
    }

    // Every window's look: ui::light_theme(), ui::dark_theme() or the app's own. Stops following
    // the OS's preference.
    void set_theme(const Theme& t) {
        follow_system_theme_ = false;
        apply_theme(t);
    }
    // Light or dark as the OS is set (dark until the backend says), now and whenever it changes.
    void follow_system_theme() {
        follow_system_theme_ = true;
        apply_theme(system_dark_ ? dark_theme() : light_theme());
    }
    [[nodiscard]] bool system_prefers_dark() const { return system_dark_; }
    [[nodiscard]] const Theme& theme() const { return theme_; }   // the windows' current one

    // Keeps `g` at the main window's size and place as the user changes them (whole window,
    // title bar included, as WindowConfig takes it). `g` must outlive the run.
    void remember_window_size(WindowGeometry& g) { geometry_ = &g; }
    // From the event loop, on the main window's WindowResize (its content size).
    void main_window_resized(const WindowResize& r) {
        if (!geometry_) return;
        geometry_->maximized.set(r.maximized);
        if (r.maximized) return;   // the size to come back to stays
        int w = r.width, content_h = r.height;
        bool custom = window_->decoration_mode() == DecorationMode::Custom;
        geometry_->width.set(w);
        geometry_->height.set(content_h + (custom ? static_cast<int>(WindowChrome::title_bar_h.raw()) : 0));
    }
    // From the event loop, on the main window's WindowMove.
    void main_window_moved(const WindowMove& m) {
        if (!geometry_ || m.maximized) return;
        geometry_->x.set(m.x);
        geometry_->y.set(m.y);
        geometry_->positioned.set(true);
    }
    // From the event loop, on a SystemTheme event.
    void system_theme_changed(bool dark) {
        system_dark_ = dark;
        if (follow_system_theme_) apply_theme(dark ? dark_theme() : light_theme());
    }
    [[nodiscard]] const std::function<void(const FileDrop&)>& file_drop_handler() const { return file_drop_; }

    // A message box, for what the app cannot show in its own window. Ignored when the
    // platform has none.
    void show_message(MessageKind kind, std::string title, std::string text) {
        if (auto* services = backend_->platform_services())
            services->show_message(window_->id(), kind, std::move(title), std::move(text));
    }

    // Injectable wrapper for logic-thread work (e.g. GIL acquire for Python callbacks).
    // Keep SDL-free: caller provides the wrapping logic.
    void set_logic_wrapper(std::function<void(std::function<void()>)> w) {
        if (logic_wrapper_) *logic_wrapper_ = std::move(w);
        else logic_wrapper_ = std::make_shared<std::function<void(std::function<void()>)>>(std::move(w));
    }
    static void apply_wrapper(const std::shared_ptr<std::function<void(std::function<void()>)>>& w,
                              std::function<void()> fn) {
        if (w && *w) (*w)(std::move(fn));
        else fn();
    }
    void run_wrapped(std::function<void()> fn) const {
        apply_wrapper(logic_wrapper_, std::move(fn));
    }

    struct PostHandle {
        std::weak_ptr<mpsc_queue<std::function<void()>>> queue;
        std::weak_ptr<std::atomic<bool>> scheduled;
        std::weak_ptr<std::atomic<bool>> closed;
        std::weak_ptr<std::function<void()>> drain_publish;
        scheduler_type sched;
    };
    PostHandle post_handle() const { return {queue_, scheduled_, closed_, drain_publish_, sched_}; }
    std::shared_ptr<std::function<void(std::function<void()>)>> logic_wrapper_ptr() const { return logic_wrapper_; }

    void post(std::function<void()> fn) {
        if (!fn) return;
        if (closed_ && closed_->load(std::memory_order_acquire)) return;
        if (detail_is_logic_thread) {
            if (detail_in_mutation_batch) {
                // Re-entrant from within a batch drain — defer publish to the
                // outer batch's tail (publish-after-whole-batch ordering).
                run_wrapped(std::move(fn));
                return;
            }
            run_wrapped([&]{
                fn();
                if (drain_publish_ && *drain_publish_) (*drain_publish_)();
            });
            return;
        }
        post_deferred(std::move(fn));
    }

    // Queue fn for a later run_loop turn, then drain all Shared/Channel and publish -- never
    // runs inline, even on the logic thread, so it is safe to call from inside a drain.
    // Goes through the wake gate, so once close_waker() ran it is a no-op -- no enqueue can
    // reach a finished run_loop, whichever thread calls it.
    void post_deferred(std::function<void()> fn) { wake_gate_->post(std::move(fn)); }

    // A callable any thread may invoke to wake the logic thread for a drain + publish; hand it
    // to Shared<T>/Channel<T>::set_waker(). Becomes a no-op once close_waker() ran, so it may
    // safely outlive the app (worker threads often do).
    std::function<void()> waker() const {
        return [gate = wake_gate_] { gate->wake(); };
    }

    // What another thread sets or sends on `source` (a Shared<T> or a Channel<T>) reaches
    // `on_value` on the logic thread: its waker posts a drain of `source`, so it needs neither a
    // set_waker() nor a drain() listing it. One post per wake, so a Shared still coalesces.
    template <typename Source, typename F>
        requires requires(Source& s) { s.drain_notifications(); }
    void receive(Source& source, F&& on_value) {
        source.observe(std::forward<F>(on_value));
        auto drain = [&source] { source.drain_notifications(); };
        source.set_waker([gate = wake_gate_, drain] { gate->post(drain); });
        post_deferred(drain);   // a value set before this call was pending with no one to wake
    }

    // After this returns no wake or cross-thread post() is in flight and none will start:
    // the run_loop may finish.
    void close_waker() { wake_gate_->close(); }

private:
    // Every cross-thread enqueue goes through here. The mutex makes close() wait for an
    // in-flight post(), so nothing ever reaches a finished or destroyed run_loop.
    // Holding the mutex across enqueue() cannot self-deadlock: run_loop never runs the
    // scheduled drain inline, so a set() inside that drain re-enters post() unlocked.
    class WakeGate {
        std::mutex mutex_;
        AppContext* ctx_;
    public:
        explicit WakeGate(AppContext& ctx) : ctx_(&ctx) {}
        void post(std::function<void()> fn) {
            std::lock_guard lock{mutex_};
            if (ctx_) ctx_->enqueue(std::move(fn));
        }
        void wake() { post([] {}); }
        void close() {
            std::lock_guard lock{mutex_};
            ctx_ = nullptr;
        }
    };

    void enqueue(std::function<void()> fn) {
        if (closed_ && closed_->load(std::memory_order_acquire)) return;
        queue_->push(std::move(fn));
        bool expected = false;
        if (scheduled_->compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            exec::start_detached(stdexec::schedule(sched_)
                                 | stdexec::then([this] { drain_queue(); }));
        }
    }

    // CAS-reschedule drain: pop every queued mutation, run the publish tail,
    // then either exit or re-arm if a producer pushed between the last pop
    // and the flag reset.
    void drain_queue() {
        auto do_drain = [&]{
            do {
                detail_in_mutation_batch = true;
                while (auto f = queue_->pop()) {
                    try {
                        (*f)();
                    } catch (...) {
                        report_unhandled_error(std::current_exception());
                    }
                }
                detail_in_mutation_batch = false;
                if (drain_publish_ && *drain_publish_) {
                    try {
                        (*drain_publish_)();
                    } catch (...) {
                        report_unhandled_error(std::current_exception());
                    }
                }
                scheduled_->store(false, std::memory_order_release);
                if (queue_->empty()) break;
                bool expected = false;
                if (!scheduled_->compare_exchange_strong(expected, true, std::memory_order_acq_rel)) break;
            } while (true);
        };
        apply_wrapper(logic_wrapper_, do_drain);
    }
    scheduler_type sched_;
    std::function<void(const FileDrop&)> file_drop_;
    bool follow_system_theme_ = false;
    // What a widget asked a window to show (WidgetTree::request_window), as that window's model.
    struct RequestedView {
        std::function<void(WidgetTree::ViewBuilder&)> build;
        void view(WidgetTree::ViewBuilder& vb) { vb.vstack([&] { build(vb); }); }
    };
    std::map<WindowId, std::unique_ptr<RequestedView>> requested_;
    std::map<WindowId, std::function<void()>> on_window_closed_;
    void open_requested(const WidgetTree::WindowRequest& r) {
        auto model = std::make_unique<RequestedView>(RequestedView{r.build});
        WindowId id = open_window(*model, {.title = r.title.c_str(), .width = 480, .height = 360});
        if (id != 0) {
            if (auto* e = registry_->find(id)) serve_window_requests(*e->tree);
            requested_[id] = std::move(model);
            on_window_closed_[id] = r.on_closed;
        }
        if (r.on_opened) r.on_opened(id);
    }
    WindowGeometry* geometry_ = nullptr;
    bool system_dark_ = true;
    Theme theme_ = dark_theme();   // the windows' current theme, for windows opened later
    void apply_theme(const Theme& t) {
        theme_ = t;
        registry_->for_each([&](WindowId, WindowRegistry::Entry& e) { e.tree->set_theme(t); });
    }
    AnimationClock* clock_;
    Window* window_;
    Backend* backend_;
    WindowRegistry* registry_;
    std::function<void(const KeyPress&)>* key_handler_;
    std::function<void()>* post_dispatch_hook_;
    std::shared_ptr<mpsc_queue<std::function<void()>>> queue_;
    std::shared_ptr<std::atomic<bool>> scheduled_;
    std::shared_ptr<std::atomic<bool>> closed_;
    std::shared_ptr<std::function<void()>> drain_publish_;
    std::shared_ptr<std::function<void(std::function<void()>)>> logic_wrapper_;
    std::shared_ptr<WakeGate> wake_gate_ = std::make_shared<WakeGate>(*this);
};

namespace app_run_detail {

// One run of model_app(): its locals as members, the lambdas they captured as member functions.
// The member ORDER below is the locals' order -- C++ destroys members in reverse declaration
// order, exactly as it did the locals -- so keep it when adding one.
class AppRun {
public:
    template <typename Model>
    explicit AppRun(Backend& backend, Window& window, Model& model)
        : loop_(), sched_(loop_.get_scheduler()), primary_id_(registry_.add(window, model)),
          window_(window), backend_(backend) {
        // Shared tail for §2 mutation queue: drain all Shared/Channel, publish dirty, tick.
        // Factored from tick path's for_each(drain_shared) + publish_dirty (not the
        // single-window drain_shared in the input path — secondary windows would starve).
        *mutation_drain_publish_ = [this] { drain_publish(); };

        // Heap-allocate AppContext so raw AppContext* cached by callers (tests,
        // and future Python bindings holding weak_ptr to queue) never dangles
        // when the logic thread exits and would otherwise destroy a stack `ctx`.
        // The shared_ptr is held both here and by the logic thread; raw
        // pointers observed in setup()/Backend::run remain valid until join.
        ctx_holder_ = std::make_shared<AppContext>(sched_, anim_clock_, window_, backend_, registry_,
                                                   global_key_handler_, post_dispatch_hook_,
                                                   mutation_queue_, mutation_scheduled_,
                                                   mutation_closed_, mutation_drain_publish_,
                                                   logic_wrapper_holder_);
        registry_.set_waker(ctx_holder_->waker());
        ctx_holder_->serve_window_requests(*registry_.find(primary_id_)->tree);
        registry_.set_text_measurer(backend_.text_measurer());
        registry_.set_clipboard(backend_.clipboard());
        registry_.set_url_opener(backend_.url_opener());

        // Timed wakes of the logic thread (a tooltip's delay). The timer thread only posts; the
        // ticks run on the logic thread, which then publishes and arms the next deadline.
        deadline_timer_.emplace([this] { on_deadline(); });
    }

    void run(std::function<void(AppContext&)> setup) {
        logic_thread_ = std::thread([this, setup = std::move(setup)] { logic_main(std::move(setup)); });

        backend_.run([this](const WindowEvent& we) { pump_event(we); });

        // Finish only after the pump returned: it keeps dispatching already-queued OS events
        // after quit(), and each one schedules onto this loop (see app.hpp). Close the gate
        // first: after this, no cross-thread post() or wake can reach the finishing run_loop.
        // (Python's PostHandle path does not go through the gate -- see prism_ext.cpp.)
        ctx_holder_->close_waker();   // a deadline firing from here on posts nothing
        loop_.finish();
        logic_thread_.join();
        deadline_timer_.reset();   // after the join: the logic thread arms it until then
    }

private:
    // The deadline timer's callback, on the timer's own thread: hand the tick to the logic thread.
    void on_deadline() { ctx_holder_->post_deferred([this] { run_due_wakes(); }); }

    // On the logic thread: run every tree's due deadlines, then arm the next one.
    void run_due_wakes() {
        auto now = DeadlineTimer::clock::now();
        registry_.for_each([now](WindowId, WindowRegistry::Entry& e) { e.tree->tick(now); });
        arm_deadlines();
    }

#ifdef PRISM_DEBUG_TOOLS_ENABLED
    // Shared by the hotkey's own detach branch and the generic secondary-WindowClose
    // path below (debug window closed via its own chrome) — both must leave the
    // inspector fully dormant so the next Ctrl+Shift+I press reopens it.
    void reset_debug_inspector() {
        debug_window_id_.reset();
        debug_controller_.reset();
        post_dispatch_hook_ = nullptr;
        // Closing the inspector (either path) must not leave a stale highlight rect on the
        // main window with no inspector left to explain it.
        if (auto* primary_entry = registry_.find(primary_id_))
            primary_entry->tree->set_debug_highlight(std::nullopt);
    }

    // Ctrl+Shift+I: opens the live tree inspector, or closes it again.
    void on_inspector_hotkey(const KeyPress& kp) {
        if (kp.key != keys::i || !(kp.mods & mods::ctrl) || !(kp.mods & mods::shift))
            return;
        if (!debug_window_id_) {
            auto* win = backend_.request_window(WindowConfig{.title = "PRISM Tree Inspector",
                                                              .decoration = DecorationMode::Custom});
            if (!win) return; // request failed — stay dormant, try again on next hotkey press
            auto* primary_entry = registry_.find(primary_id_);
            if (!primary_entry) return;
            debug_window_id_ = registry_.add(*win, debug_model_);
            auto* debug_entry = registry_.find(*debug_window_id_);
            if (!debug_entry) return;
            debug_controller_.emplace(*primary_entry->tree, *debug_entry->tree, debug_model_);
            ctx_holder_->set_post_dispatch_hook([this] {
                if (debug_controller_) debug_controller_->refresh();
            });
        } else {
            registry_.remove(*debug_window_id_);
            backend_.close_window(*debug_window_id_);
            reset_debug_inspector();
        }
    }
#endif

    void publish_entry(WindowId id, WindowRegistry::Entry& entry) {
        entry.current_snap = std::shared_ptr<const SceneSnapshot>(
            entry.tree->build_snapshot(static_cast<float>(entry.width),
                                        static_cast<float>(entry.height),
                                        ++entry.version));
        backend_.submit(id, entry.current_snap);
        backend_.wake();
        entry.tree->clear_dirty();
#ifdef PRISM_DEBUG_TOOLS_ENABLED
        if (id == primary_id_ && debug_controller_)
            debug_controller_->update_stats(*entry.current_snap);
#endif
    }

    void publish_dirty() {
        registry_.for_each_dirty([this](WindowId id, WindowRegistry::Entry& entry) {
            publish_entry(id, entry);
        });
    }

    void schedule_tick() {
        if (!anim_clock_.active() || tick_scheduled_) return;
        tick_scheduled_ = true;
        exec::start_detached(
            stdexec::schedule(sched_)
            | stdexec::then([this, logic_wrapper_holder = logic_wrapper_holder_] {
                auto do_tick = [this] {
                    tick_scheduled_ = false;
                    anim_clock_.tick(AnimationClock::clock::now());
                    // schedule_tick() inside the shared tail no-ops when the
                    // clock went inactive, matching the old guarded reschedule.
                    (*mutation_drain_publish_)();
                };
                AppContext::apply_wrapper(logic_wrapper_holder, do_tick);
            })
        );
    }

    // After input: one publish, queued behind the events already waiting, so a burst of
    // them (key repeat, a fast wheel, clicks queued while the logic thread was busy) is drawn
    // once with all of them applied, not once per event. A lone event still publishes at
    // once: its publish is next in the queue. Resized windows publish even if nothing is dirty.
    void publish_after_input(std::optional<WindowId> resized_window) {
        if (resized_window && !std::ranges::contains(resized_, *resized_window)) resized_.push_back(*resized_window);
        if (std::exchange(publish_queued_, true)) return;
        exec::start_detached(
            stdexec::schedule(sched_)
            | stdexec::then([this, logic_wrapper_holder = logic_wrapper_holder_, closed = mutation_closed_] {
                AppContext::apply_wrapper(logic_wrapper_holder, [this, closed] {
                    publish_queued_ = false;
                    if (closed->load(std::memory_order_acquire)) return;
                    for (auto id : std::exchange(resized_, {}))
                        if (auto* e = registry_.find(id)) publish_entry(id, *e);
                    publish_dirty();
                });
            })
        );
    }

    // The mutation queue's shared publish tail (see the assignment in the constructor).
    void attach_blocks() {
        registry_.for_each([this](WindowId, WindowRegistry::Entry& entry) {
            entry.tree->attach_blocks(*ctx_holder_);
        });
    }

    void drain_publish() {
        attach_blocks();   // blocks built since the last turn (a window opened, a page shown)
        registry_.for_each([](WindowId, WindowRegistry::Entry& entry) {
            entry.tree->drain_shared();
        });
        publish_dirty();
        schedule_tick();
    }

    void arm_deadlines() {
        registry_.for_each([this](WindowId, WindowRegistry::Entry& e) {
            if (auto due = e.tree->next_wake()) deadline_timer_->arm(*due);
        });
    }

    void logic_main(std::function<void(AppContext&)> setup) {
        detail_is_logic_thread = true;
        core::logic_thread.store(std::this_thread::get_id());
        backend_.wait_ready();
        registry_.for_each([this](WindowId id, WindowRegistry::Entry& entry) {
            publish_entry(id, entry);
        });

        auto& ctx = *ctx_holder_;

#ifdef PRISM_DEBUG_TOOLS_ENABLED
        // Live tree inspector, toggled by Ctrl+Shift+I. This installs the sole
        // global-key-handler/post-dispatch-hook slots AppContext exposes — an
        // app's own setup() calling set_global_key_handler/set_post_dispatch_hook
        // below would silently override this wiring. Known limitation, not solved
        // here (see commit message).
        ctx.set_global_key_handler([this](const KeyPress& kp) { on_inspector_hotkey(kp); });
#endif

        attach_blocks();
        if (setup) {
            setup(ctx);
            publish_dirty();   // what setup changed or opened (a theme, a second window)
            schedule_tick();
        }

        loop_.run();
        core::logic_thread.store(std::thread::id{});   // the model is the caller's again
    }

    // One backend event: schedule it onto the logic thread, wrapped like the rest of the work there.
    void pump_event(const WindowEvent& we) {
        const auto& ev = we.event;
        WindowId wid = we.window;
        auto wrapper_for_dispatch = logic_wrapper_holder_;
        exec::start_detached(
            stdexec::schedule(sched_)
            | stdexec::then([this, ev, wid, shown = we.shown, wrapper_for_dispatch] {
                AppContext::apply_wrapper(wrapper_for_dispatch, [this, ev, wid, shown] {
                    dispatch(ev, wid, shown);
                });
            })
        );
    }

    void dispatch(const InputEvent& ev, WindowId wid, const std::shared_ptr<const SceneSnapshot>& shown) {
        if (mutation_closed_->load(std::memory_order_acquire)) return;
        if (std::holds_alternative<WindowClose>(ev)) {
            handle_close(wid);
            return;
        }
        if (auto* st = std::get_if<SystemTheme>(&ev)) {   // every window's, not one's
            ctx_holder_->system_theme_changed(st->dark);
            publish_dirty();
            return;
        }

        auto* entry = registry_.find(wid);
        if (!entry) return;

        bool needs_publish = update_window(ev, wid, *entry);
        if (auto* kp = std::get_if<KeyPress>(&ev)) {
            entry = handle_key(*kp, wid);
            if (!entry) return;
        }
        deliver(*entry, ev, wid, shown, needs_publish);
    }

    void handle_close(WindowId wid) {
        if (wid == primary_id_) {
            if (publish_queued_) publish_dirty();   // the frame input still owes
            anim_clock_.clear();
            mutation_closed_->store(true, std::memory_order_release);
            backend_.quit();
        } else {
            registry_.remove(wid);
            backend_.close_window(wid);
            ctx_holder_->window_closed(wid);   // a widget's window: tell the widget
            publish_dirty();                  // what that changed (a panel docked again)
#ifdef PRISM_DEBUG_TOOLS_ENABLED
            if (debug_window_id_ && wid == *debug_window_id_)
                reset_debug_inspector();
#endif
        }
    }

    // Resize and move before the event reaches the widgets: they set the tree's size and the
    // size the app remembers. True when the frame must be published even if nothing is dirty.
    bool update_window(const InputEvent& ev, WindowId wid, WindowRegistry::Entry& entry) {
        bool needs_publish = false;
        if (auto* resize = std::get_if<WindowResize>(&ev)) {
            entry.width = resize->width;
            entry.height = resize->height;
            needs_publish = true;
            if (wid == primary_id_) ctx_holder_->main_window_resized(*resize);
        }
        if (auto* move = std::get_if<WindowMove>(&ev); move && wid == primary_id_)
            ctx_holder_->main_window_moved(*move);
        return needs_publish;
    }

    // The sole global key handler. Returns the window's entry again: global_key_handler may have
    // destroyed entry (e.g. debug inspector hotkey toggles window).
    WindowRegistry::Entry* handle_key(const KeyPress& kp, WindowId wid) {
        // Sees every key, shortcuts included (e.g. Ctrl+V, which the focused text widget also
        // gets as a Paste event) -- not a consuming handler.
        if (global_key_handler_) global_key_handler_(kp);
        return registry_.find(wid);
    }

    void deliver(WindowRegistry::Entry& entry, const InputEvent& ev, WindowId wid,
                 const std::shared_ptr<const SceneSnapshot>& shown, bool needs_publish) {
        // Against the frame the user saw, if the backend says which (see WindowEvent).
        const SceneSnapshot* seen = shown ? shown.get() : entry.current_snap.get();
        widget_detail::route_event(*entry.tree, seen, ev);
        if (auto* drop = std::get_if<FileDrop>(&ev); drop && ctx_holder_->file_drop_handler())
            ctx_holder_->file_drop_handler()(*drop);
        if (entry.current_snap)
            entry.window->set_cursor(entry.tree->desired_cursor());

        entry.tree->drain_shared();
        if (post_dispatch_hook_) post_dispatch_hook_();
        publish_after_input(needs_publish ? std::optional{wid} : std::nullopt);
        schedule_tick();
        arm_deadlines();
    }

    stdexec::run_loop loop_;
    AppContext::scheduler_type sched_;
#ifdef PRISM_DEBUG_TOOLS_ENABLED
    // Declared before `registry_` so it outlives the registry-owned debug WidgetTree:
    // that tree holds Connections into debug_model_'s SenderHubs, and members destruct in
    // reverse declaration order. If debug_model_ destructed before registry_ (i.e. were
    // declared after it), quitting while the inspector is still attached would leave
    // registry_'s teardown disconnecting Connections into an already-freed debug_model_ —
    // a use-after-free. See the "quit while still attached" regression test below.
    debug::TreeInspectorModel debug_model_;
    std::optional<WindowId> debug_window_id_;
    std::optional<debug::TreeInspectorController> debug_controller_;
#endif
    WindowRegistry registry_;
    WindowId primary_id_;
    AnimationClock anim_clock_;
    std::function<void(const KeyPress&)> global_key_handler_;
    std::function<void()> post_dispatch_hook_;
    bool tick_scheduled_ = false;

    // Destroyed by run() with the GIL released — queued closures must never capture nb::object.
    std::shared_ptr<mpsc_queue<std::function<void()>>> mutation_queue_ =
        std::make_shared<mpsc_queue<std::function<void()>>>();
    std::shared_ptr<std::atomic<bool>> mutation_scheduled_ = std::make_shared<std::atomic<bool>>(false);
    std::shared_ptr<std::atomic<bool>> mutation_closed_ = std::make_shared<std::atomic<bool>>(false);
    std::shared_ptr<std::function<void()>> mutation_drain_publish_ = std::make_shared<std::function<void()>>();

    // Shared wrapper holder for logic-thread work (GIL acquire for Python). SDL-free.
    std::shared_ptr<std::function<void(std::function<void()>)>> logic_wrapper_holder_ =
        std::make_shared<std::function<void(std::function<void()>)>>();

    bool publish_queued_ = false;
    std::vector<WindowId> resized_;

    // SDL's window creation and event pump must run on the process's real main
    // thread on macOS (AppKit requirement -- Cocoa_CreateDevice() silently fails
    // off-main-thread), so Backend::run() stays on whichever thread calls model_app(),
    // and the stdexec run_loop that drives view rebuilding moves to logic_thread_.
    // This struct lives on model_app()'s stack until after logic_thread_.join(), so the
    // raw AppContext* cached by callers (tests, Python bindings) stays valid until then.
    std::shared_ptr<AppContext> ctx_holder_;

    std::optional<DeadlineTimer> deadline_timer_;
    std::thread logic_thread_;

    Window& window_;
    Backend& backend_;
};

} // namespace app_run_detail

template <typename Model>
void model_app(Backend& backend, Window& window, Model& model,
               std::function<void(AppContext&)> setup = nullptr) {
    app_run_detail::AppRun run{backend, window, model};
    run.run(std::move(setup));
}

template <typename Model>
void model_app(WindowConfig cfg, Model& model,
               std::function<void(AppContext&)> setup = nullptr) {
    auto backend = Backend::software(RenderConfig{});
    auto& window = backend.create_window(cfg);
    model_app(backend, window, model, std::move(setup));
}

template <typename Model>
void model_app(std::string_view title, Model& model,
               std::function<void(AppContext&)> setup = nullptr) {
    model_app(WindowConfig{.title = title.data()}, model, std::move(setup));
}

} // namespace prism::app
