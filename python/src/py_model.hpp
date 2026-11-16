#pragma once
// PyModel along with the binding templates and the error hub its members are registered with.

#include "common.hpp"

// A dragged Python object. Held through a shared_ptr so that copies of the drag value (std::any,
// on the logic thread) never touch Python reference counts; the holder releases it GIL-safely.
struct PyDragValue {
    std::shared_ptr<PyHolder> obj;
};

// The drag role of `vb.widget(h, drag=..., drop=..., accepts=...)`: drag() gives the value (None:
// nothing to drag), drop(value, x, y) takes one whose type is `accepts` (any Python object if None).
inline DragRole py_drag_role(nb::object drag, nb::object drop, nb::object accepts) {
    DragRole role;
    if (!drag.is_none()) {
        auto fn = std::make_shared<PyHolder>(std::move(drag));
        role.source = [fn](const WidgetNode&, Point) -> std::any {
            nb::gil_scoped_acquire gil;
            try {
                nb::object v = fn->obj();
                if (v.is_none()) return {};
                return PyDragValue{std::make_shared<PyHolder>(std::move(v))};
            } catch (nb::python_error&) {
                report_python_callback_error();
                return {};
            }
        };
    }
    if (!drop.is_none()) {
        auto fn = std::make_shared<PyHolder>(std::move(drop));
        auto type = std::make_shared<PyHolder>(std::move(accepts));
        role.accepts = [type](const std::any& v) {
            auto* p = std::any_cast<PyDragValue>(&v);
            if (!p) return false;
            nb::gil_scoped_acquire gil;
            return type->obj.is_none() || nb::isinstance(p->obj->obj, nb::handle(type->obj));
        };
        role.drop = [fn](const std::any& v, DropPlace at) {
            nb::gil_scoped_acquire gil;
            try {
                fn->obj(std::any_cast<const PyDragValue&>(v).obj->obj, at.at.x.raw(), at.at.y.raw());
            } catch (nb::python_error&) {
                report_python_callback_error();
            }
        };
    }
    return role;
}
inline void py_canvas_dispatch(ViewBuilder& vb, nb::object h) {
    if (nb::isinstance<BoundPlot>(h)) {
        auto& b = nb::cast<BoundPlot&>(h);
        vb.canvas(*b.plot).depends_on(b.plot->x_range).depends_on(b.plot->y_range).depends_on(b.plot->view).depends_on(b.plot->cursor).depends_on(b.plot->revision);
    } else throw nb::type_error("ViewBuilder.canvas: unsupported handle type (expected BoundPlot)");
}
inline void py_tree_dispatch(ViewBuilder& vb, nb::object h) {
    if (nb::isinstance<BoundTree>(h)) {
        auto& b = nb::cast<BoundTree&>(h);
        if (b.ctrl) vb.tree(*b.ctrl);
    } else throw nb::type_error("ViewBuilder.tree: unsupported handle type (expected BoundTree)");
}

// add_slider_slot()'s return type: exactly one of field_h/field_v is set,
// matching BoundSliderValue's own field_h/field_v split (see its comment).
struct SliderAlloc {
    std::shared_ptr<SlotBase> owner;
    Field<Slider<double, Orientation::Horizontal>>* field_h = nullptr;
    Field<Slider<double, Orientation::Vertical>>* field_v = nullptr;
};

struct PyModel {
    std::vector<std::shared_ptr<SlotBase>> slots;
    std::mutex slots_mutex;
    std::vector<std::function<void(ViewBuilder&)>> tooltips;   // guarded by slots_mutex

    void set_tooltip(nb::object handle, std::string text) {
        auto declare = py_tooltip_for(std::move(handle), std::move(text));
        std::lock_guard<std::mutex> lk(slots_mutex);
        if (declare) tooltips.push_back(std::move(declare));
    }

    std::pair<std::shared_ptr<SlotBase>, Field<int>*> add_int_slot(int v) {
        auto s = std::make_shared<Slot<int>>(v);
        auto* p = &s->field;
        std::lock_guard<std::mutex> lk(slots_mutex);
        slots.push_back(s);
        return {s, p};
    }
    std::pair<std::shared_ptr<SlotBase>, Field<double>*> add_float_slot(double v) {
        auto s = std::make_shared<Slot<double>>(v);
        auto* p = &s->field;
        std::lock_guard<std::mutex> lk(slots_mutex);
        slots.push_back(s);
        return {s, p};
    }
    std::pair<std::shared_ptr<SlotBase>, Field<std::string>*> add_str_slot(std::string v) {
        auto s = std::make_shared<Slot<std::string>>(std::move(v));
        auto* p = &s->field;
        std::lock_guard<std::mutex> lk(slots_mutex);
        slots.push_back(s);
        return {s, p};
    }
    std::pair<std::shared_ptr<SlotBase>, Field<bool>*> add_bool_slot(bool v) {
        auto s = std::make_shared<Slot<bool>>(v);
        auto* p = &s->field;
        std::lock_guard<std::mutex> lk(slots_mutex);
        slots.push_back(s);
        return {s, p};
    }
    SliderAlloc add_slider_slot(double v, double mn, double mx, bool vertical, double step = 0.0,
                                std::string label = {}, std::string unit = {}) {
        std::lock_guard<std::mutex> lk(slots_mutex);
        if (vertical) {
            auto s = std::make_shared<SlotSliderV>(Slider<double, Orientation::Vertical>{v, mn, mx, step, label, unit});
            auto* p = &s->field;
            slots.push_back(s);
            return {s, nullptr, p};
        }
        auto s = std::make_shared<SlotSlider>(Slider<double>{v, mn, mx, step, std::move(label), std::move(unit)});
        auto* p = &s->field;
        slots.push_back(s);
        return {s, p, nullptr};
    }
    template <typename S>
    std::pair<std::shared_ptr<SlotBase>, Field<S>*> add_widget_slot(S v) {
        auto s = std::make_shared<SlotWidget<S>>(std::move(v));
        auto* p = &s->field;
        std::lock_guard<std::mutex> lk(slots_mutex);
        slots.push_back(s);
        return {s, p};
    }
    std::pair<std::shared_ptr<SlotBase>, Field<Checkbox>*> add_checkbox_slot(bool v, std::string label) {
        auto s = std::make_shared<SlotCheckbox>(Checkbox{v, std::move(label)});
        auto* p = &s->field;
        std::lock_guard<std::mutex> lk(slots_mutex);
        slots.push_back(s);
        return {s, p};
    }
    std::pair<std::shared_ptr<SlotBase>, Shared<int>*> add_shared_int_slot(int v) {
        auto s = std::make_shared<SlotShared<int>>(v);
        auto* p = &s->shared;
        std::lock_guard<std::mutex> lk(slots_mutex);
        slots.push_back(s);
        return {s, p};
    }
    std::pair<std::shared_ptr<SlotBase>, Shared<double>*> add_shared_float_slot(double v) {
        auto s = std::make_shared<SlotShared<double>>(v);
        auto* p = &s->shared;
        std::lock_guard<std::mutex> lk(slots_mutex);
        slots.push_back(s);
        return {s, p};
    }
    std::pair<std::shared_ptr<SlotBase>, Shared<std::string>*> add_shared_str_slot(std::string v) {
        auto s = std::make_shared<SlotShared<std::string>>(std::move(v));
        auto* p = &s->shared;
        std::lock_guard<std::mutex> lk(slots_mutex);
        slots.push_back(s);
        return {s, p};
    }
    std::pair<std::shared_ptr<SlotBase>, Shared<bool>*> add_shared_bool_slot(bool v) {
        auto s = std::make_shared<SlotShared<bool>>(v);
        auto* p = &s->shared;
        std::lock_guard<std::mutex> lk(slots_mutex);
        slots.push_back(s);
        return {s, p};
    }
    std::pair<std::shared_ptr<SlotBase>, Channel<int>*> add_channel_int_slot() {
        auto s = std::make_shared<SlotChannel<int>>();
        auto* p = &s->channel;
        std::lock_guard<std::mutex> lk(slots_mutex);
        slots.push_back(s);
        return {s, p};
    }
    std::pair<std::shared_ptr<SlotBase>, Channel<double>*> add_channel_float_slot() {
        auto s = std::make_shared<SlotChannel<double>>();
        auto* p = &s->channel;
        std::lock_guard<std::mutex> lk(slots_mutex);
        slots.push_back(s);
        return {s, p};
    }
    std::pair<std::shared_ptr<SlotBase>, Channel<std::string>*> add_channel_str_slot() {
        auto s = std::make_shared<SlotChannel<std::string>>();
        auto* p = &s->channel;
        std::lock_guard<std::mutex> lk(slots_mutex);
        slots.push_back(s);
        return {s, p};
    }
    std::pair<std::shared_ptr<SlotBase>, Channel<bool>*> add_channel_bool_slot() {
        auto s = std::make_shared<SlotChannel<bool>>();
        auto* p = &s->channel;
        std::lock_guard<std::mutex> lk(slots_mutex);
        slots.push_back(s);
        return {s, p};
    }
    std::pair<std::shared_ptr<SlotBase>, List<int>*> add_list_int_slot(std::vector<int> v) {
        auto s = std::make_shared<SlotList<int>>(std::move(v));
        auto* p = &s->list;
        std::lock_guard<std::mutex> lk(slots_mutex);
        slots.push_back(s);
        return {s, p};
    }
    std::pair<std::shared_ptr<SlotBase>, List<double>*> add_list_float_slot(std::vector<double> v) {
        auto s = std::make_shared<SlotList<double>>(std::move(v));
        auto* p = &s->list;
        std::lock_guard<std::mutex> lk(slots_mutex);
        slots.push_back(s);
        return {s, p};
    }
    std::pair<std::shared_ptr<SlotBase>, List<std::string>*> add_list_str_slot(std::vector<std::string> v) {
        auto s = std::make_shared<SlotList<std::string>>(std::move(v));
        auto* p = &s->list;
        std::lock_guard<std::mutex> lk(slots_mutex);
        slots.push_back(s);
        return {s, p};
    }
    // List<bool> disabled due to vector<bool> proxy issues — Python bool lists map to List<int> via __init__.py

    std::pair<std::shared_ptr<SlotBase>, prism::plot::PlotModel*> add_plot_slot() {
        auto s = std::make_shared<SlotPlot>();
        auto* p = &s->plot;
        std::lock_guard<std::mutex> lk(slots_mutex);
        slots.push_back(s);
        return {s, p};
    }
    std::pair<std::shared_ptr<SlotBase>, SlotTable*> add_table_slot(nb::object py_obj) {
        auto s = std::make_shared<SlotTable>(std::move(py_obj));
        auto* p = s.get();
        std::lock_guard<std::mutex> lk(slots_mutex);
        slots.push_back(s);
        return {s, p};
    }
    std::pair<std::shared_ptr<SlotBase>, prism::ui::TreeController*> add_tree_slot(nb::object py_obj) {
        auto s = std::make_shared<SlotTree>(py_obj);
        auto* p = &s->ctrl;
        std::lock_guard<std::mutex> lk(slots_mutex);
        slots.push_back(s);
        return {s, p};
    }

    // Derived slots — vector-based deps (avoid immutable tuple assignment)
    template <typename T>
    std::pair<std::shared_ptr<SlotBase>, SlotDerived<T>*> add_derived_slot_vec(nb::object fn, const std::vector<nb::object>& deps, const std::string& name = "<derived>") {
        T init{};
        {
            nb::gil_scoped_acquire g;
            // fn() runs synchronously under the GIL inside this direct Python call
            // (Model() construction) — let a genuine exception from the user's
            // function (nb::python_error, e.g. ZeroDivisionError) propagate
            // unchanged, so Model() raises it as-is instead of silently defaulting
            // to T{}. Only a type mismatch on the *result* is ours to translate.
            nb::object result = fn();
            try {
                init = nb::cast<T>(result);
            } catch (const std::bad_cast&) {
                std::string msg = "derived '" + name + "': function returned " +
                                   nb::cast<std::string>(nb::repr(result)) + ", expected " + py_type_name<T>();
                throw nb::type_error(msg.c_str());
            }
        }
        auto s = std::make_shared<SlotDerived<T>>(std::move(fn), std::move(init));
        for (auto& dep : deps) derived_attach_dep(s, dep);
        std::lock_guard<std::mutex> lk(slots_mutex);
        slots.push_back(s);
        return {s, s.get()};
    }
    // Python view callback — set from Model.__init__ if subclass overrides view().
    nb::object py_view_cb = nb::none();
    void set_view_callback(nb::object cb) { py_view_cb = std::move(cb); }

    void view(ViewBuilder& vb) {
        {
            std::lock_guard<std::mutex> lk(slots_mutex);
            for (auto& declare : tooltips) declare(vb);
        }
        // Check callback with GIL held — nb::object copy/incref requires GIL.
        {
            nb::gil_scoped_acquire gil;
            if (!py_view_cb.is_none()) {
                nb::object cb = py_view_cb;
                nb::object vb_obj = nb::cast(&vb, nb::rv_policy::reference);
                try {
                    cb(vb_obj);
                } catch (...) {
                    report_python_callback_error();
                }
                return;
            }
        }
        std::lock_guard<std::mutex> lk(slots_mutex);
        for (auto& s : slots) s->build(vb);
    }

    void drain() {
        // Snapshot under the lock, then drain the copy lock-free: s->drain() runs
        // Python observer callbacks (Shared/Channel), and any allocation inside one
        // can trigger CPython's automatic GC on this same thread — which re-enters
        // pymodel_tp_traverse/pymodel_tp_clear, both of which also take
        // slots_mutex. Holding the lock across drain() would self-deadlock (a
        // plain std::mutex isn't recursive) the moment that happens.
        std::vector<std::shared_ptr<SlotBase>> slots_copy;
        {
            std::lock_guard<std::mutex> lk(slots_mutex);
            slots_copy = slots;
        }
        for (auto& s : slots_copy) s->drain();
        drain_standalone();
    }

    // Same body as pymodel_tp_clear, exposed for __init__.py's _atexit_clear:
    // interpreter-exit teardown for models still rooted in module globals. Without
    // it, a SlotTree holding a Python source object keeps that object (and, when
    // its class lives in __main__, the whole __main__ dict — source -> class ->
    // method __globals__) alive past nanobind's Py_AtExit leak check, so the model
    // is reported as leaked even though nothing is actually held past exit.
    void release_py_refs() {
        py_view_cb = nb::none();
        std::vector<std::shared_ptr<SlotBase>> slots_copy;
        {
            std::lock_guard<std::mutex> lk(slots_mutex);
            slots_copy = slots;
        }
        for (auto& s : slots_copy) s->clear();
    }
};

// tp_traverse/tp_clear — makes PyModel (and every Python subclass, including
// prism.Model) participate in the cyclic GC. Without this, py_view_cb and
// each slot's Python callables (SlotDerived::py_fn/dep_keepalive_,
// SlotTree::py_src_holder) are strong references held entirely on the C++
// side: a cycle through them (Model -> slot -> callback -> Model) is
// invisible to and unbreakable by the GC. See nanobind's refleaks.rst
// "Reference cycles" section for the pattern.
static int pymodel_tp_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (!nb::inst_ready(self)) return 0; // constructor may not have run yet
    PyModel* m = nb::inst_ptr<PyModel>(self);
    if (PyObject* o = m->py_view_cb.ptr()) {
        int vret = visit(o, arg);
        if (vret) return vret;
    }
    std::lock_guard<std::mutex> lk(m->slots_mutex);
    for (auto& s : m->slots) s->traverse(visit, arg);
    return 0;
}

static int pymodel_tp_clear(PyObject* self) {
    if (!nb::inst_ready(self)) return 0; // constructor may not have run yet — see tp_traverse
    nb::inst_ptr<PyModel>(self)->release_py_refs();
    return 0;
}

static PyType_Slot pymodel_type_slots[] = {
    {Py_tp_traverse, (void*)pymodel_tp_traverse},
    {Py_tp_clear, (void*)pymodel_tp_clear},
    {0, nullptr},
};

// simplify: leaked by design. A weakref.WeakSet tracking every observed handle needs one
// long-lived Python object shared across all observe() calls. A `static nb::object` local
// would run its Py_DECREF destructor during C++ static teardown, which happens after
// Py_Finalize — decref'ing into an already-torn-down interpreter is UB. A raw PyObject*
// that is created once and never released sidesteps that: nothing ever runs its destructor.
// python/prism/__init__.py's _atexit_clear() empties this set (while the interpreter is
// still fully alive) before shutdown, so the only thing actually leaked is one now-empty
// WeakSet container. Upgrade path: a Py_AtExit callback if a hard release guarantee is
// ever needed instead of relying on atexit ordering.
static PyObject* g_observed_handles = nullptr;

static nb::handle observed_handles() {
    if (!g_observed_handles) {
        nb::object ws = nb::module_::import_("weakref").attr("WeakSet")();
        g_observed_handles = ws.release().ptr();
    }
    return nb::handle(g_observed_handles);
}

// Appends the Connection returned by an observe*() call to self.__dict__["_prism_keepalive"]
// (creating the list on first use) so a fire-and-forget `handle.observe(cb)` keeps firing —
// nothing else holds a reference to the Connection once the caller drops it. Also registers
// self in the module-level _observed_handles WeakSet so python/prism/__init__.py's atexit
// cleanup can find and disconnect any handle still referenced (e.g. a long-lived module
// global) before interpreter shutdown. Standalone observe*() no longer takes
// nb::keep_alive<0,1> on `self` (Task 14 fix — see FieldHandle/SharedHandle above): the
// Connection's keep_alive(state) keeps the hub alive instead, so handle -> keepalive
// list -> Connection is a plain acyclic chain that dies with `self`, not a GC-invisible
// self-cycle. Wraps the Connection into a Python object exactly once and returns that
// same object.
//
// `cb` is also appended to self.__dict__["_prism_callbacks"] — this is now the callback's
// ONLY strong reference (see WeakCallback above: every observe*() wrapper holds just a weak
// reference to `cb`, resolved under the GIL at call time). A callback that captures the
// owning Model (`m.x.observe(lambda v: m.rebuild())`) closes a Model -> ... -> handle ->
// callback -> Model cycle; making the handle's own dynamic_attr dict the sole strong owner
// of `cb` — which nanobind's inst_traverse walks — puts every strong reference in that cycle
// on a Python-visible edge, so the cyclic GC can actually detect and collect it (a real
// std::function-held nb::callable copy from a plain C++ member, as it was before, would be
// an extra reference the GC can never see — subtract_refs()'s refcount bookkeeping treats
// that as "referenced from outside," which keeps the whole cycle alive forever regardless of
// what else also references the same callback; mirroring `cb` elsewhere without first making
// the receivers_ side weak would not have fixed anything).
static nb::object keep_connection(nb::object self, Connection conn, nb::callable cb) {
    nb::object result = nb::cast(std::move(conn));
    nb::dict d = self.attr("__dict__");
    // setdefault, not contains()-then-[]= : the two-step check-then-set race
    // (two threads both seeing "absent" and each installing their own fresh
    // list, one clobbering the other's) is real on a free-threaded build —
    // setdefault's own C level get-or-insert is atomic under the dict's lock.
    nb::list keepalive = nb::cast<nb::list>(d.attr("setdefault")("_prism_keepalive", nb::list()));
    keepalive.append(result);
    nb::list callbacks = nb::cast<nb::list>(d.attr("setdefault")("_prism_callbacks", nb::list()));
    callbacks.append(cb);
    observed_handles().attr("add")(self);
    return result;
}

// Runs a user validator installed by field()/shared()'s Python descriptor
// (as `handle.__dict__["_prism_validator"]`, see _FieldDescriptor._allocate
// / _SharedDescriptor._allocate in python/prism/__init__.py) before a write
// reaches the handle's underlying Field/Shared. GIL is already held here
// (we're inside a Python call), so the validator runs synchronously on the
// calling thread, before any off-thread dispatch. A rejecting validator's
// exception (nb::python_error) propagates unchanged.
template <typename T>
T apply_validator(nb::object self, T v) {
    nb::dict d = self.attr("__dict__");
    if (!d.contains("_prism_validator")) return v;
    nb::object validator = d["_prism_validator"];
    nb::object result = validator(v);
    try {
        return nb::cast<T>(result);
    } catch (const std::bad_cast&) {
        std::string name = d.contains("_prism_name") ? nb::cast<std::string>(nb::str(d["_prism_name"])) : "<field>";
        // nb::repr(result) itself calls into Python (__repr__) and can raise (e.g. a
        // validator returning an object whose __repr__ is broken) — fall back to a
        // fixed placeholder so that failure doesn't replace the TypeError we're
        // already in the middle of raising.
        std::string repr;
        try {
            repr = nb::cast<std::string>(nb::repr(result));
        } catch (...) {
            repr = "<unrepresentable>";
        }
        std::string msg = "validator for '" + name + "' must return a " + py_type_name<T>() +
                           " (or raise); got " + repr;
        throw nb::type_error(msg.c_str());
    }
}

// Shared body for a handle's `.value` setter and `.set()` method — same
// validate-then-write on both, so `m.x = v`, `m.x.value = v` and
// `m.x.set(v)` behave identically (the Python descriptor's __set__ just
// delegates to `.value =`, giving a single validation path).
template <typename Handle, typename T>
void validated_set(nb::object self, T v) {
    v = apply_validator<T>(self, std::move(v));
    nb::cast<Handle&>(self).set(std::move(v));
}

// One registration for each BoundWidgetValue instantiation (radio, spin box, progress).
template <typename H, typename V>
nb::class_<H> bind_widget_value(nb::module_& m, const char* name, const char* doc) {
    return nb::class_<H>(m, name, nb::dynamic_attr(), nb::is_weak_referenceable(), doc)
        .def_prop_rw("value", &H::get, &validated_set<H, V>, "Any thread. Writes are posted to the logic thread.")
        .def("observe", [](nb::object self, nb::callable cb) {
            return keep_connection(self, nb::cast<H&>(self).observe(cb), cb);
        }, nb::arg("callback"), kObserveDoc)
        .def("get", &H::get, "Any thread; dispatched read. Same as .value.")
        .def("set", &validated_set<H, V>, nb::arg("value"), "Any thread; posted to the logic thread.");
}

// A menu handle: its choices by label (as shown, without the "&"), and its items' state.
template <typename H>
void bind_menu(nb::module_& m, const char* name, const char* doc) {
    bind_widget_value<H, std::int64_t>(m, name, doc)
        .def_prop_ro("items", [](const H& h) {
            std::vector<std::string> out;
            for (auto& it : h.state().items) if (!it.separator) out.push_back(prism::ui::menu_text(it));
            return out;
        }, "The items' labels, as shown; separators left out.")
        .def_prop_ro("chosen", [](const H& h) -> std::optional<std::string> {
            auto s = h.state();
            auto* item = item_at_path(s.items, s.chosen_path);
            if (!item) return std::nullopt;
            return prism::ui::menu_text(*item);
        }, "The label of the item chosen last, or None.")
        .def("on_choice", [](nb::object self, nb::callable cb) {
            auto& h = nb::cast<H&>(self);
            if (!h.field) throw nb::value_error("on_choice(): handle is not bound to a Model");
            auto* f = h.field;
            auto conn = prism::ui::on_choice(*f, [wc = WeakCallback(cb), f](const std::vector<std::size_t>& path) {
                if (auto* item = item_at_path(f->get().items, path)) wc(prism::ui::menu_text(*item));
            });
            if (h.owner) conn.keep_alive(h.owner);
            return keep_connection(self, std::move(conn), cb);
        }, nb::arg("callback"),
             "Runs callback(label) on the logic thread each time the user chooses an item; not when "
             "the app changes the menu.")
        .def("set_enabled", [](H& h, std::string label, bool on) {
            h.modify([label, on](auto& menu) { for_items_named(menu.items, label, [on](MenuItem& it) { it.enabled = on; }); });
        }, nb::arg("label"), nb::arg("enabled"), "Greys an item out (False) or back. Any thread; posted.")
        .def("checked", [](const H& h, std::string label) -> std::optional<bool> {
            auto items = h.state().items;
            std::optional<bool> mark;
            for_items_named(items, label, [&](MenuItem& it) { if (!mark) mark = it.checked; });
            return mark;
        }, nb::arg("label"), "An item's check mark, or None if it is not a check item.");
}

// One templated registration per scalar handle family (Field/Bound/Shared/BoundShared/
// Channel/BoundChannel/BoundDerived), instantiated once per T from NB_MODULE below.
// Naming is not uniformly "<Family>" + suffix: BoundField<T> is historically named
// "Bound" + suffix (not "BoundField" + suffix) — preserved here since it is Python-visible API.
template <typename T>
void bind_scalar(nb::module_& m, const char* suffix) {
    std::string field_name = std::string("Field") + suffix;
    auto field_cls = nb::class_<FieldHandle<T>>(m, field_name.c_str(), nb::dynamic_attr(), nb::is_weak_referenceable())
        .def(nb::init<T>(), nb::arg("value") = T{})
        .def_prop_rw("value", &FieldHandle<T>::get, &validated_set<FieldHandle<T>, T>)
        .def("get", &FieldHandle<T>::get)
        .def("set", &validated_set<FieldHandle<T>, T>);
    field_cls.def("observe", [](nb::object self, nb::callable cb) {
        return keep_connection(self, nb::cast<FieldHandle<T>&>(self).observe(cb), cb);
    }, nb::arg("callback"), kObserveDoc);
    if constexpr (std::is_same_v<T, int> || std::is_same_v<T, double>) {
        field_cls.def("add", [](nb::object self, T n) {
            auto& h = nb::cast<FieldHandle<T>&>(self);
            field_add_dispatch<T>(self, h.field, h.field.get(), n);
        }, nb::arg("n"), "Any thread; atomic increment applied on the logic thread.");
    }

    std::string bound_field_name = std::string("Bound") + suffix;
    auto bound_field_cls = nb::class_<BoundField<T>>(m, bound_field_name.c_str(), nb::dynamic_attr(), nb::is_weak_referenceable())
        .def_prop_rw("value", &BoundField<T>::get, &validated_set<BoundField<T>, T>)
        .def("observe", [](nb::object self, nb::callable cb) {
            return keep_connection(self, nb::cast<BoundField<T>&>(self).observe(cb), cb);
        }, nb::arg("callback"), kObserveDoc)
        .def("get", &BoundField<T>::get)
        .def("set", &validated_set<BoundField<T>, T>);
    if constexpr (std::is_same_v<T, int> || std::is_same_v<T, double>) {
        bound_field_cls.def("add", [](nb::object self, T n) {
            auto& h = nb::cast<BoundField<T>&>(self);
            if (h.field) field_add_dispatch<T>(self, h.owner, h.field, n);
        }, nb::arg("n"), "Any thread; atomic increment applied on the logic thread.");
    }

    std::string shared_name = std::string("Shared") + suffix;
    auto shared_cls = nb::class_<SharedHandle<T>>(m, shared_name.c_str(), nb::dynamic_attr(), nb::is_weak_referenceable())
        .def(nb::init<T>(), nb::arg("value") = T{})
        .def_prop_rw("value", &SharedHandle<T>::get, &validated_set<SharedHandle<T>, T>)
        .def("get", &SharedHandle<T>::get).def("set", &validated_set<SharedHandle<T>, T>);
    shared_cls.def("observe", [](nb::object self, nb::callable cb) {
        return keep_connection(self, nb::cast<SharedHandle<T>&>(self).observe(cb), cb);
    }, nb::arg("callback"), kObserveDoc);

    std::string bound_shared_name = std::string("BoundShared") + suffix;
    nb::class_<BoundShared<T>>(m, bound_shared_name.c_str(), nb::dynamic_attr(), nb::is_weak_referenceable())
        .def_prop_rw("value", &BoundShared<T>::get, &validated_set<BoundShared<T>, T>)
        .def("observe", [](nb::object self, nb::callable cb) {
            return keep_connection(self, nb::cast<BoundShared<T>&>(self).observe(cb), cb);
        }, nb::arg("callback"), kObserveDoc)
        .def("get", &BoundShared<T>::get).def("set", &validated_set<BoundShared<T>, T>);

    std::string channel_name = std::string("Channel") + suffix;
    nb::class_<ChannelHandle<T>>(m, channel_name.c_str(), nb::dynamic_attr(), nb::is_weak_referenceable())
        .def(nb::init<>()).def("send", &ChannelHandle<T>::send)
        .def("observe", [](nb::object self, nb::callable cb) {
            return keep_connection(self, nb::cast<ChannelHandle<T>&>(self).observe(cb), cb);
        }, nb::arg("callback"), kObserveDoc);

    std::string bound_channel_name = std::string("BoundChannel") + suffix;
    nb::class_<BoundChannel<T>>(m, bound_channel_name.c_str(), nb::dynamic_attr(), nb::is_weak_referenceable())
        .def("send", &BoundChannel<T>::send)
        .def("observe", [](nb::object self, nb::callable cb) {
            return keep_connection(self, nb::cast<BoundChannel<T>&>(self).observe(cb), cb);
        }, nb::arg("callback"), kObserveDoc);

    std::string bound_derived_name = std::string("BoundDerived") + suffix;
    nb::class_<BoundDerived<T>>(m, bound_derived_name.c_str(), nb::dynamic_attr(), nb::is_weak_referenceable())
        .def_prop_ro("value", &BoundDerived<T>::get).def("get", &BoundDerived<T>::get)
        .def("observe", [](nb::object self, nb::callable cb) {
            return keep_connection(self, nb::cast<BoundDerived<T>&>(self).observe(cb), cb);
        }, nb::arg("callback"), kObserveDoc)
        // Recomputes when `dep` changes too: a derived with no dep list adds what it read
        // (prism.derived). The owner is the SlotDerived itself (add_derived_slot_vec).
        .def("_follow", [](BoundDerived<T>& self, nb::object dep) {
            if (!self.derived || !self.owner) return;
            derived_attach_dep(std::static_pointer_cast<SlotDerived<T>>(self.owner), std::move(dep));
        }, nb::arg("dep"));
}

// List<bool>/BoundList<bool> are deliberately never instantiated — vector<bool> proxy is
// incompatible with the const T& Signal used by List<T>; use an int list for bool data.
template <typename T>
void bind_list(nb::module_& m, const char* suffix) {
    std::string list_name = std::string("List") + suffix;
    nb::class_<ListHandle<T>>(m, list_name.c_str(), nb::dynamic_attr(), nb::is_weak_referenceable())
        .def(nb::init<>()).def("push", &ListHandle<T>::push).def("erase", &ListHandle<T>::erase)
        .def("set", &ListHandle<T>::set).def("replace_all", &ListHandle<T>::replace_all)
        .def("size", &ListHandle<T>::size).def("get", &ListHandle<T>::get).def("to_list", &ListHandle<T>::to_list)
        .def("observe_insert", [](nb::object self, nb::callable cb) {
            return keep_connection(self, nb::cast<ListHandle<T>&>(self).observe_insert(cb), cb);
        }, nb::arg("callback"), kObserveDoc)
        .def("observe_remove", [](nb::object self, nb::callable cb) {
            return keep_connection(self, nb::cast<ListHandle<T>&>(self).observe_remove(cb), cb);
        }, nb::arg("callback"), kObserveDoc)
        .def("observe_update", [](nb::object self, nb::callable cb) {
            return keep_connection(self, nb::cast<ListHandle<T>&>(self).observe_update(cb), cb);
        }, nb::arg("callback"), kObserveDoc);

    std::string bound_list_name = std::string("BoundList") + suffix;
    nb::class_<BoundList<T>>(m, bound_list_name.c_str(), nb::dynamic_attr(), nb::is_weak_referenceable())
        .def("push", &BoundList<T>::push).def("erase", &BoundList<T>::erase).def("set", &BoundList<T>::set).def("replace_all", &BoundList<T>::replace_all)
        .def("size", &BoundList<T>::size).def("get", &BoundList<T>::get).def("to_list", &BoundList<T>::to_list)
        .def("observe_insert", [](nb::object self, nb::callable cb) {
            return keep_connection(self, nb::cast<BoundList<T>&>(self).observe_insert(cb), cb);
        }, nb::arg("callback"), kObserveDoc)
        .def("observe_remove", [](nb::object self, nb::callable cb) {
            return keep_connection(self, nb::cast<BoundList<T>&>(self).observe_remove(cb), cb);
        }, nb::arg("callback"), kObserveDoc)
        .def("observe_update", [](nb::object self, nb::callable cb) {
            return keep_connection(self, nb::cast<BoundList<T>&>(self).observe_update(cb), cb);
        }, nb::arg("callback"), kObserveDoc)
        .def_prop_rw("selected", &BoundList<T>::get_selected, &BoundList<T>::set_selected,
                     "The selected row (None: none), for a list placed with selectable=True. "
                     "A removal keeps it on the same item. Any thread.")
        .def("observe_selected", [](nb::object self, nb::callable cb) {
            return keep_connection(self, nb::cast<BoundList<T>&>(self).observe_selected(cb), cb);
        }, nb::arg("callback"), kObserveDoc);
}

// Module-wide Python error hub backing prism.on_error(). Mirrors g_observed_handles above:
// a raw PyObject* the module owns, incref/decref'd explicitly under the GIL rather than a
// `static nb::object` whose destructor would run (and decref into a torn-down interpreter)
// during C++ static teardown after Py_Finalize. on_error() always decrefs the handler it
// replaces, so nothing accumulates here.
static std::mutex g_error_handler_mutex;
static PyObject* g_error_handler = nullptr;

// Same stderr fallback error_hub.hpp's default_error_handler prints for a plain C++
// exception_ptr, plus the Python-specific case: a caught nb::python_error gets Python's
// own traceback printer instead of ex.what().
static void print_default_error(std::exception_ptr e) {
    try {
        std::rethrow_exception(e);
    } catch (nb::python_error& pe) {
        // restore()/PyErr_Print() need a live interpreter to print into; with none up
        // (e.g. this is running during/after Py_Finalize) fall back to a plain line.
        if (Py_IsInitialized()) {
            pe.restore();
            PyErr_Print();
        } else {
            std::cerr << "[prism] unhandled exception in posted callback: " << pe.what() << '\n';
        }
    } catch (const std::exception& ex) {
        std::cerr << "[prism] unhandled exception in posted callback: " << ex.what() << '\n';
    } catch (...) {
        std::cerr << "[prism] unhandled exception in posted callback: <non-std exception>\n";
    }
}

// Builds the object passed to a Python on_error handler: nb::python_error already carries
// the original Python exception instance; anything else becomes a RuntimeError.
static nb::object exception_to_python(std::exception_ptr e) {
    try {
        std::rethrow_exception(e);
    } catch (nb::python_error& pe) {
        return nb::borrow(pe.value());
    } catch (const std::exception& ex) {
        return nb::module_::import_("builtins").attr("RuntimeError")(ex.what());
    } catch (...) {
        return nb::module_::import_("builtins").attr("RuntimeError")("<non-std exception>");
    }
}

// Installed once at module init as prism::core's process-wide unhandled-error hook (Task 1
// — include/prism/core/error_hub.hpp). May run on any thread, with or without the GIL
// already held by the caller, so it acquires the GIL itself and only if the interpreter is
// up at all. If this handler throws, report_unhandled_error() (error_hub.hpp) is already
// guarded to fall back to its own stderr default — never call it re-entrantly here.
static void python_error_hub(std::exception_ptr e) {
    if (!Py_IsInitialized()) {
        print_default_error(e);
        return;
    }
    nb::gil_scoped_acquire gil;
    PyObject* handler_raw;
    {
        std::lock_guard<std::mutex> lk(g_error_handler_mutex);
        handler_raw = g_error_handler;
    }
    if (!handler_raw) {
        print_default_error(e);
        return;
    }
    nb::object handler = nb::borrow(nb::handle(handler_raw));
    try {
        handler(exception_to_python(e));
    } catch (nb::python_error& he) {
        // The handler itself raised — print its exception (not the original) so the
        // failure is visible, then still report the original via the default path.
        he.restore();
        PyErr_Print();
        print_default_error(e);
    } catch (...) {
        print_default_error(e);
    }
}

// Every Python callback wrapper below routes a caught exception here instead of printing
// or silently swallowing it — see task-11-brief.md. Only valid inside a catch block
// (relies on std::current_exception()).
static void report_python_callback_error() {
    prism::core::report_unhandled_error(std::current_exception());
}

// Test-only debug hook: how many strong references does a standalone handle's
// Shared<T>/Channel<T> state have right now? Exposed as
// _standalone_shared_use_count() so a test can directly prove observe() no longer
// leaves the hub holding a reference to itself (a use_count that grows by one per
// observe() call, never reclaimed short of disconnect(), would be that leak) —
// more precise than watching the alive/dead count, which a full disconnect()
// cleans up either way and so can't distinguish the redundant reference from a
// correctly-single-owned one.
static int64_t standalone_state_use_count(nb::object h) {
    if (nb::isinstance<SharedHandle<int>>(h)) return nb::cast<SharedHandle<int>&>(h).shared.use_count();
    if (nb::isinstance<SharedHandle<double>>(h)) return nb::cast<SharedHandle<double>&>(h).shared.use_count();
    if (nb::isinstance<SharedHandle<std::string>>(h)) return nb::cast<SharedHandle<std::string>&>(h).shared.use_count();
    if (nb::isinstance<SharedHandle<bool>>(h)) return nb::cast<SharedHandle<bool>&>(h).shared.use_count();
    if (nb::isinstance<ChannelHandle<int>>(h)) return nb::cast<ChannelHandle<int>&>(h).channel.use_count();
    if (nb::isinstance<ChannelHandle<double>>(h)) return nb::cast<ChannelHandle<double>&>(h).channel.use_count();
    if (nb::isinstance<ChannelHandle<std::string>>(h)) return nb::cast<ChannelHandle<std::string>&>(h).channel.use_count();
    if (nb::isinstance<ChannelHandle<bool>>(h)) return nb::cast<ChannelHandle<bool>&>(h).channel.use_count();
    throw std::runtime_error("_standalone_shared_use_count: unsupported handle type");
}

// Headless backend for pytest and prism.headless() — stays alive until either
// delay_ms elapses or _request_quit() is called from any thread. Single-instance
// (only one _run_headless can be active at a time, enforced by g_run_guard), so a
// plain global condition variable + flag is enough — no per-instance state needed.
static std::mutex g_headless_cv_mutex;
static std::condition_variable g_headless_cv;
static std::atomic<bool> g_headless_quit_requested{false};

// Runs the same drain a mutation-queue post/tick already does (drain_queue_loop,
// whose tail is AppContext's shared drain_publish — WidgetTree::drain_shared(),
// which calls model.drain(), then publish_dirty/schedule_tick) on the logic
// thread, and blocks the caller until it completes. Called right before
// WindowClose fires so a post/Shared-write made just before quit() is not
// dropped (WindowClose sets the mutation queue's closed flag, after which any
// later post is a silent no-op).
static void final_drain_before_close() {
    std::optional<AppContext::PostHandle> hopt;
    {
        std::lock_guard<std::mutex> lk(g_handle_mutex);
        if (!g_has_handle.load(std::memory_order_acquire) || !g_post_handle) return;
        hopt = *g_post_handle;
    }
    auto q = hopt->queue.lock();
    auto sf = hopt->scheduled.lock();
    if (!q || !sf) return;
    auto tp = hopt->drain_publish.lock();
    auto prom = std::make_shared<std::promise<void>>();
    auto fut = prom->get_future();
    exec::start_detached(stdexec::schedule(hopt->sched) | stdexec::then([q, sf, tp, prom] {
        if (Py_IsInitialized()) {
            nb::gil_scoped_acquire gil;
            drain_queue_loop(q, sf, tp);
        }
        prom->set_value();
    }));
    // This runs on the thread that called _run_headless (released the GIL around
    // model_app()), same guard as the spins/waits above — only re-release if we
    // actually hold it.
    std::optional<nb::gil_scoped_release> rel;
    if (Py_IsInitialized() && PyGILState_Check()) rel.emplace();
    fut.wait();
}
