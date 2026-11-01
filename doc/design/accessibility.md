# Accessibility

**Status:** the metadata is in place; the bridge to the platform screen readers is deferred
(decided 2026-11-01: not worth the cost until someone needs it).

## What exists

- Every widget describes itself: `Widget<T>::access_role` and `Widget<T>::describe(field, AccessInfo&)`
  fill an `AccessInfo` (`include/prism/ui/access.hpp`): role, name, value, numeric value and range,
  checked/selected/expanded. The name falls back to the field's member name.
- `ui::Role` values are AccessKit's `Role` discriminants (accesskit 0.25), so a bridge converts with a
  `static_cast`. Compute new ones from the crate source (count the variants of `pub enum Role` in
  `accesskit-0.25.0/src/lib.rs` from static.crates.io), never by hand, and check that the method
  reproduces the values already in the enum.
- `app::build_access_tree(tree)` (`include/prism/app/access_tree.hpp`) turns the widget tree into a
  tree of `AccessNode`s: what a bridge sends as its initial tree.
- Tests: `tests/test_access.cpp`, plus `describe` checks next to each widget's tests.

## What a bridge needs

1. **AccessKit through its C API** (`accesskit-c`): either Rust in the build, or prebuilt libraries
   for every platform and every Python wheel.
2. **One platform adapter per OS**: AT-SPI over D-Bus on Linux, UI Automation on Windows,
   NSAccessibility on macOS, each attached to the SDL window (AccessKit's C examples include SDL).
3. **Bounds**: each node's rectangle on screen. The snapshot geometry has it; `AccessNode` doesn't
   carry it yet.
4. **Actions** from the screen reader (click, focus, increment/decrement, set value, scroll), routed
   back into the widget tree as input.
5. **Incremental updates**: send the nodes whose `AccessInfo` changed after each publish, plus focus.
6. **Text details** for text widgets (character positions, selection), from `ui/text_metrics.hpp` and
   the edit states.

Testing needs a real screen reader on each platform: none of it can be checked headless, unlike
the rest of the UI. That, and the Rust dependency, is why it is deferred.

## Keeping it ready

- New widgets keep declaring `access_role` and `describe` (all built-in widgets do).
- Don't let `AccessInfo` grow platform-specific fields; it mirrors AccessKit's node properties.
