#pragma once

#include <prism/ui/mnemonic.hpp>
#include <prism/ui/cursor.hpp>
#include <prism/ui/context.hpp>
#include <prism/ui/text_metrics.hpp>
#include <prism/ui/text_edit.hpp>
#include <prism/ui/access.hpp>
#include <prism/render/draw_list.hpp>
#include <prism/core/field.hpp>
#include <prism/input/input_event.hpp>

#include <algorithm>
#include <any>
#include <memory>
#include <optional>
#if __cpp_impl_reflection
#include <meta>
#else
// Included here, at file scope, so magic_enum's compile-time
// __PRETTY_FUNCTION__ parsing sees its own unqualified ::magic_enum::
// names. Including it from inside namespace prism::ui (as before)
// nests the library as prism::ui::magic_enum::..., which shifts GCC's
// __PRETTY_FUNCTION__ output and silently breaks magic_enum's
// fixed-offset name extraction -- every candidate enum value in the
// scan range then reads as "valid" with a garbled name.
#include <magic_enum/magic_enum.hpp>
#endif
#include <string>
#include <type_traits>
#include <vector>

namespace prism::ui {
using namespace prism::core;
using namespace prism::render;
using namespace prism::input;


struct WidgetVisualState {
    bool hovered = false;
    bool pressed = false;
    bool focused = false;
    CursorShape cursor = CursorShape::Default;
};

template <typename T>
concept Numeric = std::integral<T> || std::floating_point<T>;

template <typename T>
concept StringLike = requires(const T& t) {
    { t.data() } -> std::convertible_to<const char*>;
    { t.size() } -> std::convertible_to<std::size_t>;
};

enum class FocusPolicy : uint8_t { none, tab_and_click };

enum class LayoutKind : uint8_t { Default, Row, Column, Spacer, Canvas, Scroll, VirtualList, Table, Tabs, Handle };

enum class ScrollBarPolicy : uint8_t { Auto, Always, Never };
enum class ScrollEventPolicy : uint8_t { ConsumeAlways, BubbleAtBounds };

// Sentinel: read-only label
template <StringLike T = std::string>
struct Label {
    T value{};
    bool operator==(const Label&) const = default;
};

// Concept: type wrapping a StringLike value (for editable text delegates)
template <typename T>
concept TextEditable = requires(const T& t) {
    { t.value } -> StringLike;
};

// Sentinel: single-line editable text field
template <StringLike T = std::string>
struct TextField {
    T value{};
    std::string placeholder{};
    size_t max_length = 0;
    bool operator==(const TextField&) const = default;
};

// Sentinel: masked text field (displays bullets instead of actual text)
template <StringLike T = std::string>
struct Password {
    T value{};
    std::string placeholder{};
    size_t max_length = 0;
    bool operator==(const Password&) const = default;
};

// A byte range [begin, end) of a text value.
struct TextRange {
    size_t begin = 0, end = 0;
    bool operator==(const TextRange&) const = default;
};

// Sentinel: multiline text editor with character wrapping
template <StringLike T = std::string>
struct TextArea {
    T value{};
    std::string placeholder{};
    size_t max_length = 0;   // 0 = unlimited
    size_t rows = 6;         // visible lines, determines widget height
    std::vector<TextRange> highlights{};   // e.g. search matches; drawn under the text
    bool operator==(const TextArea&) const = default;
};

// Per-character width of the no-measurer estimate (see text_width in ui/text_metrics.hpp).
// Widgets measure through Theme::measurer; this remains as a shorthand for tests.
inline Width char_width(float font_size) { return Width{0.6f * font_size}; }

// Ephemeral cursor state for text editing delegates
struct TextEditState {
    size_t cursor = 0;
    std::optional<size_t> anchor;   // selection start; empty when nothing is selected
    bool dragging = false;   // a mouse press started a selection drag
    EditHistory history;
    std::string preedit;          // input-method composition shown at the caret, not in the value
    std::size_t preedit_caret = 0;   // byte offset of the IME caret inside preedit
    DX scroll_offset{0};
    // Suggestions under a ComboBox: the highlighted one, and whether Escape or a pick closed the
    // list (an edit reopens it).
    std::size_t completion_index = 0;
    bool completion_closed = false;
};

// Ephemeral cursor state for multiline text editing
struct TextAreaEditState {
    size_t cursor = 0;
    std::optional<size_t> anchor;
    bool dragging = false;
    EditHistory history;
    std::string preedit;
    std::size_t preedit_caret = 0;
    DY scroll_y{0};
    // Where Up/Down aim, in pixels from the line start: kept across a run of Up/Down so a short
    // line on the way does not pull the caret left; any other input forgets it.
    std::optional<float> goal_x;
    std::size_t goal_cursor = 0;   // the caret goal_x was last used for: moved since, it is stale
    // At a soft wrap one offset is both the end of a line and the start of the next: true when
    // the caret means the end (it got there by End, or a click or Up/Down past a line's end).
    bool upstream = false;
    // For the scrollbar and the wheel (app::get_scroll_view), refreshed by each record().
    Height viewport_h{0};
    Height content_h{0};
    std::uint8_t show_ticks = 0;
};

// Ephemeral scroll state (stored in WidgetNode::edit_state)
struct ScrollState {
    DY offset_y{0};
    DX offset_x{0};
    Height content_h{0};
    Width content_w{0};
    Height viewport_h{0};
    Width viewport_w{0};
    ScrollBarPolicy scrollbar = ScrollBarPolicy::Auto;
    ScrollEventPolicy event_policy = ScrollEventPolicy::BubbleAtBounds;
    uint8_t show_ticks = 0;
};

// Ephemeral split-pane state (stored in WidgetNode::edit_state on a Row/Column container)
struct SplitState {
    bool engaged = false;           // true once the user has dragged a handle at least once
    std::vector<float> pane_sizes;  // main-axis size per pane (excludes Handle thickness); valid when engaged
};

// Sentinel: scroll container with observable position
struct ScrollArea {
    ScrollBarPolicy scrollbar = ScrollBarPolicy::Auto;
    ScrollEventPolicy event_policy = ScrollEventPolicy::BubbleAtBounds;
    DY scroll_y{0};
    DX scroll_x{0};
    bool operator==(const ScrollArea&) const = default;
};

// Concept: scoped enum type
template <typename T>
concept ScopedEnum = std::is_scoped_enum_v<T>;

// Enum introspection helpers for scoped enums
#if __cpp_impl_reflection

template <ScopedEnum T>
consteval size_t enum_count() {
    return std::meta::enumerators_of(^^T).size();
}

template <ScopedEnum T>
std::string enum_label(size_t index) {
    static constexpr auto enums = std::define_static_array(
        std::meta::enumerators_of(^^T));
    std::string result;
    size_t i = 0;
    template for (constexpr auto e : enums) {
        if (i == index) result = std::string(std::meta::identifier_of(e));
        ++i;
    }
    return result;
}

template <ScopedEnum T>
constexpr size_t enum_index(T value) {
    static constexpr auto enums = std::define_static_array(
        std::meta::enumerators_of(^^T));
    size_t result = 0, i = 0;
    template for (constexpr auto e : enums) {
        if ([:e:] == value) result = i;
        ++i;
    }
    return result;
}

template <ScopedEnum T>
T enum_from_index(size_t index) {
    static constexpr auto enums = std::define_static_array(
        std::meta::enumerators_of(^^T));
    T result{};
    size_t i = 0;
    template for (constexpr auto e : enums) {
        if (i == index) result = [:e:];
        ++i;
    }
    return result;
}

#else

template <ScopedEnum T>
constexpr size_t enum_count() {
    return magic_enum::enum_count<T>();
}

template <ScopedEnum T>
std::string enum_label(size_t index) {
    auto values = magic_enum::enum_values<T>();
    return std::string(magic_enum::enum_name(values[index]));
}

template <ScopedEnum T>
constexpr size_t enum_index(T value) {
    return magic_enum::enum_index(value).value();
}

template <ScopedEnum T>
T enum_from_index(size_t index) {
    return magic_enum::enum_values<T>()[index];
}

#endif

// Sentinel: dropdown with optional custom labels
// A menu: a title that opens a popup list of items (see doc/design/menus-and-dialogs.md).
// `activations` bumps on every choice, so choosing the same item again still notifies.
struct Menu {
    std::string title;
    std::vector<std::string> items;
    std::optional<std::size_t> chosen{};
    std::uint64_t activations = 0;
    bool operator==(const Menu&) const = default;
};

struct MenuEditState {
    std::size_t highlighted = 0;
};

// A popup menu with no title, opened by a right click anywhere in the container it is
// declared in (vb.context_menu). It takes no layout space.
struct ContextMenu {
    std::vector<std::string> items;
    std::optional<std::size_t> chosen{};
    std::uint64_t activations = 0;
    bool operator==(const ContextMenu&) const = default;
};

// A modal dialog: a card centered over a dimmed window. The app opens it by setting `open`;
// a button (click, or Enter on the highlighted one) closes it with `chosen` set, Escape closes
// it with no choice. Either way `activations` counts up. It takes no layout space.
struct Dialog {
    std::string title;
    std::string message;
    std::vector<std::string> buttons;
    bool open = false;
    std::optional<std::size_t> chosen{};
    std::uint64_t activations = 0;
    bool operator==(const Dialog&) const = default;
};

// A popup that holds widgets (doc/design/popup-content.md): `vb.popup(field, builder)` places
// the builder's widgets in it. The app opens and closes it with `open`; Escape, or a press
// outside a non-modal one, closes it. A modal one is centred and blocks the window behind it.
struct Popup {
    bool open = false;
    bool modal = true;
    bool operator==(const Popup&) const = default;
};

struct DialogEditState {
    std::size_t highlighted = 0;
    float scroll = 0.f;   // of a message taller than the window allows
    std::optional<std::pair<float, float>> drag;   // the scrollbar's: pointer y and scroll at the press
};

template <ScopedEnum T>
struct Dropdown {
    T value{};
    std::vector<std::string> labels{};  // empty = use reflection
    bool operator==(const Dropdown&) const = default;
};

// Ephemeral state for open dropdown popup
struct DropdownEditState {
    bool open = false;
    size_t highlighted = 0;
    Rect popup_rect{Point{X{0}, Y{0}}, Size{Width{0}, Height{0}}};
};

// Sentinel: tab bar — selected index only; tab names defined in view()
template <typename S = void>
struct TabBar {
    size_t selected = 0;
    bool operator==(const TabBar&) const = default;
};

template <typename S>
    requires (!std::is_void_v<S>)
struct TabBar<S> {
    size_t selected = 0;
    S pages{};
    // Only compare selected — pages contains non-copyable sub-components
    bool operator==(const TabBar& other) const { return selected == other.selected; }
};

// Ephemeral state for tab bar hover tracking and header hit regions
struct TabBarEditState {
    std::optional<size_t> hovered_tab;
    std::vector<std::pair<X, X>> header_x_ranges;
};

// Type-erased ephemeral widget state stored in WidgetNode::edit_state.
// Any default-constructible type can be stored; get_or_create<S>() provides
// typed access with lazy initialization.
using EditState = std::any;

// WidgetNode is defined in widget_tree.hpp; declared here so delegate
// signatures can take WidgetNode& without creating a circular include.
// The accessor below lets delegate bodies reach visual_state without
// needing the complete type in this header.
struct WidgetNode;

// Declared here, defined in widget_node.hpp (after WidgetNode is complete).
const WidgetVisualState& node_vs(const WidgetNode& n);
Size node_allocated(const WidgetNode& n);
const Theme& node_theme(const WidgetNode& n);

namespace delegate_detail {
inline Rect make_rect(X x, Y y, Width w, Height h) {
    return {Point{x, y}, Size{w, h}};
}
inline Point make_point(X x, Y y) {
    return {x, y};
}
} // namespace delegate_detail

namespace delegate_detail {
inline constexpr Height default_widget_h{30.f};
inline constexpr Width default_widget_w{200.f};

inline Width widget_w(const WidgetNode& node) {
    auto sz = node_allocated(node);
    return sz.w.raw() > 0 ? sz.w : default_widget_w;
}
inline Height widget_h(const WidgetNode& node) {
    auto sz = node_allocated(node);
    return sz.h.raw() > 0 ? sz.h : default_widget_h;
}

// Shared surface/hover background fill used by the box-style delegates.
inline void fill_widget_bg(DrawList& dl, const WidgetNode& node, Height h) {
    auto& vs = node_vs(node);
    auto& t = node_theme(node);
    auto bg = vs.hovered ? t.surface_hover : t.surface;
    dl.filled_rect(make_rect(X{0}, Y{0}, widget_w(node), h), bg);
}

// Inset inside the widget's own box on purpose: Leaf measurement sizes widgets from
// draws.bounding_box(), so a ring painted outside the box feeds back into layout and
// grows the widget on every publish (see commit 9d0e78c).
inline void draw_focus_ring(DrawList& dl, Width w, Height h, const Theme& t) {
    dl.rect_outline(make_rect(X{1}, Y{1}, w - Width{2.f}, h - Height{2.f}), t.focus_ring, 2.0f);
}

// The framework's activation gesture: mouse press, or space/enter.
inline bool is_activation_event(const InputEvent& ev) {
    if (auto* mb = std::get_if<MouseButton>(&ev))
        return mb->pressed;
    if (auto* kp = std::get_if<KeyPress>(&ev))
        return (kp->key == keys::space || kp->key == keys::enter);
    return false;
}
} // namespace delegate_detail

// Primary template: default widget for any Field<T>.
// Renders a filled rect, ignores input.
template <typename T>
struct Widget {
    static constexpr FocusPolicy focus_policy = FocusPolicy::none;

    static void record(DrawList& dl, const Field<T>&, WidgetNode& node) {
        delegate_detail::fill_widget_bg(dl, node, delegate_detail::widget_h(node));
    }

    static void handle_input(Field<T>&, const InputEvent&, WidgetNode&) {}
};

/// True if Widget<T> has been specialized with record() and handle_input().
template <typename T>
concept is_widget_v = requires(DrawList& dl, const Field<T>& cf, Field<T>& f,
                                const InputEvent& ev, WidgetNode& node) {
    Widget<T>::record(dl, cf, node);
    Widget<T>::handle_input(f, ev, node);
};

// Numeric specialization: displays the value as text
template <Numeric T>
struct Widget<T> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::none;
    static float intrinsic_width(const Field<T>& f, const WidgetNode& n) {
        return 8.f + text_width(node_theme(n).measurer, std::to_string(f.get()), 14.f);
    }
    static constexpr Role access_role = Role::Label;
    static void describe(const Field<T>& f, AccessInfo& a) { a.value = std::to_string(f.get()); }

    static void record(DrawList& dl, const Field<T>& field, WidgetNode& node) {
        auto& t = node_theme(node);
        delegate_detail::fill_widget_bg(dl, node, delegate_detail::widget_h(node));
        dl.text(std::to_string(field.get()),
                delegate_detail::make_point(X{4}, Y{4}), 14, t.text);
    }

    static void handle_input(Field<T>&, const InputEvent&, WidgetNode&) {}
};

// StringLike specialization: displays the string value
template <StringLike T>
struct Widget<T> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::none;
    static float intrinsic_width(const Field<T>& f, const WidgetNode& n) {
        return 8.f + text_width(node_theme(n).measurer, std::string_view(f.get().data(), f.get().size()), 14.f);
    }
    static constexpr Role access_role = Role::Label;
    static void describe(const Field<T>& f, AccessInfo& a) { a.value = std::string(f.get().data(), f.get().size()); }

    static void record(DrawList& dl, const Field<T>& field, WidgetNode& node) {
        auto& t = node_theme(node);
        delegate_detail::fill_widget_bg(dl, node, delegate_detail::widget_h(node));
        dl.text(std::string(field.get().data(), field.get().size()),
                delegate_detail::make_point(X{4}, Y{4}), 14, t.text);
    }

    static void handle_input(Field<T>&, const InputEvent&, WidgetNode&) {}
};

// Sentinel: checkbox with label
struct Checkbox {
    bool checked = false;
    std::string label;
    bool operator==(const Checkbox&) const = default;
};

// Shared checkbox box rendering for Widget<bool> and Widget<Checkbox>
inline void draw_check_box(DrawList& dl, X x, Y y, bool checked,
                           const WidgetVisualState& vs, const Theme& t) {
    constexpr Width box_w{16.f};
    constexpr Height box_h{16.f};
    constexpr float border = 1.5f;

    if (checked) {
        auto fill = vs.pressed  ? t.accent_active
                  : vs.hovered  ? t.accent_hover
                  :               t.accent;
        dl.filled_rect(delegate_detail::make_rect(x, y, box_w, box_h), fill);
        dl.text("\xe2\x9c\x93", delegate_detail::make_point(x + DX{2.f}, y + DY{1.f}), 13, t.text_on_primary);
    } else {
        auto fill = vs.pressed  ? t.surface_active
                  : vs.hovered  ? t.surface_hover
                  :               t.surface;
        dl.filled_rect(delegate_detail::make_rect(x, y, box_w, box_h), fill);
    }
    dl.rect_outline(delegate_detail::make_rect(x, y, box_w, box_h),
                    vs.hovered ? t.border_hover : t.border,
                    border);
}

template <>
struct Widget<Checkbox> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::tab_and_click;
    static float intrinsic_width(const Field<Checkbox>& f, const WidgetNode& n) {
        return 32.f + text_width(node_theme(n).measurer, f.get().label, 14.f) + 8.f;   // box, gap, label, margin
    }
    static constexpr Role access_role = Role::CheckBox;
    static void describe(const Field<Checkbox>& f, AccessInfo& a) {
        a.name = f.get().label;
        a.checked = f.get().checked;
    }
    static constexpr Height widget_h{30.f};
    static constexpr Height box_size{16.f};

    static void record(DrawList& dl, const Field<Checkbox>& field, WidgetNode& node) {
        auto& vs = node_vs(node);
        auto& t = node_theme(node);
        auto& cb = field.get();

        delegate_detail::fill_widget_bg(dl, node, widget_h);

        Y box_y{(widget_h.raw() - box_size.raw()) / 2.f};
        draw_check_box(dl, X{8}, box_y, cb.checked, vs, t);

        if (!cb.label.empty())
            dl.text(cb.label, delegate_detail::make_point(X{32}, Y{7}), 14, t.text);

        if (vs.focused)
            delegate_detail::draw_focus_ring(dl, delegate_detail::widget_w(node), widget_h, t);
    }

    static void handle_input(Field<Checkbox>& field, const InputEvent& ev, WidgetNode&) {
        if (delegate_detail::is_activation_event(ev)) {
            auto cb = field.get();
            cb.checked = !cb.checked;
            field.set(cb);
        }
    }
};

// bool specialization: toggle widget
template <>
struct Widget<bool> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::tab_and_click;
    static constexpr Role access_role = Role::CheckBox;
    static void describe(const Field<bool>& f, AccessInfo& a) { a.checked = f.get(); }

    static void record(DrawList& dl, const Field<bool>& field, WidgetNode& node) {
        auto& vs = node_vs(node);
        auto& t = node_theme(node);
        constexpr Height widget_h{30.f};
        constexpr Height box_size{16.f};

        delegate_detail::fill_widget_bg(dl, node, widget_h);

        Y box_y{(widget_h.raw() - box_size.raw()) / 2.f};
        draw_check_box(dl, X{8}, box_y, field.get(), vs, t);

        if (vs.focused)
            delegate_detail::draw_focus_ring(dl, delegate_detail::widget_w(node), widget_h, t);
    }

    static void handle_input(Field<bool>& field, const InputEvent& ev, WidgetNode&) {
        if (delegate_detail::is_activation_event(ev))
            field.set(!field.get());
    }
};

template <StringLike T>
struct Widget<Label<T>> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::none;
    static float intrinsic_width(const Field<Label<T>>& f, const WidgetNode& n) {
        return 8.f + text_width(node_theme(n).measurer, std::string_view(f.get().value.data(), f.get().value.size()), 14.f);
    }
    static constexpr Role access_role = Role::Label;
    static void describe(const Field<Label<T>>& f, AccessInfo& a) {
        a.value = std::string(f.get().value.data(), f.get().value.size());
    }

    static void record(DrawList& dl, const Field<Label<T>>& field, WidgetNode& node) {
        auto& t = node_theme(node);
        Width w = delegate_detail::widget_w(node);
        dl.filled_rect(delegate_detail::make_rect(X{0}, Y{0}, w, Height{24}), t.surface);
        dl.text(std::string(field.get().value.data(), field.get().value.size()),
                delegate_detail::make_point(X{4}, Y{4}), 14, t.text_muted);
    }

    static void handle_input(Field<Label<T>>&, const InputEvent&, WidgetNode&) {}
};

enum class Orientation { Horizontal, Vertical };

// Sentinel: numeric slider with min/max/step bounds
template <Numeric T = double, Orientation O = Orientation::Horizontal>
struct Slider {
    T value{};
    T min = T{0};
    T max = T{1};
    T step = T{0};  // 0 = continuous
    bool operator==(const Slider&) const = default;
};

template <Numeric T, Orientation O>
struct Widget<Slider<T, O>> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::tab_and_click;
    static constexpr Role access_role = Role::Slider;
    static void describe(const Field<Slider<T, O>>& f, AccessInfo& a) {
        const auto& s = f.get();
        a.numeric_value = static_cast<double>(s.value);
        a.min_value = static_cast<double>(s.min);
        a.max_value = static_cast<double>(s.max);
        if (s.step != T{0}) a.step = static_cast<double>(s.step);
    }
    static constexpr bool vertical = (O == Orientation::Vertical);
    static constexpr bool expand = true;
    static constexpr ExpandAxis expand_axis = vertical ? ExpandAxis::Vertical : ExpandAxis::Horizontal;
    static constexpr float min_track_len = 200.f;
    static constexpr float track_thick = 6.f;
    static constexpr float thumb_len = 12.f;
    static constexpr float widget_extent = 30.f;

    static float track_len(const WidgetNode& node) {
        auto sz = node_allocated(node);
        float allocated = vertical ? sz.h.raw() : sz.w.raw();
        return allocated > 0 ? allocated : min_track_len;
    }

    static float ratio(const Slider<T, O>& s) {
        if (s.max == s.min) return 0.f;
        return static_cast<float>(s.value - s.min) / static_cast<float>(s.max - s.min);
    }

    static void record(DrawList& dl, const Field<Slider<T, O>>& field, WidgetNode& node) {
        auto& vs = node_vs(node);
        auto& t = node_theme(node);
        auto& s = field.get();
        float r = ratio(s);
        float tl = track_len(node);

        auto track_bg = vs.hovered ? t.track_hover : t.track;
        auto thumb_color = vs.pressed ? t.accent_active
                         : vs.hovered ? t.accent_hover
                         : t.accent;

        if constexpr (vertical) {
            X track_x{(widget_extent - track_thick) / 2.f};
            dl.filled_rect(delegate_detail::make_rect(track_x, Y{0}, Width{track_thick}, Height{tl}), track_bg);
            Y thumb_y{(1.f - r) * (tl - thumb_len)};
            dl.filled_rect(delegate_detail::make_rect(X{0}, thumb_y, Width{widget_extent}, Height{thumb_len}), thumb_color);
            if (vs.focused)
                delegate_detail::draw_focus_ring(dl, Width{widget_extent}, Height{tl}, t);
        } else {
            Y track_y{(widget_extent - track_thick) / 2.f};
            dl.filled_rect(delegate_detail::make_rect(X{0}, track_y, Width{tl}, Height{track_thick}), track_bg);
            X thumb_x{r * (tl - thumb_len)};
            dl.filled_rect(delegate_detail::make_rect(thumb_x, Y{0}, Width{thumb_len}, Height{widget_extent}), thumb_color);
            if (vs.focused)
                delegate_detail::draw_focus_ring(dl, Width{tl}, Height{widget_extent}, t);
        }
    }

    static void apply_position(Field<Slider<T, O>>& field, float pos, const WidgetNode& node) {
        float tl = track_len(node);
        float t = vertical ? std::clamp(1.f - pos / tl, 0.f, 1.f)
                           : std::clamp(pos / tl, 0.f, 1.f);
        auto& s = field.get();
        T raw = static_cast<T>(s.min + t * (s.max - s.min));
        Slider<T, O> updated = s;
        if (s.step != T{0}) {
            T steps = static_cast<T>((raw - s.min + s.step / T{2}) / s.step);
            updated.value = std::clamp(static_cast<T>(s.min + steps * s.step), s.min, s.max);
        } else {
            updated.value = raw;
        }
        field.set(updated);
    }

    static void handle_input(Field<Slider<T, O>>& field, const InputEvent& ev, WidgetNode& node) {
        auto extract = [](auto& e) { return vertical ? e.position.y.raw() : e.position.x.raw(); };
        if (auto* mb = std::get_if<MouseButton>(&ev); mb && mb->pressed)
            apply_position(field, extract(*mb), node);
        if (auto* mm = std::get_if<MouseMove>(&ev); mm && node_vs(node).pressed)
            apply_position(field, extract(*mm), node);
    }
};

// Sentinel: clickable button with text label
struct Button {
    std::string text;
    uint64_t click_count = 0;
    bool operator==(const Button&) const = default;
};

template <>
struct Widget<Button> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::tab_and_click;
    static float intrinsic_width(const Field<Button>& f, const WidgetNode& n) {
        return std::max(48.f, 16.f + text_width(node_theme(n).measurer, f.get().text, 14.f));   // text at x = 8, both sides
    }
    static constexpr Role access_role = Role::Button;
    static void describe(const Field<Button>& f, AccessInfo& a) { a.name = f.get().text; }

    static void record(DrawList& dl, const Field<Button>& field, WidgetNode& node) {
        auto& vs = node_vs(node);
        auto& t = node_theme(node);
        Width w = delegate_detail::widget_w(node);
        Color bg = vs.pressed ? t.primary_active
                 : vs.hovered ? t.primary_hover
                 : t.primary;
        dl.filled_rect(delegate_detail::make_rect(X{0}, Y{0}, w, Height{32}), bg);
        dl.rect_outline(delegate_detail::make_rect(X{0}, Y{0}, w, Height{32}), t.primary_outline, 1.0f);
        dl.text(field.get().text, delegate_detail::make_point(X{8}, Y{7}), 14, t.text_on_primary);
        if (vs.focused)
            delegate_detail::draw_focus_ring(dl, w, Height{32}, t);
    }

    static void handle_input(Field<Button>& field, const InputEvent& ev, WidgetNode&) {
        if (delegate_detail::is_activation_event(ev)) {
            auto btn = field.get();
            btn.click_count++;
            field.set(btn);
        }
    }
};

template <StringLike T>
struct Widget<TextField<T>> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::tab_and_click;
    static constexpr bool expand = true;   // fills its row
    static constexpr ExpandAxis expand_axis = ExpandAxis::Horizontal;
    static constexpr Role access_role = Role::TextInput;
    static void describe(const Field<TextField<T>>& f, AccessInfo& a) {
        a.value = std::string(f.get().value.data(), f.get().value.size());
    }
    static constexpr Height widget_h{30.f};
    // padding is used in both a horizontal (inset from left edge) and
    // vertical (inset from top edge) role in text_delegates.hpp — split
    // rather than force one Scalar<Tag> to serve both axes.
    static constexpr Width padding_x{4.f};
    static constexpr Height padding_y{4.f};
    static constexpr float font_size = 14.f;
    static constexpr Width cursor_w{2.f};

    // Defined in widget_tree.hpp (after WidgetNode is complete).
    static const TextEditState& get_edit_state(const WidgetNode& node);
    static TextEditState& ensure_edit_state(WidgetNode& node);
    static void record(DrawList& dl, const Field<TextField<T>>& field, WidgetNode& node);
    static void handle_input(Field<TextField<T>>& field, const InputEvent& ev, WidgetNode& node);
};

// A text field with suggestions: typing filters `options` (case-insensitive), Down/Up move
// through them, Enter or a click takes one, Escape closes the list. The value is free text: it
// need not be one of the options (an editable combo box, as QComboBox::setEditable).
struct ComboBox {
    std::string value{};
    std::string placeholder{};
    size_t max_length = 0;
    std::vector<std::string> options{};
    bool operator==(const ComboBox&) const = default;
};

template <>
struct Widget<ComboBox> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::tab_and_click;
    static constexpr bool expand = true;
    static constexpr ExpandAxis expand_axis = ExpandAxis::Horizontal;
    static constexpr Role access_role = Role::ComboBox;
    static void describe(const Field<ComboBox>& f, AccessInfo& a) { a.value = f.get().value; }
    static void record(DrawList& dl, const Field<ComboBox>& field, WidgetNode& node);
    static void handle_input(Field<ComboBox>& field, const InputEvent& ev, WidgetNode& node);
};

template <StringLike T>
struct Widget<Password<T>> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::tab_and_click;
    static constexpr bool expand = true;
    static constexpr ExpandAxis expand_axis = ExpandAxis::Horizontal;
    static constexpr Role access_role = Role::PasswordInput;   // value deliberately never set
    static constexpr Height widget_h{30.f};
    static constexpr Width padding_x{4.f};
    static constexpr Height padding_y{4.f};
    static constexpr float font_size = 14.f;
    static constexpr Width cursor_w{2.f};

    static const TextEditState& get_edit_state(const WidgetNode& node);
    static TextEditState& ensure_edit_state(WidgetNode& node);
    static void record(DrawList& dl, const Field<Password<T>>& field, WidgetNode& node);
    static void handle_input(Field<Password<T>>& field, const InputEvent& ev, WidgetNode& node);
};

template <StringLike T>
struct Widget<TextArea<T>> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::tab_and_click;
    static constexpr bool expand = true;
    static constexpr ExpandAxis expand_axis = ExpandAxis::Horizontal;
    static constexpr Role access_role = Role::MultilineTextInput;
    static void describe(const Field<TextArea<T>>& f, AccessInfo& a) {
        a.value = std::string(f.get().value.data(), f.get().value.size());
    }
    static constexpr Width padding_x{4.f};
    static constexpr Height padding_y{4.f};
    static constexpr float font_size = 14.f;
    static constexpr Height line_height{font_size * 1.4f};
    static constexpr Width cursor_w{2.f};

    static const TextAreaEditState& get_edit_state(const WidgetNode& node);
    static TextAreaEditState& ensure_edit_state(WidgetNode& node);
    static void record(DrawList& dl, const Field<TextArea<T>>& field, WidgetNode& node);
    static void handle_input(Field<TextArea<T>>& field, const InputEvent& ev, WidgetNode& node);
};

// ScopedEnum delegate -- declared here, defined in widget_tree.hpp
template <ScopedEnum T>
    requires (!TextEditable<T>)
struct Widget<T> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::tab_and_click;
    static constexpr Role access_role = Role::ComboBox;

    static void record(DrawList& dl, const Field<T>& field, WidgetNode& node);
    static void handle_input(Field<T>& field, const InputEvent& ev, WidgetNode& node);
};

template <ScopedEnum T>
struct Widget<Dropdown<T>> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::tab_and_click;
    static constexpr Role access_role = Role::ComboBox;

    static void record(DrawList& dl, const Field<Dropdown<T>>& field, WidgetNode& node);
    static void handle_input(Field<Dropdown<T>>& field, const InputEvent& ev, WidgetNode& node);
};

template <>
struct Widget<Menu> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::tab_and_click;
    static constexpr Role access_role = Role::Button;   // AccessKit's Menu roles come with the bridge
    static constexpr bool opens_menu = true;   // neighbouring menus form a menu bar
    static void describe(const Field<Menu>& f, AccessInfo& a) { a.name = menu_detail::parse_mnemonic(f.get().title).text; }
    static std::int32_t mnemonic_key(const Field<Menu>& f) { return menu_detail::mnemonic_key(f.get().title); }
    static float intrinsic_width(const Field<Menu>& f, const WidgetNode& n);
    static void record(DrawList& dl, const Field<Menu>& field, WidgetNode& node);
    static void handle_input(Field<Menu>& field, const InputEvent& ev, WidgetNode& node);
};

template <>
struct Widget<ContextMenu> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::none;
    static constexpr Role access_role = Role::Button;
    static constexpr bool context_menu = true;
    static constexpr bool popup_anchor = true;
    static void record(DrawList& dl, const Field<ContextMenu>& field, WidgetNode& node);
    static void handle_input(Field<ContextMenu>& field, const InputEvent& ev, WidgetNode& node);
};

template <>
struct Widget<Popup> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::none;
    static constexpr Role access_role = Role::Dialog;
    static constexpr bool popup_anchor = true;
    static void record(DrawList& dl, const Field<Popup>& field, WidgetNode& node);
    static void handle_input(Field<Popup>& field, const InputEvent& ev, WidgetNode& node);
};

template <>
struct Widget<Dialog> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::none;
    static constexpr Role access_role = Role::Dialog;
    static constexpr bool popup_anchor = true;
    static void describe(const Field<Dialog>& f, AccessInfo& a) { a.name = f.get().title; }
    static void record(DrawList& dl, const Field<Dialog>& field, WidgetNode& node);
    static void handle_input(Field<Dialog>& field, const InputEvent& ev, WidgetNode& node);
};

template <>
struct Widget<TabBar<>> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::tab_and_click;
    static constexpr Role access_role = Role::TabList;

    static void record(DrawList& dl, const Field<TabBar<>>& field, WidgetNode& node);
    static void handle_input(Field<TabBar<>>& field, const InputEvent& ev, WidgetNode& node);
};

} // namespace prism::ui
