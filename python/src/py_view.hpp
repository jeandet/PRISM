#pragma once
// Per-handle view dispatch: field/shared pointers, enabled_if, tooltips, widget and list dispatch.

#include "common.hpp"

// helper to attach a single dep to a derived slot
template <typename U> struct is_std_shared_ptr : std::false_type {};
template <typename U> struct is_std_shared_ptr<std::shared_ptr<U>> : std::true_type {};

template <typename FH>
auto* field_ptr_of(FH& h) {
    using M = std::decay_t<decltype(h.field)>;
    if constexpr (std::is_pointer_v<M>) return h.field;
    else if constexpr (is_std_shared_ptr<M>::value) return h.field.get();
    else return &h.field;
}

template <typename SH>
auto* shared_ptr_of(SH& h) {
    using M = std::decay_t<decltype(h.shared)>;
    if constexpr (std::is_pointer_v<M>) return h.shared;
    else if constexpr (is_std_shared_ptr<M>::value) return h.shared.get();
    else return &h.shared;
}
template <typename T>
void derived_attach_dep(std::shared_ptr<SlotDerived<T>> slot, nb::object dep) {
    std::weak_ptr<SlotDerived<T>> weak = slot;
    auto connect_field = [&](auto* example) -> bool {
        using FH = std::decay_t<decltype(*example)>;
        if (!nb::isinstance<FH>(dep)) return false;
        auto& h = nb::cast<FH&>(dep);
        auto* ptr = field_ptr_of(h);
        if (!ptr) return true;
        slot->deps_.push_back(ptr->on_change().connect([weak](const auto&){ if (auto s = weak.lock()) s->recompute(); }));
        if constexpr (requires { h.owner; }) { if (h.owner) slot->dep_owners_.push_back(h.owner); }
        slot->dep_keepalive_.push_back(dep);
        return true;
    };
    auto connect_shared = [&](auto* example) -> bool {
        using SH = std::decay_t<decltype(*example)>;
        if (!nb::isinstance<SH>(dep)) return false;
        auto& h = nb::cast<SH&>(dep);
        auto* ptr = shared_ptr_of(h);
        if (!ptr) return true;
        slot->deps_.push_back(ptr->on_change().connect([weak](const auto&){ if (auto s = weak.lock()) s->recompute(); }));
        if constexpr (requires { h.owner; }) { if (h.owner) slot->dep_owners_.push_back(h.owner); }
        slot->dep_keepalive_.push_back(dep);
        return true;
    };
    auto connect_derived = [&](auto* example) -> bool {
        using DH = std::decay_t<decltype(*example)>;
        if (!nb::isinstance<DH>(dep)) return false;
        auto& h = nb::cast<DH&>(dep);
        auto* ptr = h.derived;
        if (!ptr) return true;
        slot->deps_.push_back(ptr->on_change().connect([weak](const auto&){ if (auto s = weak.lock()) s->recompute(); }));
        if (h.owner) slot->dep_owners_.push_back(h.owner);
        slot->dep_keepalive_.push_back(dep);
        return true;
    };
    // BoundSliderValue holds field_h XOR field_v (see its own comment) instead
    // of one `h.field` member field_ptr_of() can read generically, so it gets
    // its own connect — otherwise identical to connect_field: only h.owner and
    // whichever field's on_change() are needed to trigger recompute() on
    // change, never the field's value type (the derived compute fn reads the
    // dep's actual value itself, e.g. self.v.value).
    auto connect_slider = [&]() -> bool {
        if (!nb::isinstance<BoundSliderValue>(dep)) return false;
        auto& h = nb::cast<BoundSliderValue&>(dep);
        auto connect_one = [&](auto* ptr) {
            if (!ptr) return;
            slot->deps_.push_back(ptr->on_change().connect([weak](const auto&){ if (auto s = weak.lock()) s->recompute(); }));
        };
        connect_one(h.field_h);
        connect_one(h.field_v);
        if (h.owner) slot->dep_owners_.push_back(h.owner);
        slot->dep_keepalive_.push_back(dep);
        return true;
    };
    // Bound handles
    if (connect_field((BoundField<int>*)nullptr)) return;
    if (connect_field((BoundField<double>*)nullptr)) return;
    if (connect_field((BoundField<std::string>*)nullptr)) return;
    if (connect_field((BoundField<bool>*)nullptr)) return;
    if (connect_slider()) return;
    if (connect_field((BoundCheckboxValue*)nullptr)) return;
    if (connect_field((BoundRadio*)nullptr)) return;
    if (connect_field((BoundChoice*)nullptr)) return;
    if (connect_field((BoundPicture*)nullptr)) return;
    if (connect_field((BoundLink*)nullptr)) return;
    if (connect_field((BoundColor*)nullptr)) return;
    if (connect_field((BoundDate*)nullptr)) return;
    if (connect_field((BoundTime*)nullptr)) return;
    if (connect_field((BoundExpander*)nullptr)) return;
    if (connect_field((BoundSpinBoxInt*)nullptr)) return;
    if (connect_field((BoundSpinBoxFloat*)nullptr)) return;
    if (connect_field((BoundProgress*)nullptr)) return;
    if (connect_field((BoundButton*)nullptr)) return;
    if (connect_field((BoundComboBox*)nullptr)) return;
    if (connect_field((BoundPopup*)nullptr)) return;
    if (connect_shared((BoundShared<int>*)nullptr)) return;
    if (connect_shared((BoundShared<double>*)nullptr)) return;
    if (connect_shared((BoundShared<std::string>*)nullptr)) return;
    if (connect_shared((BoundShared<bool>*)nullptr)) return;
    if (connect_derived((BoundDerived<int>*)nullptr)) return;
    if (connect_derived((BoundDerived<double>*)nullptr)) return;
    if (connect_derived((BoundDerived<std::string>*)nullptr)) return;
    if (connect_derived((BoundDerived<bool>*)nullptr)) return;
    // Standalone handles
    if (connect_field((FieldHandle<int>*)nullptr)) return;
    if (connect_field((FieldHandle<double>*)nullptr)) return;
    if (connect_field((FieldHandle<std::string>*)nullptr)) return;
    if (connect_field((FieldHandle<bool>*)nullptr)) return;
    if (connect_shared((SharedHandle<int>*)nullptr)) return;
    if (connect_shared((SharedHandle<double>*)nullptr)) return;
    if (connect_shared((SharedHandle<std::string>*)nullptr)) return;
    if (connect_shared((SharedHandle<bool>*)nullptr)) return;
    throw nb::type_error("derived: unsupported dependency handle type");
}

// Places what `place` adds, enabled while `flag` holds: a bool field, a derived bool, or a
// checkbox (ViewBuilder::enabled_if).
template <typename Place>
void py_enabled_if(ViewBuilder& vb, nb::object flag, Place&& place) {
    if (nb::isinstance<BoundField<bool>>(flag)) vb.enabled_if(*nb::cast<BoundField<bool>&>(flag).field, place);
    else if (nb::isinstance<FieldHandle<bool>>(flag)) vb.enabled_if(*nb::cast<FieldHandle<bool>&>(flag).field, place);
    else if (nb::isinstance<BoundDerived<bool>>(flag)) vb.enabled_if(*nb::cast<BoundDerived<bool>&>(flag).derived, place);
    else if (nb::isinstance<BoundCheckboxValue>(flag)) vb.enabled_if(*nb::cast<BoundCheckboxValue&>(flag).field, place);
    else throw nb::type_error("enabled: the flag must be a bool field, a derived bool or a checkbox");
}

inline void py_widget_dispatch(ViewBuilder& vb, nb::object h) {
    if (nb::isinstance<BoundField<int>>(h)) vb.widget(*nb::cast<BoundField<int>&>(h).field);
    else if (nb::isinstance<BoundField<double>>(h)) vb.widget(*nb::cast<BoundField<double>&>(h).field);
    else if (nb::isinstance<BoundField<std::string>>(h)) vb.widget(*nb::cast<BoundField<std::string>&>(h).field);
    else if (nb::isinstance<BoundField<bool>>(h)) vb.widget(*nb::cast<BoundField<bool>&>(h).field);
    else if (nb::isinstance<BoundShared<int>>(h)) vb.widget(*nb::cast<BoundShared<int>&>(h).shared);
    else if (nb::isinstance<BoundShared<double>>(h)) vb.widget(*nb::cast<BoundShared<double>&>(h).shared);
    else if (nb::isinstance<BoundShared<std::string>>(h)) vb.widget(*nb::cast<BoundShared<std::string>&>(h).shared);
    else if (nb::isinstance<BoundShared<bool>>(h)) vb.widget(*nb::cast<BoundShared<bool>&>(h).shared);
    else if (nb::isinstance<BoundSliderValue>(h)) {
        auto& b = nb::cast<BoundSliderValue&>(h);
        if (b.field_h) vb.widget(*b.field_h);
        else if (b.field_v) vb.widget(*b.field_v);
    }
    else if (nb::isinstance<BoundCheckboxValue>(h)) vb.widget(*nb::cast<BoundCheckboxValue&>(h).field);
    else if (nb::isinstance<BoundRadio>(h)) vb.widget(*nb::cast<BoundRadio&>(h).field);
    else if (nb::isinstance<BoundChoice>(h)) vb.widget(*nb::cast<BoundChoice&>(h).field);
    else if (nb::isinstance<BoundPicture>(h)) vb.widget(*nb::cast<BoundPicture&>(h).field);
    else if (nb::isinstance<BoundLink>(h)) vb.widget(*nb::cast<BoundLink&>(h).field);
    else if (nb::isinstance<BoundColor>(h)) vb.widget(*nb::cast<BoundColor&>(h).field);
    else if (nb::isinstance<BoundDate>(h)) vb.widget(*nb::cast<BoundDate&>(h).field);
    else if (nb::isinstance<BoundTime>(h)) vb.widget(*nb::cast<BoundTime&>(h).field);
    else if (nb::isinstance<BoundExpander>(h)) vb.widget(*nb::cast<BoundExpander&>(h).field);
    else if (nb::isinstance<BoundSpinBoxInt>(h)) vb.widget(*nb::cast<BoundSpinBoxInt&>(h).field);
    else if (nb::isinstance<BoundSpinBoxFloat>(h)) vb.widget(*nb::cast<BoundSpinBoxFloat&>(h).field);
    else if (nb::isinstance<BoundProgress>(h)) vb.widget(*nb::cast<BoundProgress&>(h).field);
    else if (nb::isinstance<BoundButton>(h)) vb.widget(*nb::cast<BoundButton&>(h).field);
    else if (nb::isinstance<BoundComboBox>(h)) vb.widget(*nb::cast<BoundComboBox&>(h).field);
    else if (nb::isinstance<BoundMenu>(h)) vb.widget(*nb::cast<BoundMenu&>(h).field);
    else if (nb::isinstance<BoundLabel>(h)) vb.widget(*nb::cast<BoundLabel&>(h).field);
    else if (nb::isinstance<BoundTextArea>(h)) vb.widget(*nb::cast<BoundTextArea&>(h).field);
    else if (nb::isinstance<BoundDerived<int>>(h)) vb.widget_generic<int>(*nb::cast<BoundDerived<int>&>(h).derived);
    else if (nb::isinstance<BoundDerived<double>>(h)) vb.widget_generic<double>(*nb::cast<BoundDerived<double>&>(h).derived);
    else if (nb::isinstance<BoundDerived<std::string>>(h)) vb.widget_generic<std::string>(*nb::cast<BoundDerived<std::string>&>(h).derived);
    else if (nb::isinstance<BoundDerived<bool>>(h)) vb.widget_generic<bool>(*nb::cast<BoundDerived<bool>&>(h).derived);
    else if (nb::isinstance<BoundList<int>>(h)) vb.list(*nb::cast<BoundList<int>&>(h).list);
    else if (nb::isinstance<BoundList<double>>(h)) vb.list(*nb::cast<BoundList<double>&>(h).list);
    else if (nb::isinstance<BoundList<std::string>>(h)) vb.list(*nb::cast<BoundList<std::string>&>(h).list);
     else throw nb::type_error("ViewBuilder.widget: unsupported handle type");
}
// Declares `text` as the tooltip of the widget for handle `h`, wherever the view places it.
// Same handle kinds as py_widget_dispatch, minus lists (no single widget to hover).
inline std::function<void(ViewBuilder&)> py_tooltip_for(nb::object h, std::string text) {
    auto declare = [&](auto* obs) -> std::function<void(ViewBuilder&)> {
        if (!obs) return {};
        return [obs, text = std::move(text)](ViewBuilder& vb) { vb.tooltip_for(*obs, text); };
    };
    if (nb::isinstance<BoundField<int>>(h)) return declare(nb::cast<BoundField<int>&>(h).field);
    if (nb::isinstance<BoundField<double>>(h)) return declare(nb::cast<BoundField<double>&>(h).field);
    if (nb::isinstance<BoundField<std::string>>(h)) return declare(nb::cast<BoundField<std::string>&>(h).field);
    if (nb::isinstance<BoundField<bool>>(h)) return declare(nb::cast<BoundField<bool>&>(h).field);
    if (nb::isinstance<BoundShared<int>>(h)) return declare(nb::cast<BoundShared<int>&>(h).shared);
    if (nb::isinstance<BoundShared<double>>(h)) return declare(nb::cast<BoundShared<double>&>(h).shared);
    if (nb::isinstance<BoundShared<std::string>>(h)) return declare(nb::cast<BoundShared<std::string>&>(h).shared);
    if (nb::isinstance<BoundShared<bool>>(h)) return declare(nb::cast<BoundShared<bool>&>(h).shared);
    if (nb::isinstance<BoundSliderValue>(h)) {
        auto& b = nb::cast<BoundSliderValue&>(h);
        return b.field_h ? declare(b.field_h) : declare(b.field_v);
    }
    if (nb::isinstance<BoundCheckboxValue>(h)) return declare(nb::cast<BoundCheckboxValue&>(h).field);
    if (nb::isinstance<BoundRadio>(h)) return declare(nb::cast<BoundRadio&>(h).field);
    if (nb::isinstance<BoundChoice>(h)) return declare(nb::cast<BoundChoice&>(h).field);
    if (nb::isinstance<BoundPicture>(h)) return declare(nb::cast<BoundPicture&>(h).field);
    if (nb::isinstance<BoundLink>(h)) return declare(nb::cast<BoundLink&>(h).field);
    if (nb::isinstance<BoundColor>(h)) return declare(nb::cast<BoundColor&>(h).field);
    if (nb::isinstance<BoundDate>(h)) return declare(nb::cast<BoundDate&>(h).field);
    if (nb::isinstance<BoundTime>(h)) return declare(nb::cast<BoundTime&>(h).field);
    if (nb::isinstance<BoundExpander>(h)) return declare(nb::cast<BoundExpander&>(h).field);
    if (nb::isinstance<BoundSpinBoxInt>(h)) return declare(nb::cast<BoundSpinBoxInt&>(h).field);
    if (nb::isinstance<BoundSpinBoxFloat>(h)) return declare(nb::cast<BoundSpinBoxFloat&>(h).field);
    if (nb::isinstance<BoundProgress>(h)) return declare(nb::cast<BoundProgress&>(h).field);
    if (nb::isinstance<BoundButton>(h)) return declare(nb::cast<BoundButton&>(h).field);
    if (nb::isinstance<BoundComboBox>(h)) return declare(nb::cast<BoundComboBox&>(h).field);
    if (nb::isinstance<BoundDerived<int>>(h)) return declare(nb::cast<BoundDerived<int>&>(h).derived);
    if (nb::isinstance<BoundDerived<double>>(h)) return declare(nb::cast<BoundDerived<double>&>(h).derived);
    if (nb::isinstance<BoundDerived<std::string>>(h)) return declare(nb::cast<BoundDerived<std::string>&>(h).derived);
    if (nb::isinstance<BoundDerived<bool>>(h)) return declare(nb::cast<BoundDerived<bool>&>(h).derived);
    throw nb::type_error("tooltip: this kind of field has no single widget to hover");
}

inline void py_list_dispatch(ViewBuilder& vb, nb::object h, bool reorderable = false, bool selectable = false) {
    auto place = [&](auto& bound) {
        auto handle = vb.list(*bound.list);
        if (reorderable) std::move(handle).reorderable();
        if (selectable) std::move(handle).selection(*bound.selected);
    };
    if (nb::isinstance<BoundList<int>>(h)) place(nb::cast<BoundList<int>&>(h));
    else if (nb::isinstance<BoundList<double>>(h)) place(nb::cast<BoundList<double>&>(h));
    else if (nb::isinstance<BoundList<std::string>>(h)) place(nb::cast<BoundList<std::string>&>(h));
    else throw nb::type_error("ViewBuilder.list: unsupported handle type");
}
