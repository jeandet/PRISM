#pragma once
// The bound handle types: sliders, checkboxes, widgets, shared, channel, derived and list handles.

#include "common.hpp"

// --- List dispatch helper (mirrors field_set_dispatch but for arbitrary op) ---
// `keep` is the owning shared_ptr (standalone handle state, or a Bound* handle's
// SlotBase owner) that any posted/buffered copy of `fn` carries along, so the object
// `fn` closes over cannot be freed before the logic thread runs it.
inline void list_op_dispatch(std::shared_ptr<void> keep, std::function<void()> fn) {
    if (txn_active()) { txn_queue.emplace_back([keep, fn]() mutable { fn(); }); return; }
    if (!prism::app::detail_is_logic_thread()) {
        auto res = try_post_via_handle_impl([keep, fn]() mutable { fn(); }, false);
        if (res == PostResult::Posted) return;
        if (res == PostResult::Closed) return;
    }
    if (prism::app::detail_is_logic_thread() && Py_IsInitialized()) {
        nb::gil_scoped_acquire g; fn();
    } else {
        std::lock_guard<std::recursive_mutex> direct_lk(g_direct_dispatch_mutex);
        fn();
    }
}

// Bound handle — references Field owned by PyModel via shared_ptr<SlotBase> (no Model cycle).
// Holding the Slot directly keeps the Field/SenderHub alive even after the Model is GC'd.
template <typename T>
struct BoundField {
    std::shared_ptr<SlotBase> owner;
    Field<T>* field = nullptr;
    T get() const {
        if (!field) return T{};
        Field<T>* p = field;
        return dispatch_sync_read<T>([p](){ return p->get(); });
    }
    void set(T v) {
        if (field) field_set_dispatch(owner, field, std::move(v));
    }
    Connection observe(nb::callable cb) {
        if (!field) throw nb::value_error("observe(): handle is not bound to a Model");
        auto owner_copy = owner;
        Field<T>* f = field;
        auto conn = f->on_change().connect(WeakCallback(cb));
        if (owner_copy) conn.keep_alive(owner_copy);
        return conn;
    }
};

// Per-orientation slider ops shared by BoundSliderValue below. Each posts (or
// buffers under prism.transaction()) a single read-modify-write closure that
// runs entirely on the logic thread — the only thread allowed to touch
// Field<T> directly — so a concurrent set()/set_range() from another thread
// can never race a read against a write: whichever closure runs first is
// fully applied (including the fields it leaves untouched) before the next
// one reads.
template <Orientation O>
double slider_get(Field<Slider<double, O>>* field) {
    Field<Slider<double, O>>* p = field;
    return dispatch_sync_read<Slider<double, O>>([p](){ return p->get(); }).value;
}
template <Orientation O>
void slider_set(std::shared_ptr<void> keep, Field<Slider<double, O>>* field, double v) {
    Field<Slider<double, O>>* p = field;
    list_op_dispatch(std::move(keep), [p, v](){ auto s = p->get(); s.value = v; p->set(s); });
}
template <Orientation O>
Slider<double, O> slider_read(Field<Slider<double, O>>* field) {
    Field<Slider<double, O>>* p = field;
    return dispatch_sync_read<Slider<double, O>>([p](){ return p->get(); });
}
template <Orientation O>
nb::tuple slider_range(Field<Slider<double, O>>* field) {
    Field<Slider<double, O>>* p = field;
    auto s = dispatch_sync_read<Slider<double, O>>([p](){ return p->get(); });
    return nb::make_tuple(s.min, s.max);
}
template <Orientation O>
void slider_set_range(std::shared_ptr<void> keep, Field<Slider<double, O>>* field, double mn, double mx) {
    Field<Slider<double, O>>* p = field;
    list_op_dispatch(std::move(keep), [p, mn, mx](){ auto s = p->get(); s.min = mn; s.max = mx; p->set(s); });
}
template <Orientation O>
Connection slider_observe(Field<Slider<double, O>>* field, nb::callable cb) {
    Field<Slider<double, O>>* f = field;
    return f->on_change().connect([wc = WeakCallback(cb)](const Slider<double, O>& val) { wc(val.value); });
}

// Bound handle for a slider's inner value. Slider<double,O> holds
// value/min/max/step together in one struct behind one Field (see
// SlotSliderT above), so unlike BoundField<T> there is no Field<double> to
// point at directly. Orientation is a compile-time Slider<T,O> template
// parameter (see SlotSliderT's comment) — it doesn't affect the struct's
// layout, only which Widget<Slider<T,O>> renders it — so a Field<Slider<double,
// Horizontal>> and a Field<Slider<double,Vertical>> are two distinct C++
// types with identical semantics. Exactly one of field_h/field_v is set
// (chosen once at allocation, by orientation), and every method below just
// branches on which.
struct BoundSliderValue {
    std::shared_ptr<SlotBase> owner;
    Field<Slider<double, Orientation::Horizontal>>* field_h = nullptr;
    Field<Slider<double, Orientation::Vertical>>* field_v = nullptr;
    double get() const {
        if (field_h) return slider_get(field_h);
        if (field_v) return slider_get(field_v);
        return 0.0;
    }
    void set(double v) {
        if (field_h) slider_set(owner, field_h, v);
        else if (field_v) slider_set(owner, field_v, v);
    }
    nb::tuple range() const {
        if (field_h) return slider_range(field_h);
        if (field_v) return slider_range(field_v);
        return nb::make_tuple(0.0, 1.0);
    }
    // The parts set once at construction: label, unit, step.
    template <typename Fn>
    auto read(Fn&& part) const {
        if (field_h) return part(slider_read(field_h));
        if (field_v) return part(slider_read(field_v));
        return part(Slider<double>{});
    }
    std::string label() const { return read([](const auto& s) { return s.label; }); }
    std::string unit() const { return read([](const auto& s) { return s.unit; }); }
    double step() const { return read([](const auto& s) { return s.step; }); }
    void set_range(double mn, double mx) {
        if (field_h) slider_set_range(owner, field_h, mn, mx);
        else if (field_v) slider_set_range(owner, field_v, mn, mx);
    }
    void set_range_tuple(nb::tuple t) {
        if (nb::len(t) != 2) throw nb::value_error("range must be assigned a (min, max) tuple");
        set_range(nb::cast<double>(t[0]), nb::cast<double>(t[1]));
    }
    Connection observe(nb::callable cb) {
        if (!field_h && !field_v) throw nb::value_error("observe(): handle is not bound to a Model");
        auto owner_copy = owner;
        Connection conn = field_h ? slider_observe(field_h, cb) : slider_observe(field_v, cb);
        if (owner_copy) conn.keep_alive(owner_copy);
        return conn;
    }
};

// Bound handle for a checkbox's inner checked flag — same rationale as BoundSliderValue:
// Checkbox is a plain {checked, label} struct behind one Field, so label is cached here
// at construction rather than read back on every set().
struct BoundCheckboxValue {
    std::shared_ptr<SlotBase> owner;
    Field<Checkbox>* field = nullptr;
    std::string label;
    bool get() const {
        if (!field) return false;
        Field<Checkbox>* p = field;
        return dispatch_sync_read<Checkbox>([p](){ return p->get(); }).checked;
    }
    void set(bool v) {
        if (field) field_set_dispatch<Checkbox>(owner, field, Checkbox{v, label});
    }
    Connection observe(nb::callable cb) {
        if (!field) throw nb::value_error("observe(): handle is not bound to a Model");
        auto owner_copy = owner;
        Field<Checkbox>* f = field;
        auto conn = f->on_change().connect([wc = WeakCallback(cb)](const Checkbox& val) { wc(val.checked); });
        if (owner_copy) conn.keep_alive(owner_copy);
        return conn;
    }
};

// Bound handle for the one value Python sees inside a widget struct S (the radio group's
// chosen option, a spin box's or progress bar's value). A write is posted as an in-place
// change of that value only, so the rest of the struct (options, range, label) is never
// overwritten by a stale copy. `check` (optional) rejects a value on the calling thread.
template <typename S, typename V>
struct BoundWidgetValue {
    std::shared_ptr<SlotBase> owner;
    Field<S>* field = nullptr;
    V (*read)(const S&) = nullptr;
    void (*write)(S&, const V&) = nullptr;
    void (*check)(const S&, const V&) = nullptr;
    V get() const {
        if (!field) throw nb::value_error("handle is not bound to a Model");
        Field<S>* p = field;
        return read(dispatch_sync_read<S>([p](){ return p->get(); }));
    }
    void set(V v) {
        if (!field) throw nb::value_error("handle is not bound to a Model");
        Field<S>* p = field;
        if (check) {
            auto current = dispatch_sync_read<S>([p](){ return p->get(); });
            check(current, v);
        }
        auto w = write;
        list_op_dispatch(owner, [p, w, v](){ auto s = p->get(); w(s, v); p->set(s); });
    }
    // The whole widget value and a change to it, for properties beyond .value (a progress
    // bar's busy, a combo box's options); same thread rules as get()/set().
    S state() const {
        if (!field) throw nb::value_error("handle is not bound to a Model");
        Field<S>* p = field;
        return dispatch_sync_read<S>([p](){ return p->get(); });
    }
    template <typename F>
    void modify(F fn) {
        if (!field) throw nb::value_error("handle is not bound to a Model");
        Field<S>* p = field;
        list_op_dispatch(owner, [p, fn](){ auto s = p->get(); fn(s); p->set(s); });
    }
    Connection observe(nb::callable cb) {
        if (!field) throw nb::value_error("observe(): handle is not bound to a Model");
        auto owner_copy = owner;
        auto r = read;
        auto conn = field->on_change().connect([wc = WeakCallback(cb), r](const S& s) { wc(r(s)); });
        if (owner_copy) conn.keep_alive(owner_copy);
        return conn;
    }
};

using BoundRadio = BoundWidgetValue<RadioGroup, std::string>;
using BoundChoice = BoundWidgetValue<Choice, std::optional<std::string>>;   // .value: the option, or None
using BoundPicture = BoundWidgetValue<Picture, std::int64_t>;               // .value: unused (0)
using BoundLink = BoundWidgetValue<Link, std::int64_t>;                     // .value: clicks so far
using BoundColor = BoundWidgetValue<Color, std::string>;                    // .value: "#RRGGBB"
using BoundDate = BoundWidgetValue<std::chrono::year_month_day, std::string>; // .value: "YYYY-MM-DD"
using BoundTime = BoundWidgetValue<prism::ui::TimeOfDay, std::string>;        // .value: "HH:MM"
using BoundExpander = BoundWidgetValue<Expander, bool>;                     // .value: open
using BoundSpinBoxInt = BoundWidgetValue<SpinBox<std::int64_t>, std::int64_t>;
using BoundSpinBoxFloat = BoundWidgetValue<SpinBox<double>, double>;
using BoundProgress = BoundWidgetValue<ProgressBar, double>;
using BoundButton = BoundWidgetValue<Button, std::int64_t>;       // .value: clicks so far
using BoundComboBox = BoundWidgetValue<ComboBox, std::string>;
using BoundPopup = BoundWidgetValue<Popup, bool>;                 // .value: open
using BoundMenu = BoundWidgetValue<Menu, std::int64_t>;               // .value: choices so far
using BoundDockLayout = BoundWidgetValue<DockLayout, DockLayout>;    // Python sees it as nested dicts
using BoundLabel = BoundWidgetValue<Label<std::string>, std::string>;
using BoundTextArea = BoundWidgetValue<TextArea<std::string>, std::string>;
using BoundTextField = BoundWidgetValue<TextField<std::string>, std::string>;
using BoundPassword = BoundWidgetValue<Password<std::string>, std::string>;
using BoundTabs = BoundWidgetValue<TabBar<>, std::int64_t>;          // .value: the selected tab
using BoundDialog = BoundWidgetValue<Dialog, bool>;                  // .value: open

// A handle whose .value is the struct's `value` member (a label's text, a text area's text).
template <typename H, typename S>
H make_bound_text(std::shared_ptr<SlotBase> owner, Field<S>* f) {
    H h;
    h.owner = std::move(owner);
    h.field = f;
    h.read = [](const S& s) { return s.value; };
    h.write = [](S& s, const std::string& v) { s.value = v; };
    return h;
}

// A dock layout as Python sees it: {"panels": [...], "active": 0, "vertical": False,
// "sizes": [...], "parts": [...]}, nested. Converted on the calling thread (with the GIL), so no
// Python object reaches the logic thread.
inline nb::dict dock_to_py(const DockNode& n) {
    nb::dict d;
    d["panels"] = nb::cast(n.panels);
    d["active"] = n.active;
    d["vertical"] = n.vertical;
    d["sizes"] = nb::cast(n.sizes);
    nb::list parts;
    for (auto& p : n.parts) parts.append(dock_to_py(p));
    d["parts"] = parts;
    return d;
}

inline DockNode dock_from_py(nb::handle h) {
    auto d = nb::cast<nb::dict>(h);
    DockNode n;
    if (d.contains("panels")) n.panels = nb::cast<std::vector<std::string>>(d["panels"]);
    if (d.contains("active")) n.active = nb::cast<std::size_t>(d["active"]);
    if (d.contains("vertical")) n.vertical = nb::cast<bool>(d["vertical"]);
    if (d.contains("sizes")) n.sizes = nb::cast<std::vector<float>>(d["sizes"]);
    if (d.contains("parts"))
        for (auto p : nb::cast<nb::list>(d["parts"])) n.parts.push_back(dock_from_py(p));
    return n;
}
using BoundContextMenu = BoundWidgetValue<ContextMenu, std::int64_t>;

template <typename M>
BoundWidgetValue<M, std::int64_t> make_bound_menu(std::shared_ptr<SlotBase> owner, Field<M>* f) {
    BoundWidgetValue<M, std::int64_t> h;
    h.owner = std::move(owner);
    h.field = f;
    h.read = [](const M& m) { return static_cast<std::int64_t>(m.activations); };
    h.write = [](M&, const std::int64_t&) {};   // a count of the user's choices: not the app's to set
    return h;
}

// Menu items from the normalized dicts python/prism/__init__.py builds (_menu_items): text,
// shortcut ("Ctrl+S", or ""), enabled, checked (None, or the mark), icon (None, or a file),
// separator, items (a submenu, as these).
inline std::vector<MenuItem> py_menu_items(const std::vector<nb::dict>& items) {
    std::vector<MenuItem> out;
    for (const auto& d : items) {
        MenuItem it;
        it.separator = nb::cast<bool>(d["separator"]);
        it.text = nb::cast<std::string>(d["text"]);
        if (auto keys = nb::cast<std::string>(d["shortcut"]); !keys.empty()) {
            auto s = prism::ui::parse_shortcut(keys);
            if (!s) throw nb::value_error(("menu item \"" + it.text + "\": unknown shortcut \"" + keys + "\"").c_str());
            it.shortcut = *s;
        }
        it.enabled = nb::cast<bool>(d["enabled"]);
        if (!d["checked"].is_none()) it.checked = nb::cast<bool>(d["checked"]);
        if (!d["icon"].is_none()) {
            auto loaded = prism::render::load_image(nb::cast<std::string>(d["icon"]), Size{Width{16}, Height{16}});
            if (!loaded) throw nb::value_error(("menu icon: " + loaded.error()).c_str());
            it.icon = *loaded;
        }
        it.items = py_menu_items(nb::cast<std::vector<nb::dict>>(d["items"]));
        out.push_back(std::move(it));
    }
    return out;
}

// Whether `label` names the item: as written ("&Save") or as shown ("Save").
inline bool names_item(const MenuItem& item, const std::string& label) {
    return !item.separator && (item.text == label || prism::ui::menu_text(item) == label);
}

// Calls fn on each item named `label`, in submenus too.
inline void for_items_named(std::vector<MenuItem>& items, const std::string& label, auto fn) {
    for (auto& it : items) {
        if (names_item(it, label)) fn(it);
        for_items_named(it.items, label, fn);
    }
}

// The item at `path` (an index per submenu level), if there is one.
inline const MenuItem* item_at_path(const std::vector<MenuItem>& items, const std::vector<std::size_t>& path) {
    const std::vector<MenuItem>* at = &items;
    const MenuItem* item = nullptr;
    for (auto i : path) {
        if (i >= at->size()) return nullptr;
        item = &(*at)[i];
        at = &item->items;
    }
    return item;
}

inline BoundButton make_bound_button(std::shared_ptr<SlotBase> owner, Field<Button>* f) {
    BoundButton h;
    h.owner = std::move(owner);
    h.field = f;
    h.read = [](const Button& b) { return static_cast<std::int64_t>(b.click_count); };
    h.write = [](Button& b, const std::int64_t& v) { b.click_count = static_cast<std::uint64_t>(std::max<std::int64_t>(0, v)); };
    return h;
}

inline BoundComboBox make_bound_combobox(std::shared_ptr<SlotBase> owner, Field<ComboBox>* f) {
    BoundComboBox h;
    h.owner = std::move(owner);
    h.field = f;
    h.read = [](const ComboBox& c) { return c.value; };
    h.write = [](ComboBox& c, const std::string& v) { c.value = v; };
    return h;
}

inline BoundPopup make_bound_popup(std::shared_ptr<SlotBase> owner, Field<Popup>* f) {
    BoundPopup h;
    h.owner = std::move(owner);
    h.field = f;
    h.read = [](const Popup& p) { return p.open; };
    h.write = [](Popup& p, const bool& v) { p.open = v; };
    return h;
}

// The option's index in `options`, or none.
inline std::optional<std::size_t> option_index(const std::vector<std::string>& options, const std::optional<std::string>& v) {
    if (!v) return std::nullopt;
    auto it = std::ranges::find(options, *v);
    return it == options.end() ? std::nullopt : std::optional{static_cast<std::size_t>(it - options.begin())};
}

inline BoundChoice make_bound_choice(std::shared_ptr<SlotBase> owner, Field<Choice>* f) {
    BoundChoice h;
    h.owner = std::move(owner);
    h.field = f;
    h.read = [](const Choice& c) -> std::optional<std::string> {
        if (auto* s = c.chosen()) return *s;
        return std::nullopt;
    };
    h.write = [](Choice& c, const std::optional<std::string>& v) { c.selected = option_index(c.options, v); };
    h.check = [](const Choice& c, const std::optional<std::string>& v) {
        if (v && !option_index(c.options, v))
            throw nb::value_error(("choice: '" + *v + "' is not one of the options").c_str());
    };
    return h;
}

// An image file, as a picture shows it; ValueError if it cannot be read.
inline std::shared_ptr<const Image> picture_image(const std::string& path, std::optional<Size> box) {
    auto loaded = prism::render::load_image(path, box);
    if (!loaded) throw nb::value_error(("picture: " + loaded.error()).c_str());
    return *loaded;
}

inline BoundExpander make_bound_expander(std::shared_ptr<SlotBase> owner, Field<Expander>* f) {
    BoundExpander h;
    h.owner = std::move(owner);
    h.field = f;
    h.read = [](const Expander& e) { return e.open; };
    h.write = [](Expander& e, const bool& v) { e.open = v; };
    return h;
}

inline BoundLink make_bound_link(std::shared_ptr<SlotBase> owner, Field<Link>* f) {
    BoundLink h;
    h.owner = std::move(owner);
    h.field = f;
    h.read = [](const Link& l) { return static_cast<std::int64_t>(l.click_count); };
    h.write = [](Link& l, const std::int64_t& v) { l.click_count = static_cast<std::uint64_t>(std::max<std::int64_t>(0, v)); };
    return h;
}

inline BoundColor make_bound_color(std::shared_ptr<SlotBase> owner, Field<Color>* f) {
    BoundColor h;
    h.owner = std::move(owner);
    h.field = f;
    h.read = [](const Color& c) { return prism::ui::color_detail::to_hex(c); };
    h.write = [](Color& c, const std::string& v) {
        if (auto p = prism::ui::color_detail::parse_hex(v)) c = Color::rgba(p->r, p->g, p->b, c.a);
    };
    h.check = [](const Color&, const std::string& v) {
        if (!prism::ui::color_detail::parse_hex(v)) throw nb::value_error("color: expected \"#RRGGBB\"");
    };
    return h;
}

inline BoundDate make_bound_date(std::shared_ptr<SlotBase> owner, Field<std::chrono::year_month_day>* f) {
    BoundDate h;
    h.owner = std::move(owner);
    h.field = f;
    h.read = [](const std::chrono::year_month_day& d) { return prism::ui::date_detail::to_iso(d); };
    h.write = [](std::chrono::year_month_day& d, const std::string& v) {
        if (auto p = prism::ui::date_detail::parse_iso(v)) d = *p;
    };
    h.check = [](const std::chrono::year_month_day&, const std::string& v) {
        if (!prism::ui::date_detail::parse_iso(v)) throw nb::value_error("date: expected a real \"YYYY-MM-DD\"");
    };
    return h;
}

inline BoundTime make_bound_time(std::shared_ptr<SlotBase> owner, Field<prism::ui::TimeOfDay>* f) {
    BoundTime h;
    h.owner = std::move(owner);
    h.field = f;
    h.read = [](const prism::ui::TimeOfDay& t) { return prism::ui::time_detail::to_text(t); };
    h.write = [](prism::ui::TimeOfDay& t, const std::string& v) {
        if (auto p = prism::ui::time_detail::parse(v)) t = *p;
    };
    h.check = [](const prism::ui::TimeOfDay&, const std::string& v) {
        if (!prism::ui::time_detail::parse(v)) throw nb::value_error("time_of_day: expected \"HH:MM\"");
    };
    return h;
}

inline BoundPicture make_bound_picture(std::shared_ptr<SlotBase> owner, Field<Picture>* f) {
    BoundPicture h;
    h.owner = std::move(owner);
    h.field = f;
    h.read = [](const Picture&) { return std::int64_t{0}; };
    h.write = [](Picture&, const std::int64_t&) {};
    return h;
}

inline BoundRadio make_bound_radio(std::shared_ptr<SlotBase> owner, Field<RadioGroup>* f) {
    BoundRadio h;
    h.owner = std::move(owner);
    h.field = f;
    h.read = [](const RadioGroup& g) { return g.selected < g.options.size() ? g.options[g.selected] : std::string{}; };
    h.write = [](RadioGroup& g, const std::string& v) {
        g.selected = static_cast<std::size_t>(std::ranges::find(g.options, v) - g.options.begin());
    };
    h.check = [](const RadioGroup& g, const std::string& v) {
        if (std::ranges::find(g.options, v) == g.options.end())
            throw nb::value_error(("radio: '" + v + "' is not one of the options").c_str());
    };
    return h;
}

template <typename T>
BoundWidgetValue<SpinBox<T>, T> make_bound_spinbox(std::shared_ptr<SlotBase> owner, Field<SpinBox<T>>* f) {
    BoundWidgetValue<SpinBox<T>, T> h;
    h.owner = std::move(owner);
    h.field = f;
    h.read = [](const SpinBox<T>& s) { return s.value; };
    // Like the widget: kept inside [min, max].
    h.write = [](SpinBox<T>& s, const T& v) { s.value = std::clamp(v, s.min, std::max(s.min, s.max)); };
    return h;
}

inline BoundProgress make_bound_progress(std::shared_ptr<SlotBase> owner, Field<ProgressBar>* f) {
    BoundProgress h;
    h.owner = std::move(owner);
    h.field = f;
    h.read = [](const ProgressBar& p) { return p.value; };
    h.write = [](ProgressBar& p, const double& v) { p.value = v; };
    return h;
}

// Standalone / bound handles for Shared<T> and Channel<T>
//
// The Shared<T> itself lives behind a shared_ptr ("state"), not by value in the
// handle. drain_fn captures that shared_ptr (never `this`/a raw pointer into the
// handle), so if a Python callback running inside this handle's own
// drain_notifications() drops the handle's last Python reference, the state that
// still-running call is reading/writing outlives the call — only the (now
// pointless) handle wrapper goes away. keep_alive(state) on the returned
// Connection means the state also outlives the handle whenever something else
// (e.g. a Connection stored elsewhere) still needs it.
//
// observe()'s wrapper must NOT also capture `state`: the wrapper is stored inside
// state->on_change()'s receivers_, a member of *state itself, so a `state` capture
// there is a strong reference from the hub back to its own owning object — the hub
// leaks forever (only disconnect() removing the receiver breaks it) for no safety
// benefit, since drain_fn already keeps state alive across the whole drain call,
// callbacks included.
template <typename T>
struct SharedHandle {
    std::shared_ptr<Shared<T>> shared;
    std::shared_ptr<std::function<void()>> drain_fn;
    SharedHandle(T init) : shared(make_tracked_state<Shared<T>>(std::move(init))) {
        auto state = shared;
        drain_fn = std::make_shared<std::function<void()>>([state] { state->drain_notifications(); });
        register_standalone_drainer(drain_fn);
    }
    ~SharedHandle() {
        drain_fn.reset();
        prune_expired_standalone_drainers();
    }
    SharedHandle(const SharedHandle&) = delete;
    SharedHandle& operator=(const SharedHandle&) = delete;
    SharedHandle(SharedHandle&&) = delete;
    SharedHandle& operator=(SharedHandle&&) = delete;
    T get() const { return shared->get(); }
    void set(T v) { shared->set(std::move(v)); ensure_idle_wake(); }
    Connection observe(nb::callable cb) {
        auto conn = shared->on_change().connect(WeakCallback(cb));
        conn.keep_alive(shared);
        return conn;
    }
};
template <typename T>
struct BoundShared {
    std::shared_ptr<SlotBase> owner;
    Shared<T>* shared = nullptr;
    T get() const { return shared ? shared->get() : T{}; }
    void set(T v) {
        if (shared) { shared->set(std::move(v)); ensure_idle_wake(); }
    }
    Connection observe(nb::callable cb) {
        if (!shared) throw nb::value_error("observe(): handle is not bound to a Model");
        auto owner_copy = owner;
        Shared<T>* s = shared;
        auto conn = s->on_change().connect(WeakCallback(cb));
        if (owner_copy) conn.keep_alive(owner_copy);
        return conn;
    }
};
// State ownership rationale (including why observe()'s wrapper captures only `cb`,
// never `state`) mirrors SharedHandle above.
template <typename T>
struct ChannelHandle {
    std::shared_ptr<Channel<T>> channel;
    std::shared_ptr<std::function<void()>> drain_fn;
    ChannelHandle() : channel(make_tracked_state<Channel<T>>()) {
        auto state = channel;
        drain_fn = std::make_shared<std::function<void()>>([state] { state->drain_notifications(); });
        register_standalone_drainer(drain_fn);
    }
    ~ChannelHandle() {
        drain_fn.reset();
        prune_expired_standalone_drainers();
    }
    ChannelHandle(const ChannelHandle&) = delete;
    ChannelHandle& operator=(const ChannelHandle&) = delete;
    ChannelHandle(ChannelHandle&&) = delete;
    ChannelHandle& operator=(ChannelHandle&&) = delete;
    void send(T v) { channel->send(std::move(v)); ensure_idle_wake(); }
    Connection observe(nb::callable cb) {
        auto conn = channel->on_receive().connect(WeakCallback(cb));
        conn.keep_alive(channel);
        return conn;
    }
};
template <typename T>
struct BoundChannel {
    std::shared_ptr<SlotBase> owner;
    Channel<T>* channel = nullptr;
    void send(T v) {
        if (channel) { channel->send(std::move(v)); ensure_idle_wake(); }
    }
    Connection observe(nb::callable cb) {
        if (!channel) throw nb::value_error("observe(): handle is not bound to a Model");
        auto owner_copy = owner;
        Channel<T>* c = channel;
        auto conn = c->on_receive().connect(WeakCallback(cb));
        if (owner_copy) conn.keep_alive(owner_copy);
        return conn;
    }
};

// Derived — Python compute with C++ subscription
template <typename T>
struct SlotDerived : SlotBase {
    T value_{};
    nb::object py_fn = nb::none();
    SenderHub<const T&> changed_;
    std::vector<std::shared_ptr<SlotBase>> dep_owners_;
    std::vector<nb::object> dep_keepalive_; // keeps standalone handles alive
    std::vector<Connection> observers_;
    // Declared last so it's destroyed FIRST (C++ destroys members in reverse
    // declaration order): deps_'s Connections must disconnect from each
    // dependency's SenderHub before dep_owners_ (above) releases the
    // shared_ptr that keeps that dependency's Slot alive — the other order
    // frees the Slot first and deps_'s disconnect() then UAFs into it.
    std::vector<Connection> deps_;
    SlotDerived() = default;
    SlotDerived(nb::object fn, T init) : py_fn(std::move(fn)), value_(std::move(init)) {}
    T get() const {
        const T* p = &value_;
        return dispatch_sync_read<T>([p](){ return *p; });
    }
    SenderHub<const T&>& on_change() { return changed_; }
    void recompute() {
        T nv{};
        {
            if (!Py_IsInitialized()) return;
            nb::gil_scoped_acquire g;
            nb::object result;
            try { result = py_fn(); } catch (...) { report_python_callback_error(); return; }
            try {
                nv = nb::cast<T>(result);
            } catch (const std::bad_cast&) {
                // Not a cast<T> the caller can retry — a clear TypeError, routed through
                // on_error like any other derived-callback failure, rather than a silent
                // no-op (value_ untouched below) or a crash.
                std::string msg = std::string("derived function must return a ") + py_type_name<T>() +
                                   " (or raise); got " + nb::cast<std::string>(nb::repr(result));
                PyErr_SetString(PyExc_TypeError, msg.c_str());
                prism::core::report_unhandled_error(std::make_exception_ptr(nb::python_error()));
                return;
            }
        }
        if (nv == value_) return;
        value_ = std::move(nv);
        emit_or_defer(static_cast<void*>(&changed_), [this]{ changed_.emit(value_); });
    }
    void build(ViewBuilder& vb) override { vb.widget_generic<T>(*this); }
    void traverse(visitproc visit, void* arg) override {
        if (PyObject* o = py_fn.ptr()) visit(o, arg);
        for (auto& o : dep_keepalive_) if (PyObject* p = o.ptr()) visit(p, arg);
    }
    // Only release py_fn/dep_keepalive_ (the nb::objects that can form a cycle back
    // to the owning Model) — NOT deps_/dep_owners_. Those are a live cross-reference
    // into another slot's SenderHub (dep_owners_ is what keeps that slot's Field
    // alive at all past this Model's own teardown); breaking it here from
    // tp_clear, ahead of this SlotDerived's own destruction, races the owning
    // Model's slots vector destructor and can UAF into an already-freed SenderHub.
    // A recompute that still fires after py_fn is cleared is safe either way:
    // py_fn() on nb::none() raises a plain (caught, reported) TypeError.
    void clear() override {
        dep_keepalive_.clear();
        py_fn = nb::none();
    }
    // A `keep` shared_ptr<SlotBase> held by a posted/queued closure can be the
    // reference that drops this SlotDerived to zero, and the mutation queue itself
    // is destroyed with the GIL released (model_app.hpp:190) — same hazard as
    // SlotTree above. Release py_fn/dep_keepalive_ under the GIL (or leak them if
    // the interpreter is already gone) before the member destructors below run;
    // deps_/dep_owners_ need no such guard — they hold no Python objects. See
    // release_py_object_guarded above.
    ~SlotDerived() override {
        release_py_object_guarded(py_fn);
        for (auto& o : dep_keepalive_) release_py_object_guarded(o);
    }
};

template <typename T>
struct BoundDerived {
    std::shared_ptr<SlotBase> owner;
    SlotDerived<T>* derived = nullptr;
    T get() const {
        if (!derived) return T{};
        SlotDerived<T>* p = derived;
        // SlotDerived::get already does dispatch, but keep direct to avoid double dispatch
        return p->get();
    }
    Connection observe(nb::callable cb) {
        if (!derived) throw nb::value_error("observe(): handle is not bound to a Model");
        auto owner_copy = owner;
        auto* d = derived;
        auto conn = d->on_change().connect(WeakCallback(cb));
        if (owner_copy) conn.keep_alive(owner_copy);
        return conn;
    }
};
// Standalone list — same shared_ptr-owned-state shape as FieldHandle above (see the
// SharedHandle rationale comment below): observe*()'s Connection keep_alive(list)
// keeps the hub alive, not the Python wrapper.
template <typename T>
struct ListHandle {
    std::shared_ptr<List<T>> list;
    ListHandle() : list(make_tracked_state<List<T>>()) {}
    ListHandle(const ListHandle&) = delete;
    ListHandle& operator=(const ListHandle&) = delete;
    ListHandle(ListHandle&&) = delete;
    ListHandle& operator=(ListHandle&&) = delete;
    void push(T v) {
        List<T>* p = list.get();
        list_op_dispatch(list, [p, v = std::move(v)]() mutable { p->push_back(std::move(v)); });
    }
    void erase(size_t i) {
        List<T>* p = list.get();
        list_op_dispatch(list, [p, i](){ if (i < p->size()) p->erase(i); });
    }
    void set(size_t i, T v) {
        List<T>* p = list.get();
        list_op_dispatch(list, [p, i, v = std::move(v)]() mutable { if (i < p->size()) p->set(i, std::move(v)); });
    }
    void replace_all(nb::list py) {
        std::vector<T> vec; vec.reserve(nb::len(py));
        for (auto h : py) vec.push_back(nb::cast<T>(h));
        List<T>* p = list.get();
        list_op_dispatch(list, [p, vec = std::move(vec)]() mutable { p->replace_all(vec); });
    }
    size_t size() const {
        List<T>* p = list.get();
        return dispatch_sync_read<size_t>([p](){ return p->size(); });
    }
    T get(size_t i) const {
        List<T>* p = list.get();
        // nanobind translates std::out_of_range to Python IndexError at the boundary.
        return dispatch_sync_read<T>([p,i](){
            if (i >= p->size()) throw std::out_of_range("list index out of range");
            return (*p)[i];
        });
    }
    nb::list to_list() const {
        List<T>* p = list.get();
        // Reader may run on the logic thread post-finalization; build a plain
        // std::vector<T> there (no nb:: construction) and convert to nb::list
        // back on this (GIL-holding) calling thread.
        auto vec = dispatch_sync_read<std::vector<T>>([p](){
            std::vector<T> out; out.reserve(p->size());
            for (size_t i=0;i<p->size();++i) out.push_back((*p)[i]);
            return out;
        });
        nb::list out; for (auto& v : vec) out.append(v); return out;
    }
    Connection observe_insert(nb::callable cb) {
        auto conn = list->on_insert().connect(WeakCallback(cb));
        conn.keep_alive(list);
        return conn;
    }
    Connection observe_remove(nb::callable cb) {
        auto conn = list->on_remove().connect(WeakCallback(cb));
        conn.keep_alive(list);
        return conn;
    }
    Connection observe_update(nb::callable cb) {
        auto conn = list->on_update().connect(WeakCallback(cb));
        conn.keep_alive(list);
        return conn;
    }
};
template <typename T>
struct BoundList {
    std::shared_ptr<SlotBase> owner;
    List<T>* list = nullptr;
    Field<std::optional<std::size_t>>* selected = nullptr;
    std::optional<std::size_t> get_selected() const {
        if (!selected) return std::nullopt;
        auto* f = selected;
        return dispatch_sync_read<std::optional<std::size_t>>([f] { return f->get(); });
    }
    void set_selected(std::optional<std::size_t> row) {
        if (!selected) return;
        auto* f = selected;
        list_op_dispatch(owner, [f, row] { f->set(row); });
    }
    Connection observe_selected(nb::callable cb) {
        if (!selected) throw nb::value_error("observe_selected(): handle is not bound to a Model");
        auto c = selected->on_change().connect(WeakCallback(cb));
        if (owner) c.keep_alive(owner);
        return c;
    }
    void push(T v) { if(list) { auto* p=list; list_op_dispatch(owner, [p, v=std::move(v)]() mutable { p->push_back(std::move(v)); }); } }
    void erase(size_t i) { if(list) { auto* p=list; list_op_dispatch(owner, [p,i](){ if(i<p->size()) p->erase(i); }); } }
    void set(size_t i, T v) { if(list) { auto* p=list; list_op_dispatch(owner, [p,i,v=std::move(v)]() mutable { if(i<p->size()) p->set(i,std::move(v)); }); } }
    void replace_all(nb::list py) { if(!list) return; std::vector<T> vec; vec.reserve(nb::len(py)); for(auto h:py) vec.push_back(nb::cast<T>(h)); auto* p=list; list_op_dispatch(owner, [p, vec=std::move(vec)]() mutable { p->replace_all(vec); }); }
    size_t size() const {
        if (!list) return 0;
        List<T>* p = list;
        return dispatch_sync_read<size_t>([p](){ return p->size(); });
    }
    T get(size_t i) const {
        if (!list) return T{};
        List<T>* p = list;
        return dispatch_sync_read<T>([p,i](){
            if (i >= p->size()) throw std::out_of_range("list index out of range");
            return (*p)[i];
        });
    }
    nb::list to_list() const {
        if (!list) return nb::list();
        List<T>* p = list;
        auto vec = dispatch_sync_read<std::vector<T>>([p](){
            std::vector<T> out; out.reserve(p->size());
            for (size_t i=0;i<p->size();++i) out.push_back((*p)[i]);
            return out;
        });
        nb::list out; for (auto& v : vec) out.append(v); return out;
    }
    Connection observe_insert(nb::callable cb) {
        if(!list) throw nb::value_error("observe_insert(): handle is not bound to a Model"); auto owner_copy=owner; auto* p=list; auto c=p->on_insert().connect(WeakCallback(cb)); if(owner_copy) c.keep_alive(owner_copy); return c;
    }
    Connection observe_remove(nb::callable cb) {
        if(!list) throw nb::value_error("observe_remove(): handle is not bound to a Model"); auto owner_copy=owner; auto* p=list; auto c=p->on_remove().connect(WeakCallback(cb)); if(owner_copy) c.keep_alive(owner_copy); return c;
    }
    Connection observe_update(nb::callable cb) {
        if(!list) throw nb::value_error("observe_update(): handle is not bound to a Model"); auto owner_copy=owner; auto* p=list; auto c=p->on_update().connect(WeakCallback(cb)); if(owner_copy) c.keep_alive(owner_copy); return c;
    }
};
