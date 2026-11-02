#include <prism/backends/software_backend.hpp>
#include "sdl_webgpu.hpp"
#include <prism/backends/font_lookup.hpp>
#include <prism/core/utf8.hpp>
#include <prism/ui/window_chrome.hpp>

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <optional>

// Set by src/meson.build; defaulted so this file still builds elsewhere (then only
// RenderConfig paths are used).
#ifndef PRISM_UI_FONT_PATH
#define PRISM_UI_FONT_PATH nullptr
#define PRISM_UI_FONT_INSTALL_PATH ""
#endif
#ifndef PRISM_MONO_FONT_PATH
#define PRISM_MONO_FONT_PATH nullptr
#define PRISM_MONO_FONT_INSTALL_PATH ""
#endif

namespace prism::backends {
using namespace prism::core;
using namespace prism::render;
using namespace prism::ui;
using namespace prism::app;

std::string SoftwareBackend::ui_font_path() const {
    return find_font(render_config_.font_path, PRISM_UI_FONT_PATH, backend_library_dir(),
                     PRISM_UI_FONT_INSTALL_PATH);
}

std::string SoftwareBackend::mono_font_path() const {
    return find_font(render_config_.mono_font_path, PRISM_MONO_FONT_PATH, backend_library_dir(),
                     PRISM_MONO_FONT_INSTALL_PATH);
}

SoftwareBackend::SoftwareBackend(RenderConfig cfg)
    : render_config_(cfg)
{
    auto ui = ui_font_path(), mono = mono_font_path();
    if (ui.empty() && mono.empty())   // otherwise every label silently disappears
        std::fprintf(stderr, "[prism] warning: no font found, text will not be drawn "
                             "(set RenderConfig::font_path / mono_font_path)\n");
    measurer_ = std::make_unique<SdlTextMeasurer>(std::move(ui), std::move(mono));
}

SoftwareBackend::~SoftwareBackend() {
    measurer_.reset();   // closes its fonts: must run before TTF_Quit()
    windows_.clear();    // before the fonts their WebGPU text caches were drawn with, and the context
    destroy_webgpu_context(wgpu_);
    fonts_.close();
    TTF_Quit();
    SDL_Quit();
}

// RenderConfig::renderer, unless PRISM_RENDERER=webgpu|sdl says otherwise.
bool SoftwareBackend::wants_webgpu() const {
    if (const char* env = std::getenv("PRISM_RENDERER")) {
        if (std::strcmp(env, "webgpu") == 0) return true;
        if (std::strcmp(env, "sdl") == 0) return false;
    }
    return render_config_.renderer == Renderer::WebGpu;
}

void SoftwareBackend::prepare_window(SdlWindow& win) {
    if (wgpu_) win.use_webgpu(wgpu_);
}

Window& SoftwareBackend::create_window(WindowConfig cfg) {
    // Pre-run only; after run() use request_window() (queued, thread-safe).
    assert(!ready_.load(std::memory_order_acquire) && "create_window after run(): use request_window()");
    std::lock_guard<std::mutex> lk(windows_mutex_);
    auto id = ++next_id_;
    auto window = std::make_unique<SdlWindow>(id, cfg);
    auto& ref = *window;
    windows_.emplace(id, std::move(window));
    snapshots_[id]; // default-construct snapshot slot
    return ref;
}

Window* SoftwareBackend::request_window(WindowConfig cfg) {
    auto req = std::make_shared<PendingWindowRequest>();
    req->cfg = cfg;
    window_requests_.push(req);
    wake();

    std::unique_lock lock(req->m);
    req->cv.wait_for(lock, std::chrono::seconds(2), [&] { return req->done; });
    return req->done ? req->result : nullptr;
}

void SoftwareBackend::drain_window_requests() {
    while (auto req_opt = window_requests_.pop()) {
        auto req = *req_opt;
        Window* result = nullptr;
        if (running_.load(std::memory_order_relaxed)) {
            WindowId id;
            {
                std::lock_guard<std::mutex> lk(windows_mutex_);
                id = ++next_id_;
            }
            auto win = std::make_unique<SdlWindow>(id, req->cfg);
            prepare_window(*win);
            win->ensure_created();
            if (pacer_) win->enable_vsync();
            SDL_StartTextInput(win->sdl_window());
            {
                std::lock_guard<std::mutex> lk(windows_mutex_);
                auto [it, _] = windows_.emplace(id, std::move(win));
                snapshots_[id];
                result = it->second.get();
            }
        }
        {
            std::lock_guard lock(req->m);
            req->result = result;
            req->done = true;
        }
        req->cv.notify_one();
    }
}

void SoftwareBackend::close_window(WindowId id) {
    close_requests_.push(id);
    wake();
}

void SoftwareBackend::drain_close_requests() {
    while (auto id_opt = close_requests_.pop()) {
        std::lock_guard<std::mutex> lk(windows_mutex_);
        windows_.erase(*id_opt);
        snapshots_.erase(*id_opt);
        last_presented_.erase(*id_opt);
        force_redraw_.erase(*id_opt);
    }
}

WindowId SoftwareBackend::sdl_id_to_prism_id(uint32_t sdl_window_id) const {
    std::lock_guard<std::mutex> lk(windows_mutex_);
    for (auto& [id, win] : windows_) {
        if (SDL_GetWindowID(win->sdl_window()) == sdl_window_id)
            return id;
    }
    return 0;
}

namespace {

#ifndef __EMSCRIPTEN__
// One SDL file dialog in flight: owns the filter strings until SDL calls back (SDL requires
// them to outlive the call), and the app's callback.
struct DialogCall {
    std::vector<std::string> names, patterns;
    std::vector<SDL_DialogFileFilter> filters;
    std::function<void(app::FileDialogResult)> done;
};

// SDL calls this once, on any thread: a list of paths, an empty list (cancelled), or null
// (the dialog failed; SDL_GetError says why).
void SDLCALL dialog_done(void* userdata, const char* const* filelist, int) {
    std::unique_ptr<DialogCall> call{static_cast<DialogCall*>(userdata)};
    app::FileDialogResult result;
    if (!filelist) result.error = SDL_GetError();
    else
        for (auto p = filelist; *p; ++p) result.paths.emplace_back(*p);
    call->done(std::move(result));
}
#endif

void show_file_dialog(SDL_Window* parent, QueuedPlatformServices::FileRequest& req) {
#ifdef __EMSCRIPTEN__
    // Emscripten's SDL port has no file dialogs; a web build would use the browser's file
    // input instead (not done yet).
    (void)parent;
    req.done({{}, "no file dialogs in the web build yet"});
#else
    auto call = std::make_unique<DialogCall>();
    for (auto& f : req.dialog.filters) {
        call->names.push_back(f.name);
        call->patterns.push_back(f.patterns);
    }
    for (std::size_t i = 0; i < call->names.size(); ++i)   // the strings no longer move
        call->filters.push_back({call->names[i].c_str(), call->patterns[i].c_str()});
    call->done = std::move(req.done);
    const char* start = req.dialog.start.empty() ? nullptr : req.dialog.start.c_str();
    const auto* filters = call->filters.empty() ? nullptr : call->filters.data();
    int n = static_cast<int>(call->filters.size());
    auto* raw = call.release();   // dialog_done takes it back
    switch (req.dialog.kind) {
    case app::FileDialogKind::Open:     SDL_ShowOpenFileDialog(dialog_done, raw, parent, filters, n, start, false); break;
    case app::FileDialogKind::OpenMany: SDL_ShowOpenFileDialog(dialog_done, raw, parent, filters, n, start, true); break;
    case app::FileDialogKind::Save:     SDL_ShowSaveFileDialog(dialog_done, raw, parent, filters, n, start); break;
    case app::FileDialogKind::Folder:   SDL_ShowOpenFolderDialog(dialog_done, raw, parent, start, false); break;
    }
#endif
}

void show_message(SDL_Window* parent, const QueuedPlatformServices::MessageRequest& req) {
    SDL_MessageBoxFlags flags = req.kind == app::MessageKind::Error   ? SDL_MESSAGEBOX_ERROR
                              : req.kind == app::MessageKind::Warning ? SDL_MESSAGEBOX_WARNING
                                                                      : SDL_MESSAGEBOX_INFORMATION;
    SDL_ShowSimpleMessageBox(flags, req.title.c_str(), req.text.c_str(), parent);
}

} // namespace

// On the main thread. The parent is looked up under the lock, then the lock is released before
// showing: a native dialog may run an event loop of its own.
void SoftwareBackend::show_platform_requests() {
    auto parent_of = [this](WindowId id) -> SDL_Window* {
        std::lock_guard<std::mutex> lk(windows_mutex_);
        auto it = windows_.find(id);
        return it != windows_.end() ? it->second->sdl_window() : nullptr;
    };
    platform_.drain([&](QueuedPlatformServices::FileRequest& f) { show_file_dialog(parent_of(f.parent), f); },
                    [&](QueuedPlatformServices::MessageRequest& m) { show_message(parent_of(m.parent), m); });
}

void SoftwareBackend::run(std::function<void(const WindowEvent&)> deliver) {
    // Every event carries the frame on screen when it was read (WindowEvent::shown), so the
    // app hit-tests it against what the user saw. last_presented_ is only touched on this
    // thread (presents and drain_close_requests), so reading it here needs no lock.
    auto event_cb = [&](WindowEvent we) {
        if (!we.shown)
            if (auto it = last_presented_.find(we.window); it != last_presented_.end()) we.shown = it->second;
        deliver(we);
    };
    SDL_Init(SDL_INIT_VIDEO);

    TTF_Init();
    {
        auto ui = ui_font_path(), mono = mono_font_path();
        fonts_.open(ui.empty() ? nullptr : ui.c_str(), mono.empty() ? nullptr : mono.c_str(), 16.f);
    }

    if (wants_webgpu()) wgpu_ = create_webgpu_context();

    // Create SDL windows (deferred from create_window()) and start text input
    {
        std::lock_guard<std::mutex> lk(windows_mutex_);
        for (auto& [id, win] : windows_) {
            prepare_window(*win);
            win->ensure_created();
            SDL_StartTextInput(win->sdl_window());
        }
    }

    // Present pacing: resolve the frame interval once — RenderConfig::target_fps,
    // else the display's actual refresh rate (60 Hz fallback) — and ask SDL for
    // vsync as a best-effort tear guard. Input dispatch below stays per-event-batch;
    // only presents wait for the frame clock.
    if (render_config_.frame_pacing) {
        float display_hz = 0;
        {
            std::lock_guard<std::mutex> lk(windows_mutex_);
            if (!windows_.empty()) {
                if (auto display = SDL_GetDisplayForWindow(windows_.begin()->second->sdl_window()))
                    if (const auto* mode = SDL_GetCurrentDisplayMode(display))
                        display_hz = mode->refresh_rate;
            }
            for (auto& [id, win] : windows_) win->enable_vsync();
        }
        pacer_.emplace(frame_interval_from_hz(resolve_frame_hz(render_config_.target_fps, display_hz)));
    }

    ready_.store(true, std::memory_order_release);
    ready_.notify_one();

    // A fast mouse can queue many SDL_EVENT_MOUSE_MOTION events between two SDL_WaitEvent
    // wakes; each dispatched MouseMove ultimately triggers a full-tree relayout + rebuild
    // downstream (see model_app.hpp), so processing every raw motion sample individually
    // makes rendering fall further and further behind the actual mouse position under a
    // fast/continuous drag. Only the most recent queued position actually matters, so
    // consecutive motion events for the same window are buffered and only the last one is
    // ever dispatched -- flushed whenever a different kind of event (or a motion for a
    // different window) needs to be delivered first, preserving relative event order.
    std::optional<WindowEvent> pending_motion;
    // One drop arrives as begin, a file or text event per item, complete: delivered as one event.
    std::optional<WindowEvent> pending_drop;
    auto pointer_left = [&](WindowId wid) {
        if (pointer_window_ != wid) return;
        pointer_window_ = 0;
        event_cb(WindowEvent{wid, MouseLeave{}});
    };

    auto flush_pending_motion = [&] {
        if (pending_motion) {
            event_cb(*pending_motion);
            pending_motion.reset();
        }
    };

    // Caller must hold windows_mutex_.
    auto find_window = [&](WindowId id) -> SdlWindow* {
        auto it = windows_.find(id);
        return it != windows_.end() ? it->second.get() : nullptr;
    };
    // Window-space Y -> client-space Y (custom chrome draws its own title bar
    // above the client area). Caller must hold windows_mutex_.
    auto client_y = [&](WindowId id, float y) {
        if (auto* win = find_window(id);
            win && win->decoration_mode() == DecorationMode::Custom)
            y -= WindowChrome::title_bar_h.raw();
        return y;
    };
    auto drop_point = [&](WindowId wid, const SDL_DropEvent& d) {
        std::lock_guard<std::mutex> lk(windows_mutex_);
        return Point{X{d.x}, Y{client_y(wid, d.y)}};
    };

    // A window needs presenting when its snapshot slot differs from what was last
    // presented (or SDL asked for a redraw).
    auto any_dirty = [&] {
        std::lock_guard<std::mutex> lk(windows_mutex_);
        for (auto& [id, slot] : snapshots_)
            if (force_redraw_.contains(id) ||
                slot.snapshot.load(std::memory_order_acquire) != last_presented_[id])
                return true;
        return false;
    };

    while (running_.load(std::memory_order_relaxed)) {
        SDL_Event ev;
        // Paced + dirty: wait only until the next frame deadline (events still wake
        // us earlier). Otherwise block indefinitely — every submit() is paired with
        // wake(), and with nothing dirty there is no present to wait for.
        bool have_event;
        if (pacer_ && any_dirty()) {
            have_event = SDL_WaitEventTimeout(&ev, static_cast<int>(
                pacer_->ms_until_next_frame(FramePacer::clock::now()).count()));
        } else {
            have_event = SDL_WaitEvent(&ev);
            if (!have_event) continue;
        }

        if (have_event) {
            do {
                // Resolve which prism window this event belongs to
                WindowId wid = 0;
                if (ev.type >= SDL_EVENT_WINDOW_FIRST && ev.type <= SDL_EVENT_WINDOW_LAST) {
                    wid = sdl_id_to_prism_id(ev.window.windowID);
                } else if (ev.type == SDL_EVENT_MOUSE_MOTION) {
                    wid = sdl_id_to_prism_id(ev.motion.windowID);
                } else if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN || ev.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                    wid = sdl_id_to_prism_id(ev.button.windowID);
                } else if (ev.type == SDL_EVENT_MOUSE_WHEEL) {
                    wid = sdl_id_to_prism_id(ev.wheel.windowID);
                } else if (ev.type == SDL_EVENT_KEY_DOWN || ev.type == SDL_EVENT_KEY_UP) {
                    wid = sdl_id_to_prism_id(ev.key.windowID);
                } else if (ev.type == SDL_EVENT_TEXT_INPUT) {
                    wid = sdl_id_to_prism_id(ev.text.windowID);
                } else if (ev.type >= SDL_EVENT_DROP_FILE && ev.type <= SDL_EVENT_DROP_POSITION) {
                    wid = sdl_id_to_prism_id(ev.drop.windowID);
                }
                // During a press-drag, route events to the window that received the press
                if (wid == 0 && pressed_window_ != 0)
                    wid = pressed_window_;
                // For single-window case, fall back to first window
                {
                    std::lock_guard<std::mutex> lk(windows_mutex_);
                    if (wid == 0 && windows_.size() == 1)
                        wid = windows_.begin()->first;
                }

                if (ev.type != SDL_EVENT_MOUSE_MOTION || (pending_motion && pending_motion->window != wid))
                    flush_pending_motion();

                switch (ev.type) {
                case SDL_EVENT_QUIT:
                    event_cb(WindowEvent{wid, WindowClose{}});
                    running_.store(false, std::memory_order_relaxed);
                    break;
                case SDL_EVENT_DROP_BEGIN:
                    pending_drop.reset();
                    break;
                case SDL_EVENT_DROP_FILE:
                case SDL_EVENT_DROP_TEXT: {
                    std::string data = ev.drop.data ? ev.drop.data : "";   // SDL's copy is temporary
                    Point at = drop_point(wid, ev.drop);
                    if (ev.type == SDL_EVENT_DROP_FILE) {
                        if (!pending_drop || !std::holds_alternative<FileDrop>(pending_drop->event))
                            pending_drop = WindowEvent{wid, FileDrop{at, {}}};
                        std::get<FileDrop>(pending_drop->event).paths.push_back(std::move(data));
                    } else {
                        if (!pending_drop || !std::holds_alternative<TextDrop>(pending_drop->event))
                            pending_drop = WindowEvent{wid, TextDrop{at, {}}};
                        std::get<TextDrop>(pending_drop->event).text += data;
                    }
                    break;
                }
                case SDL_EVENT_DROP_COMPLETE:
                    if (pending_drop) event_cb(std::move(*pending_drop));
                    pending_drop.reset();
                    break;
                case SDL_EVENT_WINDOW_MOUSE_LEAVE:   // its own window only, not the drag fallback
                    pointer_left(sdl_id_to_prism_id(ev.window.windowID));
                    break;
                case SDL_EVENT_WINDOW_RESIZED: {
                    int rh = ev.window.data2;
                    {
                        std::lock_guard<std::mutex> lk(windows_mutex_);
                        if (auto* win = find_window(wid);
                            win && win->decoration_mode() == DecorationMode::Custom)
                            rh -= static_cast<int>(WindowChrome::title_bar_h.raw());
                    }
                    event_cb(WindowEvent{wid, WindowResize{ev.window.data1, rh}});
                    break;
                }
                case SDL_EVENT_MOUSE_MOTION: {
                    {
                        std::lock_guard<std::mutex> lk(windows_mutex_);
                        auto* win = find_window(wid);
                        if (win && win->in_resize()) {
                            win->update_resize(
                                static_cast<int>(ev.motion.x), static_cast<int>(ev.motion.y));
                            break;
                        }
                        if (win && win->decoration_mode() == DecorationMode::Custom) {
                            int ww, wh;
                            SDL_GetWindowSize(win->sdl_window(), &ww, &wh);
                            auto zone = WindowChrome::hit_test(
                                static_cast<int>(ev.motion.x), static_cast<int>(ev.motion.y), ww, wh);
                            if (zone != WindowChrome::HitZone::Client) {
                                win->set_cursor(WindowChrome::cursor_for(zone));
                                flush_pending_motion();   // the last content move, then the leave
                                pointer_left(wid);
                                break;
                            }
                        }
                        pointer_window_ = wid;
                        pending_motion = WindowEvent{wid, MouseMove{
                            Point{X{ev.motion.x}, Y{client_y(wid, ev.motion.y)}}, ev.motion.state}};
                    }
                    break;
                }
                case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                    SDL_CaptureMouse(true);
                    pressed_window_ = wid;
                    std::lock_guard<std::mutex> lk(windows_mutex_);
                    auto* win = find_window(wid);
                    if (win && win->decoration_mode() == DecorationMode::Custom) {
                        int ww, wh;
                        SDL_GetWindowSize(win->sdl_window(), &ww, &wh);
                        auto zone = WindowChrome::hit_test(
                            static_cast<int>(ev.button.x), static_cast<int>(ev.button.y), ww, wh);
                        if (zone == WindowChrome::HitZone::Close) {
                            event_cb(WindowEvent{wid, WindowClose{}});
                            break;
                        }
                        if (zone == WindowChrome::HitZone::Minimize) {
                            win->minimize();
                            break;
                        }
                        if (zone == WindowChrome::HitZone::Maximize) {
                            if (win->is_fullscreen())
                                win->restore();
                            else
                                win->maximize();
                            break;
                        }
                        if (zone != WindowChrome::HitZone::Client) {
                            win->begin_resize(
                                static_cast<int>(ev.button.x), static_cast<int>(ev.button.y));
                            break;
                        }
                    }
                    event_cb(WindowEvent{wid, MouseButton{
                        Point{X{ev.button.x}, Y{client_y(wid, ev.button.y)}},
                        ev.button.button, true,
                        static_cast<uint16_t>(SDL_GetModState()), ev.button.clicks}});
                    break;
                }
                case SDL_EVENT_MOUSE_BUTTON_UP: {
                    SDL_CaptureMouse(false);
                    pressed_window_ = 0;
                    std::lock_guard<std::mutex> lk(windows_mutex_);
                    auto* win = find_window(wid);
                    if (win && win->in_resize()) {
                        win->end_resize();
                        break;
                    }
                    event_cb(WindowEvent{wid, MouseButton{
                        Point{X{ev.button.x}, Y{client_y(wid, ev.button.y)}},
                        ev.button.button, false,
                        static_cast<uint16_t>(SDL_GetModState()), ev.button.clicks}});
                    break;
                }
                case SDL_EVENT_MOUSE_WHEEL: {
                    std::lock_guard<std::mutex> lk(windows_mutex_);
                    event_cb(WindowEvent{wid, MouseScroll{
                        Point{X{ev.wheel.mouse_x}, Y{client_y(wid, ev.wheel.mouse_y)}},
                        DX{ev.wheel.x}, DY{ev.wheel.y}}});
                    break;
                }
                case SDL_EVENT_KEY_DOWN:
                    event_cb(WindowEvent{wid, KeyPress{static_cast<int32_t>(ev.key.key), ev.key.mod}});
                    // The clipboard is read here, on the main thread, and delivered as an event:
                    // widgets on the logic thread never wait for it.
                    if (mods::shortcut(ev.key.mod) && static_cast<int32_t>(ev.key.key) == keys::v) {
                        if (char* text = SDL_GetClipboardText()) {
                            // Clipboard text comes from other apps: may hold NULs or invalid UTF-8.
                            if (auto clean = core::utf8::sanitize(text); !clean.empty())
                                event_cb(WindowEvent{wid, Paste{std::move(clean)}});
                            SDL_free(text);
                        }
                    }
                    break;
                case SDL_EVENT_KEY_UP:
                    event_cb(WindowEvent{wid, KeyRelease{static_cast<int32_t>(ev.key.key), ev.key.mod}});
                    break;
                case SDL_EVENT_TEXT_EDITING:   // input-method composition (preedit)
                    event_cb(WindowEvent{wid, Composition{ev.edit.text ? ev.edit.text : "",
                                                          ev.edit.start, ev.edit.length}});
                    break;
                case SDL_EVENT_TEXT_INPUT:
                    // X11/Wayland still emit text for Super+letter (SDL filters only Ctrl/Alt);
                    // with Super as a shortcut modifier that would copy *and* type.
                    if (SDL_GetModState() & SDL_KMOD_GUI) break;
                    event_cb(WindowEvent{wid, TextInput{ev.text.text}});
                    break;
                case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                    event_cb(WindowEvent{wid, WindowClose{}});
                    break;
                case SDL_EVENT_USER:
                    drain_window_requests();
                    drain_close_requests();
                    clipboard_.drain([](const std::string& text) { SDL_SetClipboardText(text.c_str()); });
                    show_platform_requests();
                    break;
                case SDL_EVENT_WINDOW_EXPOSED:
                    // De-occlusion/un-minimize: SDL needs a redraw even though the
                    // snapshot pointer is unchanged.
                    {
                        std::lock_guard<std::mutex> lk(windows_mutex_);
                        force_redraw_.insert(wid);
                    }
                    break;
                default:
                    break;
                }
            } while (SDL_PollEvent(&ev));

            flush_pending_motion(); // the queue's last event is very often a motion event itself
        }

        if (!running_.load(std::memory_order_relaxed)) break;

        // Present dirty snapshots — collect under lock, render outside. With pacing
        // on, only on frame boundaries; last_presented_ dedupes unchanged snapshots,
        // so a high-rate publish stream coalesces into one present per frame.
        const auto now = FramePacer::clock::now();
        if (!pacer_ || pacer_->frame_due(now)) {
            std::vector<std::pair<WindowId, std::shared_ptr<const SceneSnapshot>>> to_render;
            {
                std::lock_guard<std::mutex> lk(windows_mutex_);
                for (auto& [id, snap_slot] : snapshots_) {
                    auto snap = snap_slot.snapshot.load(std::memory_order_acquire);
                    if (!snap) continue;
                    if (!force_redraw_.contains(id) && snap == last_presented_[id]) continue;
                    to_render.emplace_back(id, snap);
                    last_presented_[id] = std::move(snap);
                }
                force_redraw_.clear();
            }
            bool presented_any = false;
            for (auto& [id, snap] : to_render) {
                SdlWindow* win = nullptr;
                {
                    std::lock_guard<std::mutex> lk(windows_mutex_);
                    if (auto it = windows_.find(id); it != windows_.end()) win = it->second.get();
                }
                if (win) {
                    const auto t0 = FramePacer::clock::now();
                    win->render_snapshot(*snap, fonts_);
                    const auto t1 = FramePacer::clock::now();
                    {
                        std::lock_guard<std::mutex> lk(windows_mutex_);
                        if (auto it = snapshots_.find(id); it != snapshots_.end()) {
                            it->second.present_count.fetch_add(1, std::memory_order_relaxed);
                            it->second.last_present_ns.store(
                                std::chrono::duration_cast<std::chrono::nanoseconds>(
                                    t1.time_since_epoch()).count(),
                                std::memory_order_relaxed);
                            it->second.last_present_ms.store(
                                std::chrono::duration<double, std::milli>(t1 - t0).count(),
                                std::memory_order_relaxed);
                        }
                    }
                    presented_any = true;
                }
            }
            if (presented_any && pacer_) pacer_->mark_presented(now);
        }
    }
    drain_window_requests();
    drain_close_requests();
}

void SoftwareBackend::submit(WindowId window, std::shared_ptr<const SceneSnapshot> snap) {
    std::lock_guard<std::mutex> lk(windows_mutex_);
    if (auto it = snapshots_.find(window); it != snapshots_.end())
        it->second.snapshot.store(std::move(snap), std::memory_order_release);
}

std::optional<PresentStats> SoftwareBackend::present_stats(WindowId id) const {
    std::lock_guard<std::mutex> lk(windows_mutex_);
    auto it = snapshots_.find(id);
    if (it == snapshots_.end()) return std::nullopt;
    const auto& slot = it->second;
    const uint64_t count = slot.present_count.load(std::memory_order_relaxed);
    if (count == 0) return std::nullopt;
    return PresentStats{
        .present_count = count,
        .last_present_at = std::chrono::steady_clock::time_point{std::chrono::nanoseconds{
            slot.last_present_ns.load(std::memory_order_relaxed)}},
        .last_present_ms = slot.last_present_ms.load(std::memory_order_relaxed),
    };
}

void SoftwareBackend::wake() {
    if (!SDL_WasInit(SDL_INIT_EVENTS)) return;   // before run() or after quit: nothing to wake
    SDL_Event wake_ev{};
    wake_ev.type = SDL_EVENT_USER;
    SDL_PushEvent(&wake_ev);
}

void SoftwareBackend::wait_ready() {
    ready_.wait(false, std::memory_order_acquire);
}

void SoftwareBackend::quit() {
    running_.store(false, std::memory_order_relaxed);
    wake();
}

} // namespace prism::backends
