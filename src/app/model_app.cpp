#include <prism/app/model_app.hpp>

// The bodies live here so that every model_app() caller stops compiling the publish pipeline:
// event routing, the error hub and the sender-graph schedule/then instantiations are only
// needed where the logic thread actually runs.
#include <prism/app/event_routing.hpp>
#include <prism/core/error_hub.hpp>
#include <prism/core/logic_thread.hpp>
#include <prism/ui/context.hpp>

#include <algorithm>
#include <exception>
#include <utility>
#include <variant>

namespace prism::app {

void AppContext::choose_files(FileDialog dialog, std::function<void(FileDialogResult)> done) {
    auto on_logic_thread = [gate = wake_gate_, done = std::move(done)](FileDialogResult r) mutable {
        gate->post([done = std::move(done), r = std::move(r)]() mutable { done(std::move(r)); });
    };
    auto* services = backend_->platform_services();
    if (!services) return on_logic_thread({{}, "this backend has no file dialogs"});
    services->show_file_dialog(window_->id(), std::move(dialog), std::move(on_logic_thread));
}

void AppContext::close_window(WindowId id) {
    if (id == window_->id() || !window_open(id)) return;
    registry_->remove(id);
    backend_->close_window(id);
    requested_.erase(id);   // a widget's window (below): its widget asked for this, no callback
    on_window_closed_.erase(id);
}

void AppContext::serve_window_requests(WidgetTree& tree) {
    tree.set_window_opener(
        [this](WidgetTree::WindowRequest r) { post([this, r = std::move(r)] { open_requested(r); }); },
        [this](WindowId id) { post([this, id] { close_window(id); }); });
}

void AppContext::window_closed(WindowId id) {
    auto it = on_window_closed_.find(id);
    if (it == on_window_closed_.end()) return;
    auto done = std::move(it->second);
    on_window_closed_.erase(it);
    requested_.erase(id);
    if (done) done();
}

void AppContext::set_theme(const Theme& t) {
    follow_system_theme_ = false;
    apply_theme(t);
}

void AppContext::follow_system_theme() {
    follow_system_theme_ = true;
    apply_theme(system_dark_ ? dark_theme() : light_theme());
}

void AppContext::main_window_resized(const WindowResize& r) {
    if (!geometry_) return;
    geometry_->maximized.set(r.maximized);
    if (r.maximized) return;   // the size to come back to stays
    int w = r.width, content_h = r.height;
    bool custom = window_->decoration_mode() == DecorationMode::Custom;
    geometry_->width.set(w);
    geometry_->height.set(content_h + (custom ? static_cast<int>(WindowChrome::title_bar_h.raw()) : 0));
}

void AppContext::main_window_moved(const WindowMove& m) {
    if (!geometry_ || m.maximized) return;
    geometry_->x.set(m.x);
    geometry_->y.set(m.y);
    geometry_->positioned.set(true);
}

void AppContext::system_theme_changed(bool dark) {
    system_dark_ = dark;
    if (follow_system_theme_) apply_theme(dark ? dark_theme() : light_theme());
}

void AppContext::show_message(MessageKind kind, std::string title, std::string text) {
    if (auto* services = backend_->platform_services())
        services->show_message(window_->id(), kind, std::move(title), std::move(text));
}

void AppContext::set_logic_wrapper(std::function<void(std::function<void()>)> w) {
    if (logic_wrapper_) *logic_wrapper_ = std::move(w);
    else logic_wrapper_ = std::make_shared<std::function<void(std::function<void()>)>>(std::move(w));
}

void AppContext::post(std::function<void()> fn) {
    if (!fn) return;
    if (closed_ && closed_->load(std::memory_order_acquire)) return;
    if (detail_is_logic_thread()) {
        if (detail_in_mutation_batch()) {
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

void AppContext::enqueue(std::function<void()> fn) {
    if (closed_ && closed_->load(std::memory_order_acquire)) return;
    queue_->push(std::move(fn));
    schedule_drain();
}

void AppContext::schedule_drain() {
    bool expected = false;
    if (scheduled_->compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        exec::start_detached(stdexec::schedule(sched_)
                             | stdexec::then([this] { drain_queue(); }));
    }
}

void AppContext::drain_queue() {
    apply_wrapper(logic_wrapper_, [&] {
        run_mutations(take_queued());
        run_publish_tail();
        scheduled_->store(false, std::memory_order_release);
        if (!queue_->empty()) schedule_drain();
    });
}

std::vector<std::function<void()>> AppContext::take_queued() {
    std::vector<std::function<void()>> batch;
    while (auto f = queue_->pop()) batch.push_back(std::move(*f));
    return batch;
}

void AppContext::run_mutations(std::vector<std::function<void()>> batch) {
    detail_in_mutation_batch() = true;
    for (auto& f : batch) {
        try {
            f();
        } catch (...) {
            report_unhandled_error(std::current_exception());
        }
    }
    detail_in_mutation_batch() = false;
}

void AppContext::run_publish_tail() {
    if (!drain_publish_ || !*drain_publish_) return;
    try {
        (*drain_publish_)();
    } catch (...) {
        report_unhandled_error(std::current_exception());
    }
}

void AppContext::open_requested(const WidgetTree::WindowRequest& r) {
    auto model = std::make_unique<RequestedView>(RequestedView{r.build});
    WindowId id = open_window(*model, {.title = r.title.c_str(), .width = 480, .height = 360});
    if (id != 0) {
        if (auto* e = registry_->find(id)) serve_window_requests(*e->tree);
        requested_[id] = std::move(model);
        on_window_closed_[id] = r.on_closed;
    }
    if (r.on_opened) r.on_opened(id);
}

void AppContext::apply_theme(const Theme& t) {
    theme_ = t;
    registry_->for_each([&](WindowId, WindowRegistry::Entry& e) { e.tree->set_theme(t); });
}

namespace app_run_detail {

void AppRun::run(std::function<void(AppContext&)> setup) {
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

void AppRun::run_due_wakes() {
    auto now = DeadlineTimer::clock::now();
    registry_.for_each([now](WindowId, WindowRegistry::Entry& e) { e.tree->tick(now); });
    arm_deadlines();
}

#ifdef PRISM_DEBUG_TOOLS_ENABLED
void AppRun::reset_debug_inspector() {
    debug_window_id_.reset();
    debug_controller_.reset();
    post_dispatch_hook_ = nullptr;
    // Closing the inspector (either path) must not leave a stale highlight rect on the
    // main window with no inspector left to explain it.
    if (auto* primary_entry = registry_.find(primary_id_))
        primary_entry->tree->set_debug_highlight(std::nullopt);
}

void AppRun::on_inspector_hotkey(const KeyPress& kp) {
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

void AppRun::publish_entry(WindowId id, WindowRegistry::Entry& entry) {
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

void AppRun::publish_dirty() {
    registry_.for_each_dirty([this](WindowId id, WindowRegistry::Entry& entry) {
        publish_entry(id, entry);
    });
}

void AppRun::schedule_tick() {
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

void AppRun::publish_after_input(std::optional<WindowId> resized_window) {
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

void AppRun::attach_blocks() {
    registry_.for_each([this](WindowId, WindowRegistry::Entry& entry) {
        entry.tree->attach_blocks(*ctx_holder_);
    });
}

void AppRun::drain_publish() {
    attach_blocks();   // blocks built since the last turn (a window opened, a page shown)
    registry_.for_each([](WindowId, WindowRegistry::Entry& entry) {
        entry.tree->drain_shared();
    });
    publish_dirty();
    schedule_tick();
}

void AppRun::arm_deadlines() {
    registry_.for_each([this](WindowId, WindowRegistry::Entry& e) {
        if (auto due = e.tree->next_wake()) deadline_timer_->arm(*due);
    });
}

void AppRun::logic_main(std::function<void(AppContext&)> setup) {
    detail_is_logic_thread() = true;
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

void AppRun::pump_event(const WindowEvent& we) {
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

void AppRun::dispatch(const InputEvent& ev, WindowId wid, const std::shared_ptr<const SceneSnapshot>& shown) {
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

void AppRun::handle_close(WindowId wid) {
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

bool AppRun::update_window(const InputEvent& ev, WindowId wid, WindowRegistry::Entry& entry) {
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

WindowRegistry::Entry* AppRun::handle_key(const KeyPress& kp, WindowId wid) {
    // Sees every key, shortcuts included (e.g. Ctrl+V, which the focused text widget also
    // gets as a Paste event) -- not a consuming handler.
    if (global_key_handler_) global_key_handler_(kp);
    return registry_.find(wid);
}

void AppRun::deliver(WindowRegistry::Entry& entry, const InputEvent& ev, WindowId wid,
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

} // namespace app_run_detail

} // namespace prism::app
