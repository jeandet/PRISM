#pragma once

#include <prism/core/fixed_string.hpp>

#if __cpp_impl_reflection
#include <meta>

namespace prism::core {

// --- Annotations ---------------------------------------------------------
// Attach to a member of a plain reflected struct:
//
//   struct Settings {
//       [[=prism::core::skip]]                  int internal_version;
//       [[=prism::core::readonly]]              std::string device_id;
//       [[=prism::core::label<"Sample Rate">]]  int sample_rate;
//       [[=prism::core::section<"Audio">]]      float volume;
//       [[=prism::core::tooltip<"In dB">]]      Field<Slider<>> gain;   // model_app views
//       [[=prism::core::range<0.0, 30.0, 0.1>, =prism::core::unit<"V">]] double volts;   // a slider
//   };
//
// prism::inspector still exposes these under their original names
// (prism::inspector::skip, etc.) via the `using namespace prism::core;`
// already present in field_mirror.hpp -- no call site there changes.

constexpr inline struct {} skip{};
constexpr inline struct {} readonly{};

template <fixed_string S> struct label_t   { static constexpr auto value = S; };
template <fixed_string S> struct section_t { static constexpr auto value = S; };
template <fixed_string S> struct tooltip_t { static constexpr auto value = S; };
template <fixed_string S> struct unit_t    { static constexpr auto value = S; };
template <fixed_string S> constexpr inline label_t<S>   label{};
template <fixed_string S> constexpr inline section_t<S> section{};
template <fixed_string S> constexpr inline tooltip_t<S> tooltip{};
template <fixed_string S> constexpr inline unit_t<S>    unit{};

// A number's bounds, for a widget that needs them (the inspector shows it as a slider).
// step 0: continuous.
template <auto Min, auto Max, auto Step = 0>
struct range_t {
    static constexpr auto min = Min;
    static constexpr auto max = Max;
    static constexpr auto step = Step;
};
template <auto Min, auto Max, auto Step = 0> constexpr inline range_t<Min, Max, Step> range{};

template <std::meta::info M, typename Tag>
consteval bool has_annotation() {
    static constexpr auto annots = std::define_static_array(std::meta::annotations_of(M));
    for (auto a : annots) {
        if (std::meta::type_of(a) == ^^Tag) return true;
    }
    return false;
}

template <template <fixed_string> class Templ>
consteval bool is_specialization_of(std::meta::info t) {
    return std::meta::has_template_arguments(t) && std::meta::template_of(t) == ^^Templ;
}

template <std::meta::info M, template <fixed_string> class Templ>
consteval std::string_view extract_string_annotation() {
    static constexpr auto annots = std::define_static_array(std::meta::annotations_of(M));
    template for (constexpr auto a : annots) {
        constexpr auto t = std::meta::type_of(a);
        if constexpr (is_specialization_of<Templ>(t)) {
            return [:t:]::value.view();
        }
    }
    return {};
}

// The caption a member shows: its label annotation, or its own name.
template <std::meta::info M>
consteval std::string_view caption_of() {
    constexpr auto label = extract_string_annotation<M, label_t>();
    return label.empty() ? std::meta::identifier_of(M) : label;
}

// The member's range_t<...> type, or a null reflection when it has none.
template <std::meta::info M>
consteval std::meta::info range_annotation() {
    for (auto a : std::meta::annotations_of(M)) {
        auto t = std::meta::remove_cv(std::meta::type_of(a));
        if (std::meta::has_template_arguments(t) && std::meta::template_of(t) == ^^range_t) return t;
    }
    return {};
}

} // namespace prism::core

#endif // __cpp_impl_reflection
