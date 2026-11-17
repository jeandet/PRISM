# Picture

A picture shows an image on its own: a logo, a preview, a plot rendered elsewhere.

```cpp
#include <prism/render/image_decoder.hpp>   // render::load_image

struct Splash {
    core::Field<ui::Picture> logo{{.image = *render::load_image("logo.png"),
                                   .size = core::Size{core::Width{200}, core::Height{100}},
                                   .description = "The PRISM logo"}};

    void view(app::ViewBuilder& vb) { vb.widget(logo); }
};
```

## `ui::Picture`

| Member | Type | Default | Meaning |
|---|---|---|---|
| `image` | `std::shared_ptr<const render::Image>` | none | what it shows |
| `size` | `std::optional<core::Size>` | none | the box it fits in, proportions kept and centred; none: the image's own size |
| `description` | `std::string` | `""` | what it shows, for assistive technology |

## Getting an image

- `render::load_image(path)` reads a PNG, JPEG or SVG file. It returns a
  `std::expected<std::shared_ptr<const render::Image>, std::string>`: the image, or why it failed.
  Pass a size as its second argument to draw an SVG at that size.
- `render::decode_image(bytes)` does the same from bytes in memory, a download say.
- `render::Image::from_rgba(width, height, pixels)` wraps pixels you computed: 4 bytes per
  pixel, row after row. It returns a `std::expected` too.

The two file loaders are safe to call from any thread.

## Using it

A picture takes no focus and no input. Setting a new image redraws it.

## See also

- [Button](button.md) and [Menu](../popups/menu.md) items: both take an `icon`.
- [Canvas](../graphics/canvas.md): `dl.image(img, rect)` draws an image among your own drawing.
