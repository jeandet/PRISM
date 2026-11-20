#pragma once

// Draws a published SceneSnapshot into a Canvas: walks z_order back-to-front, each entry's draw
// list, then the overlay on top with no clip. ClipPush pushes the (already resolved) rect as the
// new clip -- intersected with the canvas bounds -- and ClipPop restores the previous one.
// Coordinates are logical points = pixels here (scale 1).
//
// Clearing is the caller's job: fill the canvas with snap.window.background first, as the SDL
// backend clears with it (SDL_RenderClear in SdlWindow::render_snapshot).

#include <prism/raster/canvas.hpp>

#include <prism/render/scene_snapshot.hpp>

namespace prism::raster {

void draw(Canvas& canvas, const prism::render::SceneSnapshot& snap, const Fonts& fonts);

} // namespace prism::raster
