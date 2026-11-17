# Canvas

A canvas is a struct that draws itself. Give it a `canvas()` method and place it with
`vb.canvas(*this)`.

```cpp
struct Waveform {
    core::Field<ui::Slider<>> frequency{{.value = 3.0, .min = 0.5, .max = 8.0}};

    void canvas(render::DrawList& dl, core::Rect bounds, const ui::WidgetNode& node) {
        auto& t = *node.theme;
        float w = bounds.extent.w.raw();
        float h = bounds.extent.h.raw();
        float cy = h * 0.5f;
        float freq = static_cast<float>(value(frequency));

        dl.rounded_rect(bounds, t.surface, 6.f);
        dl.line({core::X{0}, core::Y{cy}}, {core::X{w}, core::Y{cy}}, t.track, 1.f);

        int steps = static_cast<int>(w);
        std::vector<core::Point> pts(static_cast<size_t>(steps));
        for (int i = 0; i < steps; ++i) {
            float x = static_cast<float>(i);
            float phase = freq * x / w;
            float y = cy - 0.4f * h * std::sin(2.f * std::numbers::pi_v<float> * phase);
            pts[static_cast<size_t>(i)] = {core::X{x}, core::Y{y}};
        }
        dl.polyline(pts, t.accent, 2.f);
    }

    void view(app::ViewBuilder& vb) {
        vb.canvas(*this).depends_on(frequency);
        vb.widget(frequency);
    }
};
```

<img src="../../screenshots/canvas.svg" alt="A sine wave drawn on a canvas above its frequency slider" width="400"/>

## Drawing

`canvas(dl, bounds, node)` records drawing commands into `dl`.
Coordinates are the canvas's own: (0, 0) is its top-left corner, and `bounds.extent` is its
size. Coordinates are typed: `core::X` and `core::Y` for positions, `core::Width` and
`core::Height` for sizes. Mixing an X with a Y does not compile.
`node.theme` holds the theme's colors: draw with them, and the canvas follows light and dark.

| `render::DrawList` method | Draws |
|---|---|
| `filled_rect(rect, color)` | a filled rectangle |
| `rect_outline(rect, color, thickness = 1)` | a rectangle's outline |
| `rounded_rect(rect, color, radius, thickness = 0)` | a rounded rectangle; filled when `thickness` is 0 |
| `circle(center, radius, color, thickness = 0)` | a circle; filled when `thickness` is 0 |
| `line(from, to, color, thickness = 1)` | a line |
| `polyline(points, color, thickness = 1)` | connected lines |
| `filled_polygon(points, color)` | a filled polygon |
| `text(string, origin, size, color)` | text, with an optional angle, anchor and font style |
| `image(image, rect, tint = white)` | an image fitted into `rect` |
| `clip_push(origin, extent)`, `clip_pop()` | draw only inside a rectangle, until the pop |

Images come from `render::load_image(path)` or `render::Image::from_rgba(w, h, pixels)`;
see [Picture](../controls/picture.md).

## When it redraws

A canvas redraws when its size changes, and when one of the fields given to
`.depends_on(...)` changes. List every field `canvas()` reads:

| Handle method | Does |
|---|---|
| `.depends_on(a, b...)` | redraws when any of these change |
| `.min_size(core::Height{h})`, `.min_size(core::Width{w})` | at least this tall or wide |

## Mouse and keys

Add a `handle_canvas_input` method and the canvas takes the focus and the input:

```cpp
struct Pad {
    core::Field<core::Point> last{};

    void canvas(render::DrawList& dl, core::Rect, const ui::WidgetNode& node) {
        dl.circle(last.get(), 6.f, node.theme->accent);
    }
    void handle_canvas_input(const input::InputEvent& ev, ui::WidgetNode&, core::Rect) {
        if (auto* mb = std::get_if<input::MouseButton>(&ev); mb && mb->pressed) last = mb->position;
    }
    void view(app::ViewBuilder& vb) { vb.canvas(*this).depends_on(last); }
};
```

`input::InputEvent` is a variant: `MouseMove`, `MouseButton`, `MouseScroll`, `KeyPress`,
`KeyRelease`, `TextInput` and others. Positions are in the canvas's coordinates.

## See also

- [Plot](plot.md): a canvas PRISM ships.
