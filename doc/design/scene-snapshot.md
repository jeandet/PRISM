# Scene Snapshot

## Overview

The `SceneSnapshot` is the central data structure in PRISM's pipeline. It is a complete, immutable, plain-data description of what should be on screen at a given moment. The render thread only ever reads snapshots — it never modifies them.

## Structure

```cpp
struct SceneSnapshot {
    uint64_t                                       version;
    std::vector<std::pair<WidgetId, Rect>>         geometry;          // resolved layout
    std::vector<std::shared_ptr<const DrawList>>   draw_lists;        // parallel to geometry
    std::vector<uint32_t>                          z_order;           // back-to-front indices
    DrawList                                       overlay;           // drawn last, unclipped
    std::vector<std::pair<WidgetId, Rect>>         overlay_geometry;  // hit regions for overlays

    // Instrumentation filled by WidgetTree::build_snapshot() (debug inspector, perf_lab)
    double build_time_ms; std::size_t dirty_widget_count, draw_command_count,
                                      approx_bytes, layout_pass_count;
    std::chrono::steady_clock::time_point built_at;
};
```

`geometry`, `draw_lists`, and `z_order` are parallel — `z_order[i]` indexes both. Draw lists are
`shared_ptr<const DrawList>`: a widget that did not change shares its draw list with the previous
snapshot instead of copying it. `const` guarantees no one mutates an entry another snapshot still
holds. The one exception, `resolve_clips()`, only touches entries built fresh this frame (see the
comment in `include/prism/render/scene_snapshot.hpp`).

## Versioning

Each snapshot carries a monotonically increasing `version` number. This serves as:

- Frame correlation ID in traces (link app-side publish to render-side consume).
- Cache key for memoised layout (same version → same geometry → skip re-layout).
- Debugging aid — log which version is being rendered vs published.

## Publication Model

Each publish builds a new snapshot and hands it over through one `atomic_cell` swap; the render
thread loads the latest. Unchanged widgets are reused by pointer (structural sharing), so the cost
of a publish is layout + the dirty widgets' `record()` + one vector of pointers — not a deep copy.
Layout itself is still recomputed on every publish, and runs twice when a virtual list or table
viewport changed (`layout_pass_count`).

The app thread also keeps the published snapshot to hit-test input against it
(`include/prism/app/window_registry.hpp`). That is a read-only use; the snapshot is never mutated
after publication.

## Open Questions

- Snapshot arena/pool allocator to avoid per-frame heap churn.
- Incremental layout: skip measure/arrange for subtrees whose size hints did not change.
