# Render Backend

## Overview

A render backend takes an immutable `SceneSnapshot` and puts pixels on screen. The entire render path — from snapshot interpretation to frame presentation — is owned by the backend. There is no shared intermediate representation between backends.

## Interface

`BackendBase` is an abstract class — runtime-polymorphic so backends can be loaded or switched at runtime:

```cpp
class BackendBase {
public:
    virtual ~BackendBase();
    virtual Window& create_window(WindowConfig cfg) = 0;
    virtual Window* request_window(WindowConfig cfg);   // thread-safe; default calls create_window
    virtual void close_window(WindowId) {}
    virtual void run(std::function<void(const WindowEvent&)> event_cb) = 0;
    virtual void submit(WindowId window, std::shared_ptr<const SceneSnapshot> snap) = 0;
    virtual void wake() = 0;
    virtual void quit() = 0;
    virtual void wait_ready() {}
    virtual std::optional<PresentStats> present_stats(WindowId) const { return std::nullopt; }
};
```

`Backend` is a move-only RAII wrapper with static factories:
- `Backend::software(cfg)` — built-in, statically linked
- `Backend::load(path, cfg)` — dlopen a backend .so (**not implemented**)

The backend owns: window, event loop, rendering, presentation. Communication:
- App → Backend: `submit()` + `wake()`
- Backend → App: `event_cb` callback with `InputEvent` values

Planned: each backend .so would export an `extern "C"` factory returning `std::unique_ptr<BackendBase>`. C++ vtable ABI (not C ABI) — same compiler is required for a C++26 project. This matches how Qt, Mesa, and other C++ libraries handle plugins.

## Why Not Split Rasteriser + Presenter?

A split interface (`Rasteriser` + `Presenter` with a `PixelBuffer` in between) forces GPU backends through a CPU-side pixel buffer, defeating the purpose. Instead:

- The **software backend** internally splits into CPU rasteriser + SDL3 presenter — but that's its private implementation detail.
- A **Vulkan backend** goes GPU-native end-to-end: upload draw list → shaders → swapchain present.
- They share no intermediate types except `SceneSnapshot` and `DrawList`.

## Software Backend (Implemented)

`SoftwareBackend : BackendBase` owns the SDL windows, the event loop, and presentation. Despite
the name, it rasterises through **SDL3's `SDL_Renderer`** (GPU-accelerated where the platform
offers it) and **SDL3_ttf** for text — see `src/backends/sdl_window.cpp`.

```
SceneSnapshot
  → iterate z_order back-to-front
  → SdlWindow::render_cmd() per DrawList command (SDL_RenderFillRect, SDL_RenderGeometry, ...)
  → TextCmd: TTF_RenderText_Blended → texture → draw
  → SDL_RenderPresent
```

Known limitations (tracked in `doc/review-2026-11-01.md`):
- Text: two bundled fonts by role — Noto Sans (`FontRole::Ui`) and JetBrains Mono
  (`FontRole::Mono`), each the other's fallback (`SdlFontSet`). Rasterised text is cached per
  window across frames (`render::FrameCache`); widgets measure through `SdlTextMeasurer`.
- `RoundedRect` and `Circle` are tessellated into anti-aliased meshes (`render/shape_mesh.hpp`,
  backend-neutral) and drawn with `SDL_RenderGeometry`.
- Full repaint of the window on every present (no damage tracking).

**Headless test types (header-only, no SDL):** `PixelBuffer` and `SoftwareRenderer`
(`include/prism/render/`) are a tiny CPU rasteriser kept only for unit tests — `FilledRect` only,
everything else is a no-op. They are not on the production path.

## Renderers

`SoftwareBackend` draws with `SDL_Renderer` by default, or with the WebGPU renderer
(`backends/wgpu_renderer.hpp`) when `RenderConfig::renderer` is `Renderer::WebGpu` or the
environment has `PRISM_RENDERER=webgpu`, in builds with `-Dwebgpu=enabled`. Windows, input,
clipboard and pacing are the same either way; see `second-backend.md`.

## Backend Contract

What any backend must honour, beyond the `BackendBase` signatures. The SDL backend is the
reference; `tests/test_pixels.cpp` checks some of it on real pixels.

- **Commands**: `FilledRect`, `RectOutline`, `RoundedRect`, `Circle`, `Line`, `Polyline`,
  `FilledPolygon` (triangle-strip order), `TextCmd`, `ImageCmd`, `ClipPush`/`ClipPop`
  (`render/draw_list.hpp`). Colors are straight RGBA8 with alpha blending. Anti-aliasing of
  shapes may come from `render/shape_mesh.hpp` (meshes with an alpha fringe) or be native.
- **Clips arrive pre-intersected** (`resolve_clips` in `scene_snapshot.hpp`): apply each as is.
- **Order**: `z_order` back to front, then `overlay`, which is never clipped.
- **Units**: every coordinate and `TextCmd::size` is in points. Rasterise at points × the
  window's pixel density, or text and carets drift (the HiDPI selection bug of 2026-11-01).
- **Text**: draw with the font `FontRole` names (`Ui` = Noto Sans, `Mono` = JetBrains Mono, each
  the other's fallback), and provide a `TextMeasurer` whose widths match what is drawn, at every
  density. Measurement runs on the logic thread, drawing on the render thread.
- **Snapshots** are immutable and their `DrawList`s may be shared with earlier snapshots.
- **Cadence**: present only when a window's snapshot changed or the platform asks for a redraw;
  input events are delivered as they arrive, never paced.
- **Threads**: `run()` owns the platform's main thread; `submit()`, `wake()` and
  `present_stats()` are the only calls from other threads. The GPU context lives on the render
  thread.
- **Input**: key codes and modifiers are SDL's values (`input/input_event.hpp`); a non-SDL
  backend translates its native codes into them.

## Present Cadence

`SoftwareBackend` paces presents to the display's refresh rate (`RenderConfig::frame_pacing`, default on): the run loop waits on `SDL_WaitEventTimeout` against a `FramePacer` frame clock (`include/prism/backends/frame_pacer.hpp`) and presents at most one frame per window per refresh interval. `RenderConfig::target_fps` overrides the rate (0 = display rate, 60 Hz fallback). Input event dispatch to the app thread is *not* paced — only presents are — so input latency is independent of the frame clock. A window is presented only when its snapshot changed since the last present (`shared_ptr` comparison) or SDL requested a redraw (`SDL_EVENT_WINDOW_EXPOSED`); a high-rate publish stream coalesces into one present per frame. Renderer vsync is requested as a best-effort tear guard. `frame_pacing = false` restores event-rate-bound behavior.

Present statistics (`present_count`, last-present timestamp and duration) are exposed to apps via `Backend::present_stats(WindowId)` — pull-based; backends that don't present return `std::nullopt`.

## Build System

| Meson dependency | Links | Purpose |
|---|---|---|
| `prism_core_dep` | headers + stdexec, fmt (+ magic_enum without reflection) | Widgets, DrawList, SceneSnapshot, concurrency primitives — no SDL |
| `prism_software_backend_dep` | `libprism-software-backend` + SDL3 + SDL3_ttf | The SDL backend |
| `prism_dep` | core + software backend | Convenience for apps |

Headless tests link only `prism_core_dep` — no SDL required. There is a single backend library
today; `Backend::load()` (dlopen a backend `.so`) is not implemented.

## Window Ownership

The backend creates the SDL windows (several per app — see `WindowRegistry`) and owns the event pump. `SDL_Init(SDL_INIT_VIDEO)` is called inside `SoftwareBackend::run()` (SDL3 requires event pumping on the init thread). A `wait_ready()` handshake ensures the app thread doesn't call `wake()` before SDL is initialised.

## GPU Backend (Phase 5)

```
SceneSnapshot
  → upload DrawList commands as GPU vertex/index data
  → fragment shader rasterisation (or compute shader for tiles)
  → GPU-side compositing with blending
  → swapchain present
```

Tile strategy differs: CPU wants spatial tiles for parallelism, GPU wants draw-call batching for throughput.

## Packaging (planned — today everything ships as one `libprism-software-backend`)

| Package | Contents |
|---|---|
| `libprism-dev` | Headers (header-only core) + `BackendBase` vtable anchor (.a) |
| `libprism-software` | `SoftwareBackend` .so (links SDL3) |
| `libprism-vulkan` | `VulkanBackend` .so (links Vulkan) [future] |

`BackendBase::~BackendBase()` is defined out-of-line in `backend.cpp` to anchor the vtable in a single translation unit. Soname versioning on vtable-breaking changes.

## Open Questions

- Glyph atlas vs. per-string texture cache — the first step for text performance.
- Damage tracking: worth it on a GPU-backed renderer, or is full repaint fine?
- Tile splitting for parallel CPU rasterisation — deferred until a CPU rasteriser is real.
- Which second backend: see `second-backend.md`.
