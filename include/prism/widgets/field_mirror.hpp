#pragma once

#include <prism/app/widget_tree.hpp>
#include <prism/core/field.hpp>
#include <prism/core/fixed_string.hpp>
#include <prism/core/reflect_annotations.hpp>
#include <prism/ui/delegate.hpp>

#include <array>
#include <tuple>
#include <type_traits>
#include <vector>

#if __cpp_impl_reflection
#include <meta>
#include <ranges>

namespace prism::inspector {
using namespace prism::core;
using namespace prism::ui;

// skip/readonly/label/section/has_annotation/extract_string_annotation now live in
// prism::core::reflect_annotations.hpp; the `using namespace prism::core;` above is
// what still makes prism::inspector::skip etc. resolve for every existing call site.

// --- Slot shapes -----------------------------------------------------------

template <typename T>
concept MirrorLeaf = Numeric<T> || StringLike<T> || ScopedEnum<T>;

// One labeled row: a static name caption + the live value. ReadOnly=true
// renders the value without focus/input wiring ([[=prism::inspector::readonly]]).
template <typename M, bool ReadOnly = false>
struct LeafSlot {
    Field<Label<std::string>> name{};
    Field<M> value{};

    void view(prism::app::WidgetTree::ViewBuilder& vb) {
        vb.widget(name);
        if constexpr (ReadOnly) vb.widget_readonly(value);
        else vb.widget(value);
    }
};

// A number with a range annotation: one slider, captioned with the member's name.
template <typename M, typename Range>
struct RangeSlot {
    Field<Slider<M>> value{{.min = static_cast<M>(Range::min), .max = static_cast<M>(Range::max),
                            .step = static_cast<M>(Range::step)}};

    void sync(M v, std::string_view caption, std::string_view unit) {
        auto s = value.get();
        s.value = v;
        s.label = caption;
        s.unit = unit;
        value.set(std::move(s));
    }
    [[nodiscard]] M get() const { return value.get().value; }
    void view(prism::app::WidgetTree::ViewBuilder& vb) { vb.widget(value); }
};

template <typename T> struct is_range_slot : std::false_type {};
template <typename M, typename R> struct is_range_slot<RangeSlot<M, R>> : std::true_type {};
template <typename T> inline constexpr bool is_range_slot_v = is_range_slot<T>::value;

// A member excluded from rendering ([[=prism::inspector::skip]]). Still
// round-trips through sync_from/build so unrelated edits don't reset it.
template <typename M>
struct HiddenSlot {
    Field<M> value{};
};

template <typename T> struct is_hidden_slot : std::false_type {};
template <typename M> struct is_hidden_slot<HiddenSlot<M>> : std::true_type {};
template <typename T> inline constexpr bool is_hidden_slot_v = is_hidden_slot<T>::value;

template <typename T>
concept NestedMirrorSlot = requires(T& t) { t.slots; };

template <typename T> struct FieldMirror;

// The caption a leaf shows: its label annotation, or the member's own name.
template <std::meta::info M>
consteval std::string_view caption_of() {
    constexpr auto label = extract_string_annotation<M, label_t>();
    return label.empty() ? std::meta::identifier_of(M) : label;
}

template <typename T>
consteval std::size_t field_mirror_member_count() {
    return std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()).size();
}

template <typename T>
consteval std::meta::info field_mirror_tuple_info() {
    std::vector<std::meta::info> slot_types;
    static constexpr auto members = std::define_static_array(
        std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()));
    template for (constexpr auto m : members) {
        constexpr auto mtype = std::meta::type_of(m);
        using M = typename [:mtype:];
        if constexpr (has_annotation<m, decltype(skip)>()) {
            slot_types.push_back(std::meta::substitute(^^HiddenSlot, {mtype}));
        } else if constexpr (constexpr auto r = range_annotation<m>(); r != std::meta::info{}) {
            static_assert(Numeric<M>, "[[=prism::core::range<...>]] is only supported on numbers");
            static_assert(!has_annotation<m, decltype(readonly)>(),
                "[[=prism::core::range<...>]] makes an editable slider: drop readonly, or the range");
            slot_types.push_back(std::meta::substitute(^^RangeSlot, {mtype, r}));
        } else if constexpr (MirrorLeaf<M>) {
            constexpr bool ro = has_annotation<m, decltype(readonly)>();
            slot_types.push_back(std::meta::substitute(
                ^^LeafSlot, {mtype, std::meta::reflect_constant(ro)}));
        } else {
            static_assert(!has_annotation<m, decltype(readonly)>(),
                "[[=prism::inspector::readonly]] is only supported on leaf members "
                "(numeric, string-like, or scoped enum) -- not on nested structs");
            slot_types.push_back(std::meta::substitute(^^FieldMirror, {mtype}));
        }
    }
    return std::meta::substitute(^^std::tuple, slot_types);
}

template <typename T>
using FieldMirrorTuple = [: field_mirror_tuple_info<T>() :];

template <typename T>
struct FieldMirror {
    FieldMirrorTuple<T> slots;
    std::array<Field<Label<std::string>>, field_mirror_member_count<T>()> section_headers{};

    FieldMirror() {
        static constexpr auto members = std::define_static_array(
            std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()));
        std::size_t idx = 0;
        template for (constexpr auto m : members) {
            constexpr auto title = extract_string_annotation<m, section_t>();
            if constexpr (!title.empty()) {
                section_headers[idx].set(Label<std::string>{std::string(title)});
            }
            ++idx;
        }
    }

    void sync_from(const T& v) {
        static constexpr auto members = std::define_static_array(
            std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()));
        template for (constexpr auto i : std::views::iota(std::size_t{0}, members.size())) {
            constexpr auto m = members[i];
            auto& slot = std::get<i>(slots);
            using SlotT = std::remove_cvref_t<decltype(slot)>;
            if constexpr (NestedMirrorSlot<SlotT>) {
                // label/readonly are no-ops here: a nested FieldMirror<M> has no caption
                // field of its own to override and no single leaf widget to make read-only.
                slot.sync_from(v.[:m:]);
            } else if constexpr (is_hidden_slot_v<SlotT>) {
                slot.value.set(v.[:m:]);
            } else if constexpr (is_range_slot_v<SlotT>) {
                slot.sync(v.[:m:], caption_of<m>(), extract_string_annotation<m, unit_t>());
            } else {
                slot.name.set(Label<std::string>{std::string(caption_of<m>())});
                slot.value.set(v.[:m:]);
            }
        }
    }

    T build() const {
        T out{};
        static constexpr auto members = std::define_static_array(
            std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()));
        template for (constexpr auto i : std::views::iota(std::size_t{0}, members.size())) {
            constexpr auto m = members[i];
            const auto& slot = std::get<i>(slots);
            using SlotT = std::remove_cvref_t<decltype(slot)>;
            if constexpr (NestedMirrorSlot<SlotT>) {
                out.[:m:] = slot.build();
            } else if constexpr (is_range_slot_v<SlotT>) {
                out.[:m:] = slot.get();
            } else {
                out.[:m:] = slot.value.get();
            }
        }
        return out;
    }

    template <typename Fn>
    void for_each_leaf(Fn&& fn) {
        static constexpr auto members = std::define_static_array(
            std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()));
        template for (constexpr auto i : std::views::iota(std::size_t{0}, members.size())) {
            auto& slot = std::get<i>(slots);
            using SlotT = std::remove_cvref_t<decltype(slot)>;
            if constexpr (NestedMirrorSlot<SlotT>) {
                slot.for_each_leaf(fn);
            } else if constexpr (is_hidden_slot_v<SlotT>) {
                // invisible -- no widget exists for it, so it can never receive a local edit.
            } else {
                fn(slot.value);
            }
        }
    }

    void view(prism::app::WidgetTree::ViewBuilder& vb) {
        static constexpr auto members = std::define_static_array(
            std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()));
        template for (constexpr auto i : std::views::iota(std::size_t{0}, members.size())) {
            constexpr auto m = members[i];
            using SlotT = std::remove_cvref_t<decltype(std::get<i>(slots))>;
            if constexpr (!is_hidden_slot_v<SlotT>) {
                constexpr auto title = extract_string_annotation<m, section_t>();
                if constexpr (!title.empty()) {
                    vb.widget(section_headers[i]);
                }
                vb.block(std::get<i>(slots));
            }
        }
    }
};

} // namespace prism::inspector

#endif // __cpp_impl_reflection
