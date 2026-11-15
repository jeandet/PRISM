#pragma once

#if __cpp_impl_reflection
#include <prism/core/reflect.hpp>
#include <prism/core/reflect_annotations.hpp>
#include <prism/core/traits.hpp>

#include <cstdio>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

// What WidgetTree learns from a model's members by reflection when the model places its fields
// itself (a view() method): their names, and a warning for any it left out.
namespace prism::app::widget_detail {
using namespace prism::core;

// view()-driven placement (vb.widget(field)) only ever sees a Field<T>&, never the
// member identifier that placed it -- unlike WidgetTree::build_node_tree's whole-model
// reflection branch, which walks Model's own members and knows both. Recover the identifier here, keyed
// by address, so ViewBuilder::apply_field_name can resolve it for whichever field
// view() happens to place.
template <typename Model>
void record_field_names(Model& model, std::unordered_map<const void*, std::string_view>& field_names,
                        std::unordered_map<const void*, std::string>& field_tooltips) {
    static constexpr auto own_members = std::define_static_array(
        std::meta::nonstatic_data_members_of(
            ^^Model, std::meta::access_context::unchecked()));
    template for (constexpr auto m : own_members) {
        auto& member = model.[:m:];
        using M = std::remove_cvref_t<decltype(member)>;
        if constexpr (is_field_v<M> || is_derived_v<M> || is_shared_v<M>) {
            field_names.emplace(static_cast<const void*>(&member),
                                std::meta::identifier_of(m));
            if constexpr (constexpr auto tip = core::extract_string_annotation<m, core::tooltip_t>(); !tip.empty())
                field_tooltips.emplace(static_cast<const void*>(&member), tip);
        }
    }
}

// The members that could have been a widget and that view() never placed: a forgotten widget.
// Not a reference (the model that owns it answers for it), not a Shared (plumbing between
// threads, read through a Field or a Derived, never shown as itself), and not one the block
// marked [[=core::skip]]: a value it reads, never a widget.
template <typename Model>
std::vector<std::string_view> unplaced_fields(Model& model, const std::set<const void*>& placed) {
    static constexpr auto members = std::define_static_array(
        std::meta::nonstatic_data_members_of(
            ^^Model, std::meta::access_context::unchecked()));
    std::vector<std::string_view> names;
    template for (constexpr auto m : members) {
        if constexpr (!std::meta::is_reference_type(std::meta::type_of(m)) &&
                      !has_annotation<m, decltype(skip)>()) {
            auto& member = model.[:m:];
            using M = std::remove_cvref_t<decltype(member)>;
            if constexpr (is_field_v<M> || is_derived_v<M>)
                if (!placed.contains(&member)) names.push_back(std::meta::identifier_of(m));
        }
    }
    return names;
}

template <typename Model>
void check_unplaced_fields([[maybe_unused]] Model& model,
                           [[maybe_unused]] const std::set<const void*>& placed) {
#ifndef NDEBUG
    // Model can be a class-template specialization reached through a deduced function-
    // template parameter (e.g. LeafSlot<M, ReadOnly>) -- on this reflection implementation
    // such a ^^Model reports has_identifier() == false, and identifier_of() would throw as
    // a consteval call. Fall back to a placeholder rather than crash the build; this is a
    // debug-only diagnostic string, not something callers parse. See the analogous
    // Shared<T>& -> Shared<T>* workaround in inspector.hpp for the same underlying bug.
    static constexpr std::string_view model_name = std::meta::has_identifier(^^Model)
        ? std::meta::identifier_of(^^Model) : std::string_view{"<anonymous>"};
    for (std::string_view name : unplaced_fields(model, placed))
        std::fprintf(stderr, "[prism] warning: Field '%.*s' in %.*s not placed by view()\n",
                     static_cast<int>(name.size()), name.data(),
                     static_cast<int>(model_name.size()), model_name.data());
#endif
}

} // namespace prism::app::widget_detail
#endif // __cpp_impl_reflection
