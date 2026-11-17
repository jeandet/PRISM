# Widgets & Sentinel Types

## Overview

In PRISM's **Model-View-Behavior (MVB)** architecture, widgets are the **View** layer — compile-time resolved rendering strategies selected by the field's value type. **Sentinel types** are templated wrapper types that carry both a value and its presentation semantics. **Concepts** drive widget resolution, keeping the system generic and decoupled from specific types.

Two field wrapper types exist:
- `Field<T>` — observable + rendered (widget generates the visual). Part of the **Model** layer.
- `State<T>` — observable + invisible (no widget, used for backend state and synchronisation). Part of the **Model** layer.

## Widget Visual State

Every widget node carries a `WidgetVisualState` that tracks interactive feedback, plus an `std::any edit_state` for ephemeral delegate-managed UI state:

```cpp
struct WidgetVisualState {
    bool hovered = false;   // mouse cursor is over widget
    bool pressed = false;   // mouse button held down on widget
    bool focused = false;   // keyboard focus
};
```

Visual state is managed by the `WidgetTree`:
- `update_hover(optional<WidgetId>)` — MouseMove → hit_test → set hovered, clear previous
- `set_pressed(WidgetId, bool)` — MouseButton down/up → set pressed state

Widgets receive `WidgetNode&` (not `WidgetVisualState&`), giving access to `visual_state`, `edit_state`, and `dirty`:
- `record(DrawList&, const Field<T>&, const WidgetNode&)` — for rendering with hover/press/focus feedback
- `handle_input(Field<T>&, const InputEvent&, WidgetNode&)` — widgets can mutate node state

Widgets are declared in `ui/delegate.hpp`, where `WidgetNode` is only forward-declared (to avoid circular includes), so visual state is read through the `node_vs(node)` helper. Widgets that need the complete `WidgetNode` (e.g. for `edit_state`) declare their methods there and define them in a `delegates/*_delegates.hpp` header (text, dropdown, menu, form, tabs, dialog, color, date, time).

## Field vs State

Both share the same observable core (`.get()`, `.set()`, `.on_change()`, `.observe()`). The only difference: reflection emits a widget for `Field<T>` and skips `State<T>`.

Two subscription APIs:
- `.observe(cb)` — fire-and-forget, connection stored internally, lives as long as the Field/State. Use this by default.
- `.on_change().connect(cb)` — returns `[[nodiscard]] Connection` for scoped lifetime management.

```cpp
struct Dashboard {
    Field<std::string> username{"ada"};   // → text input widget
    Field<bool>        dark_mode{true};        // → checkbox widget
    State<int>         request_count{0};       // → observable, no widget
    State<std::string> session_token{""};      // → observable, no widget
};
```

`Field<T>` holds only the value — no display label. The member name via P2996 reflection provides identity. Display labels (e.g. "Username") are a form-layout concern, not a model concern.

`State<T>` is for values that participate in the signal graph (triggering reactions, cross-block sync) but have no visual representation.

## Sentinel Types

A sentinel type wraps a value and encodes presentation semantics in the type system. Sentinels are templated with a default underlying type:

```cpp
template <StringLike T = std::string>
struct Label { T value; };

template <StringLike T = std::string>
struct TextField {
    T value{};
    std::string placeholder{};
    size_t max_length = 0;  // 0 = unlimited
};

template <StringLike T = std::string>
struct Password {
    T value{};
    std::string placeholder{};
    size_t max_length = 0;
};

template <StringLike T = std::string>
struct TextArea {
    T value{};
    std::string placeholder{};
    size_t max_length = 0;  // 0 = unlimited
    size_t rows = 6;        // visible rows
};

template <Numeric T = double>
struct Slider {
    T value{};
    T min = T{0};
    T max = T{1};
    T step = T{0};  // 0 = continuous
};

struct Button {
    std::string text;
    uint64_t click_count = 0;  // increments on click, observable via on_change()
};

struct Checkbox {
    bool checked = false;
    std::string label{};
};

template <ScopedEnum T>
struct Dropdown {
    T value{};
    std::vector<std::string> labels{};  // empty = reflection-derived names
};
```

Usage in model structs:

```cpp
enum class Theme { Light, Dark, System };

struct Settings {
    Field<std::string>           username{"ada"};                        // → read-only string display
    Field<TextField<>>           search{{.placeholder = "Search..."}};        // → editable text field
    Field<Label<>>               status{{"OK"}};                             // → read-only label
    Field<Password<>>            secret{{.placeholder = "API key"}};          // → masked text input
    Field<Slider<>>              volume{{.value = 0.8}};                     // → continuous slider
    Field<Button>                save{{"Save"}};                             // → clickable button
    Field<bool>                  dark_mode{true};                            // → checkbox (default delegate)
    Field<Checkbox>              notifications{{.checked = true, .label = "Enable"}};  // → checkbox with label
    Field<Theme>                 theme{Theme::Dark};                         // → auto-dropdown via reflection
    Field<Slider<int>>           quality{{.value = 3, .min = 1, .max = 5, .step = 1}};  // → discrete slider
    Field<Label<MyCustomString>> custom{{my_string}};                        // works with any StringLike type
};
```

The template parameter defaults mean `Label<>` is `Label<std::string>`, `Slider<>` is `Slider<double>`. Users can substitute their own types as long as they satisfy the concept.

## Concepts

Concepts define the traits a type must have for a widget to handle it. Widgets match on concepts, not concrete types:

```cpp
template <typename T>
concept StringLike = requires(const T& t) {
    { t.data() } -> std::convertible_to<const char*>;
    { t.size() } -> std::convertible_to<std::size_t>;
};

template <typename T>
concept Numeric = std::integral<T> || std::floating_point<T>;
```

Sentinel-level concepts define what a widget needs from the sentinel wrapper:

```cpp
template <typename T>
concept TextEditable = requires(const T& t) {
    { t.value } -> StringLike;
};

template <typename T>
concept ScopedEnum = std::is_scoped_enum_v<T>;
```

Everything else matches on the sentinel type itself (`Slider<T, O>`, `Checkbox`, `Label<T>`, ...).

## Widget Resolution

Widgets are resolved at compile time: `Widget<T>` is specialized per type or constrained by a
concept. Each has `record()` for rendering and `handle_input()` for interaction; both receive
the `WidgetNode` for visual state, edit state and dirty tracking.

```cpp
template <StringLike T>
struct Widget<Label<T>> {
    static void record(DrawList& dl, const Field<Label<T>>& field, WidgetNode& node);
    static void handle_input(Field<Label<T>>& field, const InputEvent& ev, WidgetNode& node);
};

template <ScopedEnum T>
    requires (!TextEditable<T>)
struct Widget<T> { /* a dropdown over the enum's values */ };
```

A type of your own gets a widget by an explicit specialization:

```cpp
struct TemperatureKnob {
    float value;
    float min = -40.0f;
    float max = 85.0f;
    bool operator==(const TemperatureKnob&) const = default;
};

template <>
struct Widget<TemperatureKnob> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::tab_and_click;
    static void record(DrawList& dl, const Field<TemperatureKnob>& field, WidgetNode& node) {
        // custom circular knob rendering with hover/press feedback
    }
    static void handle_input(Field<TemperatureKnob>& field, const InputEvent& ev, WidgetNode& node) {
        // custom input handling
    }
};
```

## Default Type-to-Widget Mapping

When no sentinel is used, `Field<T>` uses the default widget for `T`:

| `Field<T>` type | Concept matched | Default widget | Status |
|---|---|---|---|
| `Field<bool>` | `Widget<bool>` | Checkbox | **Implemented** |
| `Field<std::string>` | `StringLike` | Read-only text display | **Implemented** |
| `Field<int>`, `Field<double>` | `Numeric` | Read-only number display | **Implemented** |
| `Field<ScopedEnum>` | `ScopedEnum` concept | Auto-dropdown via P2996 `enumerators_of` (or magic_enum) | **Implemented** |
| `Field<Color>` | `Widget<Color>` | Swatch + hex code; popup with a hue bar and a saturation/value square | **Implemented** |
| `Field<std::chrono::year_month_day>` | `Widget<year_month_day>` | ISO date; popup with a month calendar | **Implemented** |
| any other type | primary template | Filled rect (catch-all fallback) | **Implemented** |

Sentinels override these defaults:

| `Field<Sentinel>` type | Widget | Status |
|---|---|---|
| `Field<Label<T>>` | Read-only text label | **Implemented** |
| `Field<TextField<T>>` | Single-line editable text field (cursor, scroll, placeholder, max_length) | **Implemented** |
| `Field<Password<T>>` | Masked text input (shares `text_detail::` helpers with TextField) | **Implemented** |
| `Field<Slider<T>>` | Slider (continuous or discrete based on `step`) | **Implemented** |
| `Field<Button>` | Clickable button (text label, observable click_count) | **Implemented** |
| `Field<Checkbox>` | Checkbox box + label text, whole-widget toggle | **Implemented** |
| `Field<Dropdown<T>>` | Dropdown with custom labels | **Implemented** |
| `Field<TextArea<T>>` | Multiline text editor (character wrap, vertical scroll, Up/Down navigation, configurable rows) | **Implemented** |
| `Field<TimeOfDay>` | `HH:MM`, each part stepped by arrows or the wheel, or typed | **Implemented** |

## Reflection Walk

During widget tree construction, P2996 reflection walks the model struct:

```
for each member M of Model:
    if M is Field<T>:
        resolve Widget<T>
        create widget node with widget
        connect Field<T>::on_change() → mark widget dirty
    if M is State<T>:
        skip (no widget)
    if M is a nested struct (a block):
        recurse
```

## Shared Implementation Helpers

TextField and Password share their implementation through parameterized helpers in
`text_detail::` (`delegates/text_delegates.hpp`):

- `text_field_record<Sentinel, DisplayFn>()`: rendering, parameterized by a display transform
  (`plain_display` for TextField, `masked_display` for Password);
- `text_field_handle_input<Sentinel>()`: input handling, shared identically;
- `mask_string(size_t)`: UTF-8 bullet "●" characters.

The enum dropdown, `Dropdown<T>` and `Choice` share `dropdown_detail::dropdown_record()` and
`dropdown_detail::dropdown_handle_input()`.

TextArea has its own helpers in the same namespace: `text_area_record()`,
`text_area_handle_input()`, `wrap_lines()`, `cursor_to_line_col()`, `line_col_to_cursor()`.

## Strong Coordinate Types

All widget rendering and input handling uses strong types from `types.hpp`. Widget constants use proper dimensional types (`Width`, `Height`, `X`, `Y`). `char_width()` returns `Width`. `TextEditState.scroll_offset` is `DX`, `TextAreaEditState.scroll_y` is `DY`. Widgets draw using `clip_push(Point, Size)` which establishes a local coordinate system — `{0,0}` inside a clip means "top-left of the clipped region." See [draw-list.md](draw-list.md) for the full type system and local coordinate semantics.

## Focus Policy

Each widget declares a compile-time `FocusPolicy`:

```cpp
enum class FocusPolicy : uint8_t { none, tab_and_click };
```

- `none` — non-interactive (Label, StringLike, primary catch-all)
- `tab_and_click` — interactive (bool, Slider, Button, Checkbox, TextField, Password, TextArea, ScopedEnum, Dropdown)

Tab/Shift+Tab cycles through focusable widgets. Click on a focusable widget sets focus. Focused widgets render a blue focus ring (`RectOutline`, 2px).

## Overlay System

Dropdown popups render via the overlay system:

- `WidgetNode::overlay_draws` — per-widget DrawList for content above all widgets
- `SceneSnapshot::overlay` — collected during `layout_flatten`, rendered last
- `SceneSnapshot::overlay_geometry` — `{WidgetId, Rect}` pairs for overlay hit-testing
- `hit_test()` checks overlay geometry first (on top), then normal widget geometry
- `WidgetTree::close_overlays()` — clears overlay draws + resets edit_state on click-outside

## Design Principles

- **Widgets are the View layer** — in MVB terms, widgets handle widget-level input mechanics (e.g. translating a click into a toggle). They are PRISM-internal, not user-written.
- **Concepts over concrete types** — widgets match traits, not specific types. Custom string/numeric types work if they satisfy the concept.
- **Type = specification** — the sentinel type carries both the value and its rendering semantics. No runtime metadata, no string annotations.
- **Templated with defaults** — sentinels are generic (`Label<T = std::string>`) so users can plug in their own types.
- **Explicit override** — template specialization of `Widget<T>` overrides concept-based resolution for custom rendering.
- **Observable core shared** — `Field<T>` and `State<T>` share the same observer machinery. The only difference is UI visibility.

## Open Questions

- Should `Widget` carry layout hints (preferred size, stretch policy) in addition to rendering?
- Animated transitions between widget states (e.g., text field gaining focus) — widget concern or separate animation layer?
