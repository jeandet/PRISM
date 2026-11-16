# Blocks: Reusable UI + Logic Bundles

> **Today:** a block is a plain struct with fields, an optional `view()`, and its reactions, placed
> with `vb.block(b)` (see the README's Blocks section and the gallery's "Build your own" tab). It
> wires what needs the running app in an optional `void attach(app::AppContext&)`, found by a
> concept (option 2 below, not the base class): the tree notes it the first time it builds the
> block, once per object, children before parents, and the app runs it on the logic thread before
> its setup (later for blocks built later, e.g. in a new window). What follows is the original
> exploration of a base class; it was not implemented.

## Overview

A `Block` is an inheritance-based base class that lets a struct bundle both its **Model** (Fields) and its **Behavior** (setup logic) in MVB terms. Blocks are self-contained, composable, and reusable — analogous to FastAPI sub-apps or VHDL entities with ports.

Today, models are plain structs: reflection builds the widget tree (the **View** is automatic), but all **Behavior** wiring lives in the top-level `model_app` setup lambda. Blocks move the Behavior into the struct itself, enabling library-grade reusable pieces (hex editor, plot widget, sensor dashboard panel).

## Motivation

Without Block, a reusable "gas sensor panel" requires the consumer to know and reproduce its internal Behavior wiring. With Block, the consumer just nests it and connects to its external-facing Fields:

```cpp
// Without Block — consumer must wire internals
app::model_app("Dashboard", dashboard, [&](AppContext& ctx) {
    // repeated per sensor instance, must know GasSensor internals
    dashboard.sensor_a.pressure.on_change() | core::on(ctx.scheduler())
        | core::then([&](const Slider<>& s) {
            if (s.value > 0.9) dashboard.sensor_a.status.set({"WARNING"});
        });
    // ... same for sensor_b, sensor_c, ...
});

// With Block — internals are self-wired
struct Dashboard : prism::Block {
    GasSensor sensor_a;  // wires itself
    GasSensor sensor_b;  // wires itself
    Field<Label<>> summary{{"All OK"}};

    void setup(AppContext& ctx) override {
        // only wire the interface — connect child outputs
        connect(sensor_a.status.on_change() | core::on(ctx.scheduler())
            | core::then([this](const Label<>& l) {
                if (l.text == "WARNING") summary.set({"Check sensor A!"});
            }));
    }
};
```

Blocks also split complexity: even with a single instance, encapsulating logic + UI together makes the code easier to reason about.

## Base Class

```cpp
namespace prism {

class Block {
public:
    virtual ~Block() = default;

    // Self-wiring hook — called bottom-up after tree is built
    virtual void setup(AppContext& ctx) {}

    // Custom rendering surface (future — requires canvas infrastructure)
    virtual void canvas(DrawList& dl, const Rect& bounds, const WidgetNode& node) {}

    // Custom input handling for canvas blocks (future)
    virtual void handle_input(const InputEvent& ev, WidgetNode& node) {}

protected:
    void connect(Connection conn) { connections_.push_back(std::move(conn)); }

private:
    std::vector<Connection> connections_;
};

} // namespace prism
```

- `setup()` defaults to no-op so pure containers don't need to override
- `canvas()` and `handle_input()` default to no-op — a Block with neither is a pure Field container, with both is a custom rendering surface
- `connect()` is protected — only the block itself stores its wiring
- Connections are destroyed with the Block

## Detection and Tree Integration

`build_container` currently detects blocks via reflection (any struct with Fields/States). With the Block base class, detection splits:

- **Plain struct** (current behavior): detected by reflection, no `setup()` called, parent wires manually
- **Block subclass**: detected by `std::is_base_of_v<Block, T>`, tree built recursively from Fields the same way, instance registered for `setup()` invocation

During tree construction, `build_container` collects `Block*` pointers. After the full tree is built and indexed, `setup()` is called **bottom-up** — children before parents. This guarantees a parent's `setup()` can rely on nested Blocks being fully wired.

```cpp
// In WidgetTree constructor (pseudocode)
WidgetTree(Model& model) {
    root_ = build_container(model);  // builds tree, collects Block pointers
    build_index(root_);

    // Bottom-up: children registered first, called first
    for (auto* comp : blocks_)
        comp->setup(app_context_);

    clear_dirty();
}
```

## Composition Patterns

### Container of Fields — bundles UI + logic

The simplest and most common pattern. The Block owns Fields, wires them in `setup()`, and the parent interacts only through the public Fields (its "ports"):

```cpp
struct GasSensor : prism::Block {
    Field<Label<>> name{{""}};
    Field<Slider<>> pressure{{}};
    Field<Label<>> status{{"OK"}};

    void setup(AppContext& ctx) override {
        connect(pressure.on_change() | core::on(ctx.scheduler())
            | core::then([this](const Slider<>& s) {
                if (s.value > 0.9) status.set({"WARNING"});
            }));
    }
};
```

### Custom rendering surface (implemented via ViewBuilder::canvas)

The `vb.canvas(model)` method in ViewBuilder provides the canvas escape hatch. Any struct with a `canvas(DrawList&, Rect, const WidgetNode&)` method can draw custom content. Input handling is optional via `handle_canvas_input()`. See the [canvas spec](../../docs/superpowers/specs/2026-03-29-canvas-escape-hatch-design.md) for details.

```cpp
struct PlotView {
    Field<std::vector<float>> data{{}};

    void canvas(DrawList& dl, Rect bounds, const WidgetNode& node) {
        // custom plot rendering — pan, grid, series
    }
    void handle_canvas_input(const InputEvent& ev, WidgetNode& node, Rect bounds) {
        // mouse drag for pan, scroll for zoom
    }
    void view(WidgetTree::ViewBuilder& vb) {
        vb.canvas(*this).depends_on(data);
    }
};
```

### Hybrid — custom surface + child Fields

Fields provide standard widgets (toolbar, search bar); the canvas provides the custom area:

```cpp
struct HexEditor : prism::Block {
    Field<TextField<>> search_bar{{}};
    Field<Label<>> offset_display{{"0x0000"}};

    void canvas(DrawList& dl, const Rect& bounds, const WidgetNode& node) override {
        // hex grid rendering
    }
    void handle_input(const InputEvent& ev, WidgetNode& node) override {
        // cursor movement, selection, editing
    }
    void setup(AppContext& ctx) override {
        // wire search bar to scroll position
    }
};
```

### Layered composition — widgets inside blocks

A `TextEdit` delegate (atomic widget) can be used inside a `TextEditor` block that adds line numbers, syntax highlighting, file I/O, and a search toolbar. Delegates handle atomic rendering; Blocks handle orchestration.

### A block as a bar's content

A block placed in `vb.toolbar` or `vb.status_bar` lays its widgets out along the bar's row, so
a `vb.spacer()` in its `view()` pushes what follows to the bar's far end. Anywhere else a block
is a column of its own, placed as one unit.

```cpp
vb.status_bar([&] { vb.block(status); });   // status.view(): label, spacer, label
```

### Top-level app as Block

`model_app` just becomes "run this Block as a window." If the model is a plain struct, current behavior is preserved (setup lambda still works):

```cpp
int main() {
    Dashboard dashboard;
    app::model_app("Dashboard", dashboard);  // setup() called automatically
}
```

## Backward Compatibility

- Plain structs (no Block base) work exactly as today — no migration needed
- The `model_app` setup lambda remains available for top-level wiring
- A model can mix plain nested structs and Block subclasses in the same tree
- Existing delegates, layout, rendering, and input routing are unchanged

## Design Decisions

### Skipped fields: a value, not a widget

A block sometimes keeps a `Field` that no widget shows: a selection index, the mask behind a row of
checkboxes. A `view()` that does not place it is right, so `[[=core::skip]]` on the member tells the
debug "not placed by view()" check to skip it — the same annotation that leaves a member out of the
automatic UI (`core/reflect_annotations.hpp`). Before it could be used there, the only way to
silence that warning was to hide the field in a nested struct, which told the reader nothing about
why it was there. Only a compiler with reflection parses the annotation (clang 22 rejects `[[=x]]`), so
code that must build everywhere still nests the field; the check does not run without reflection.

### Why inheritance, not concepts

Three approaches were considered:

1. **Inheritance** (`struct Foo : Block`) — explicit, familiar (Qt-like), provides `connect()` helper and connection storage for free
2. **Concept-based** (detect `setup()` method via reflection) — no base class, but requires manual connection storage in every block
3. **CRTP** (`struct Foo : Block<Foo>`) — static polymorphism, but `canvas()` and `handle_input()` need virtual dispatch anyway for the tree to call them uniformly

Inheritance was chosen for simplicity and alignment with the Qt-like SDK design principles. The vtable cost is negligible: `setup()` runs once at init, `handle_input()` at event rate.

### Why bottom-up setup order

Children wire first, parents wire second. A parent's `setup()` can assume nested Blocks are fully operational. This matches VHDL elaboration order and avoids ordering bugs.

### Why Block owns connections

The alternative (tree owns connections, Block returns them) adds complexity for no benefit in the static case. Self-contained ownership means the Block is a single unit of deployment — move it between projects and it just works.

## Deferred (separate designs)

| Topic | Why separate |
|-------|-------------|
| `BlockList<T>` (dynamic instances) | Touches list/table widgets, partial tree rebuild, virtual scrolling — entirely different design space |
| `canvas()` / `handle_input()` implementation | Depends on canvas rendering infrastructure not yet built |
| Layout hints (min/max size, stretch) | Layout system concern, needed for canvas blocks but independent |
| Theming / style context | Needed for library blocks to match host app appearance |
| Field visibility (internal vs port) | "Private" Fields hidden from parent tree — interesting but not blocking |
| Block lifecycle (teardown, suspend) | Only needed with dynamic add/remove |

## Open Questions

- Should `setup()` receive the Block's own `WidgetNode&` in addition to `AppContext`? This would let it inspect its tree position, but adds coupling.
- For hybrid Blocks (Fields + canvas), how does layout decide the canvas area's size relative to the Fields? This overlaps with the layout hints design.
- Should there be a way to declare a Field as "internal" (no widget generated, but still observable)? `State<T>` fills this role partially, but a Field that renders only within the block's canvas would be different.
