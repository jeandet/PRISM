#pragma once

#include <prism/input/input_event.hpp>
#include <prism/render/scene_snapshot.hpp>

#include <cassert>
#include <optional>

namespace prism::input {
using namespace prism::core;
using namespace prism::render;

// What a click can hit of geometry entry `i`: its visible part.
[[nodiscard]] inline Rect hit_area(const SceneSnapshot& snap, std::size_t i) {
    assert(snap.visible.empty() || snap.visible.size() == snap.geometry.size());
    return snap.visible.empty() ? snap.geometry[i].second : snap.visible[i];
}

[[nodiscard]] inline std::optional<WidgetId> hit_test(
    const SceneSnapshot& snap, Point pos)
{
    // Overlays render on top — check them first
    for (auto it = snap.overlay_geometry.rbegin(); it != snap.overlay_geometry.rend(); ++it) {
        auto& [id, rect] = *it;
        if (rect.contains(pos))
            return id;
    }
    // Then normal widget geometry, back-to-front (skip synthetic entries with id=0)
    for (auto it = snap.z_order.rbegin(); it != snap.z_order.rend(); ++it) {
        const WidgetId id = snap.geometry[*it].first;
        if (id != 0 && hit_area(snap, *it).contains(pos))
            return id;
    }
    return std::nullopt;
}

[[nodiscard]] inline float scale_of(const SceneSnapshot& snap, std::size_t i) {
    assert(snap.scale.empty() || snap.scale.size() == snap.geometry.size());
    return snap.scale.empty() ? 1.f : snap.scale[i];
}

// Where a widget is: in the window, or else in the overlay (the widgets of an open popup), so
// events reach them in their own coordinates too.
[[nodiscard]] inline std::optional<Placed> find_widget_place(const SceneSnapshot& snap, WidgetId id) {
    for (std::size_t i = 0; i < snap.geometry.size(); ++i)
        if (snap.geometry[i].first == id) return Placed{snap.geometry[i].second, scale_of(snap, i)};
    for (auto& [wid, rect] : snap.overlay_placed)
        if (wid == id) return Placed{rect};
    for (auto& [wid, rect] : snap.overlay_geometry)
        if (wid == id) return Placed{rect};
    return std::nullopt;
}

[[nodiscard]] inline std::optional<Rect> find_widget_rect(const SceneSnapshot& snap, WidgetId id) {
    return find_widget_place(snap, id).transform([](const Placed& p) { return p.rect; });
}

// The scroll view whose scrollbar is under `pos` (a press there drags its thumb).
[[nodiscard]] inline std::optional<WidgetId> hit_test_scrollbar(const SceneSnapshot& snap, Point pos) {
    for (auto it = snap.scrollbar_geometry.rbegin(); it != snap.scrollbar_geometry.rend(); ++it)
        if (it->second.contains(pos)) return it->first;
    return std::nullopt;
}

[[nodiscard]] inline std::optional<WidgetId> hit_test_overlay(
    const SceneSnapshot& snap, Point pos)
{
    for (auto it = snap.overlay_geometry.rbegin(); it != snap.overlay_geometry.rend(); ++it) {
        if (it->second.contains(pos))
            return it->first;
    }
    return std::nullopt;
}

} // namespace prism::input
