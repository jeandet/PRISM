# A second rendering backend

**Status:** investigation (2026-11-01). Nothing built yet; a decision is needed first.

## Why

Today one backend draws everything: SDL3 windows and input, `SDL_Renderer` for shapes, SDL_ttf
for text (`render-backend.md`). A second backend proves the backend seam is real before more code
grows around SDL's semantics, and is the road to Phase 5 (GPU text, large scenes).

## Where the seam stands

Checked against the code (file references in the opencode inventory of 2026-11-01):

- No header above `include/prism/backends/` includes SDL. The contract is written down in
  `render-backend.md` ("Backend Contract").
- Reusable as is: `DrawList`, `SceneSnapshot` and its clip resolution, `shape_mesh.hpp`
  (anti-aliased triangle meshes for rounded rects and circles), `FrameCache`, `FramePacer`,
  `WindowRegistry`, the clipboard queue, the access tree, and `tests/test_pixels.cpp` as a check.
- Every backend writes its own: window and event pump with input translation, the draw calls,
  text drawing **and** text measurement (kept in agreement), image upload, cursors, present.
- Small leaks to fix with the second backend, not before: key codes and modifiers are SDL's
  numbers (translate them, one table per backend); `FramePacer` and the bundled-font lookup live
  in SDL-named places though they are SDL-free; `Backend::load()` (choose a backend at run time)
  does not exist, so a backend is chosen at link time.
- The wait-free snapshot handoff spec
  (`docs/superpowers/specs/2026-11-01-wait-free-snapshot-handoff-design.md`) still fits, and
  lands with the second backend as a shared component.

## Options

| | What | Keeps SDL for windows/input | New dependency | Web (wasm) | Work |
|---|---|---|---|---|---|
| A | **WebGPU renderer** (Dawn or wgpu-native, standard `webgpu.h`) | yes | WebGPU implementation | the same API in browsers | large: pipelines, text atlas, images |
| B | **SDL_GPU renderer** (SDL3's Vulkan/Metal/D3D12 layer) | yes | none (SDL_shadercross for shaders) | no WebGPU target yet | large, same as A |
| C | **ThorVG renderer** (vector engine with CPU, GL and WebGPU engines) | yes | ThorVG | via its WebGPU/GL engines | medium: map commands, fonts through ThorVG |
| D | **A non-SDL platform layer** (native Wayland/Win32/Cocoa, or embedding in a host toolkit) | no | per platform | no | very large; a separate decision |

Notes from the search:

- WebGPU's C API (`webgpu.h`) is shared by Dawn, wgpu-native and Emscripten, but still published
  as dated snapshots rather than stable releases: expect some churn when updating.
- SDL_GPU works but is young for 2D; the one detailed 2026 report (Simutrans) found its
  experimental 2D renderer slower than their software path so far.
- Text on the GPU: the Slug algorithm (glyphs drawn from their outlines, sharp at any scale) was
  put in the public domain in March 2026, with reference shaders published. A glyph atlas is the
  simpler first step; Slug is the upgrade for zoomable text (plots, large displays).

## Recommendation

**A: a WebGPU renderer, with SDL kept for windows and input.**

1. It is the only option where one renderer also serves the browser: the wasm demo would move to
   WebGPU with the same code.
2. It keeps the part of SDL that works well (windows, input, IME, clipboard on every platform) and
   replaces the part that limits us (`SDL_Renderer`: no shaders, per-string text textures).
3. The pieces line up: shapes are already meshes, the contract is written, the pixel tests can
   check the new backend's text and selection the same way.

Order of work: the shared snapshot mailbox and the helper moves first; then rectangles, meshes,
clips and images; then text through a glyph atlas (FreeType, already a dependency of SDL_ttf);
the pixel tests run against both backends. Slug text, damage tracking and tiles come after.

Option D is a different question (do we stop depending on SDL for windows at all?). Nothing in
the contract blocks it later.
