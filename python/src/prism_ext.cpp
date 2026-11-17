// Python bindings for PRISM, one translation unit split across the headers beside it: each
// header uses what the ones above it define, so the include order matters.
#include "common.hpp"
#include "runtime.hpp"
#include "plot_bindings.hpp"
#include "tree_table.hpp"
#include "bound_handles.hpp"
#include "plot_bound.hpp"
#include "text_bound.hpp"
#include "py_view.hpp"
#include "py_model.hpp"

#include <prism/app/event_routing.hpp>

struct DelayHeadlessBackend final : BackendBase {
    HeadlessWindow window_{0, {}};
    std::deque<HeadlessWindow> more_;   // prism.open_window's (a deque: they never move)
    WindowId next_id_ = 2;
    int delay_ms_ = 100;
    explicit DelayHeadlessBackend(int d) : delay_ms_(d) {}
    Window& create_window(WindowConfig cfg) override { window_ = HeadlessWindow{1, cfg}; return window_; }
    Window* request_window(WindowConfig cfg) override { return &more_.emplace_back(next_id_++, cfg); }
    void run(std::function<void(const WindowEvent&)> cb) override {
        {
            std::unique_lock<std::mutex> lk(g_headless_cv_mutex);
            g_headless_cv.wait_for(lk, std::chrono::milliseconds(delay_ms_),
                                    [] { return g_headless_quit_requested.load(std::memory_order_acquire); });
        }
        final_drain_before_close();
        cb(WindowEvent{window_.id(), WindowClose{}});
    }
    void submit(WindowId, std::shared_ptr<const SceneSnapshot>) override {}
    void wake() override {}
    void quit() override {}
};

static void bind_runtime(nb::module_& m) {
    prism::core::set_unhandled_error_handler(python_error_hub);
    m.def("_set_error_handler", [](nb::object handler) {
        PyObject* new_raw = nullptr;
        if (!handler.is_none()) {
            handler.inc_ref();
            new_raw = handler.ptr();
        }
        PyObject* old_raw;
        {
            std::lock_guard<std::mutex> lk(g_error_handler_mutex);
            old_raw = g_error_handler;
            g_error_handler = new_raw;
        }
        if (old_raw) nb::handle(old_raw).dec_ref();
    }, nb::arg("handler").none());
    m.def("is_logic_thread", [](){ return detail_is_logic_thread(); });
    m.attr("_observed_handles") = observed_handles();
    m.def("_standalone_state_alive_count", [](){ return g_standalone_state_alive.load(std::memory_order_relaxed); });
    m.def("_standalone_shared_use_count", &standalone_state_use_count, nb::arg("handle"));

    nb::class_<Connection>(m, "Connection", nb::dynamic_attr(), nb::is_weak_referenceable())
        .def("disconnect", &Connection::disconnect)
        .def("__enter__", [](Connection& self){ return &self; })
        .def("__exit__", [](Connection& self, nb::object, nb::object, nb::object){ self.disconnect(); return false; });

    bind_scalar<int>(m, "Int");
    bind_scalar<double>(m, "Float");
    bind_scalar<std::string>(m, "Str");
    bind_scalar<bool>(m, "Bool");
}

static void bind_handles(nb::module_& m) {
    nb::class_<BoundSliderValue>(m, "BoundSlider", nb::dynamic_attr(), nb::is_weak_referenceable(),
                                 "Bound handle for a Model's slider field. .value/.range "
                                 "assignments may be made from any thread (posted to the "
                                 "logic thread); reads dispatch synchronously.")
        .def_prop_rw("value", &BoundSliderValue::get, &validated_set<BoundSliderValue, double>,
                     "Any thread. Current slider value (assigned unclamped — only mouse drags clamp into range).")
        .def_prop_rw("range", &BoundSliderValue::range, &BoundSliderValue::set_range_tuple,
                     "Any thread. The slider's (min, max) bounds as a tuple; assigning a "
                     "(min, max) tuple posts the change to the logic thread, like .value.")
        .def_prop_ro("label", &BoundSliderValue::label, "Caption drawn above the track; empty: none.")
        .def_prop_ro("unit", &BoundSliderValue::unit, "Printed after the value in the caption.")
        .def_prop_ro("step", &BoundSliderValue::step, "Snapping step; 0 = continuous. Sets the caption's decimals.")
        .def("observe", [](nb::object self, nb::callable cb) {
            return keep_connection(self, nb::cast<BoundSliderValue&>(self).observe(cb), cb);
        }, nb::arg("callback"), kObserveDoc)
        .def("get", &BoundSliderValue::get, "Any thread; dispatched read. Same as .value.")
        .def("set", &validated_set<BoundSliderValue, double>, nb::arg("value"),
             "Any thread; posted to the logic thread. Same as assigning .value.")
        .def("set_range", [](BoundSliderValue& self, double mn, double mx) {
            PyErr_WarnEx(PyExc_DeprecationWarning,
                         "set_range() is deprecated; assign a (min, max) tuple to .range instead", 1);
            self.set_range(mn, mx);
        }, nb::arg("min"), nb::arg("max"),
             "Deprecated alias for assigning a (min, max) tuple to .range.");

    nb::class_<BoundCheckboxValue>(m, "BoundCheckbox", nb::dynamic_attr(), nb::is_weak_referenceable(),
                                   "Bound handle for a Model's checkbox field. Writes may be "
                                   "made from any thread (posted to the logic thread); reads "
                                   "dispatch synchronously.")
        .def_prop_rw("value", &BoundCheckboxValue::get, &validated_set<BoundCheckboxValue, bool>,
                     "Any thread. Current checked state.")
        .def("observe", [](nb::object self, nb::callable cb) {
            return keep_connection(self, nb::cast<BoundCheckboxValue&>(self).observe(cb), cb);
        }, nb::arg("callback"), kObserveDoc)
        .def("get", &BoundCheckboxValue::get, "Any thread; dispatched read. Same as .value.")
        .def("set", &validated_set<BoundCheckboxValue, bool>, nb::arg("value"),
             "Any thread; posted to the logic thread. Same as assigning .value.");

    bind_widget_value<BoundExpander, bool>(m, "BoundExpander",
        "Bound handle for a Model's section header: .value is True while the section is open.")
        .def_prop_ro("title", [](const BoundExpander& h) { return h.state().title; });
    bind_widget_value<BoundLink, std::int64_t>(m, "BoundLink",
        "Bound handle for a Model's link: .value counts the clicks; observe() fires on each.")
        .def_prop_ro("text", [](const BoundLink& h) { return h.state().text; })
        .def_prop_rw("url", [](const BoundLink& h) { return h.state().url; },
                     [](BoundLink& h, std::string u) { h.modify([u = std::move(u)](Link& l) { l.url = u; }); },
                     "What a click opens. Any thread; posted.");
    bind_widget_value<BoundColor, std::string>(m, "BoundColor",
        "Bound handle for a Model's color picker: .value is \"#RRGGBB\"; setting it takes the same, "
        "with or without '#' (anything else raises ValueError).");
    bind_widget_value<BoundDate, std::string>(m, "BoundDate",
        "Bound handle for a Model's date picker: .value is \"YYYY-MM-DD\"; setting it takes the same "
        "(anything else, or a date that does not exist, raises ValueError).");
    bind_widget_value<BoundTime, std::string>(m, "BoundTime",
        "Bound handle for a Model's time of day: .value is \"HH:MM\"; setting it takes the same "
        "(anything else raises ValueError).");
    bind_widget_value<BoundPicture, std::int64_t>(m, "BoundPicture", "Bound handle for a Model's picture.")
        .def_prop_ro("has_image", [](const BoundPicture& h) { return h.state().image != nullptr; })
        .def_prop_rw("description", [](const BoundPicture& h) { return h.state().description; },
                     [](BoundPicture& h, std::string d) { h.modify([d = std::move(d)](Picture& p) { p.description = d; }); },
                     "What it shows, for assistive tech. Any thread; posted.")
        .def("load", [](BoundPicture& h, std::string path) {
                auto img = picture_image(path, h.state().size);   // read here: a bad file raises now
                h.modify([img](Picture& p) { p.image = img; });
            }, nb::arg("path"), "Shows the image in `path` instead. Any thread; posted.");
    bind_widget_value<BoundChoice, std::optional<std::string>>(m, "BoundChoice",
        "Bound handle for a Model's choice: .value is the chosen option or None; a string that is "
        "not one of the options raises ValueError.")
        .def_prop_rw("options", [](const BoundChoice& h) { return h.state().options; },
                     [](BoundChoice& h, std::vector<std::string> opts) {
                         h.modify([opts = std::move(opts)](Choice& c) {
                             auto chosen = c.chosen() ? std::optional{*c.chosen()} : std::nullopt;
                             c.options = opts;
                             c.selected = option_index(c.options, chosen);   // kept if still there
                         });
                     },
                     "The options; a choice no longer among them becomes None. Any thread; posted.");
    bind_widget_value<BoundRadio, std::string>(m, "BoundRadio",
        "Bound handle for a Model's radio field: .value is the chosen option. Setting a string "
        "that is not one of the options raises ValueError.");
    bind_widget_value<BoundSpinBoxInt, std::int64_t>(m, "BoundSpinBoxInt",
        "Bound handle for a Model's integer spin box. Writes are kept inside [min, max].");
    bind_widget_value<BoundSpinBoxFloat, double>(m, "BoundSpinBoxFloat",
        "Bound handle for a Model's float spin box. Writes are kept inside [min, max].");
    bind_widget_value<BoundProgress, double>(m, "BoundProgress",
        "Bound handle for a Model's progress bar: .value, display only.")
        .def_prop_rw("busy", [](const BoundProgress& h) { return h.state().busy; },
                     [](BoundProgress& h, bool v) { h.modify([v](ProgressBar& p) { p.busy = v; }); },
                     "The work's length is unknown: a segment slides instead of the value.");
    bind_widget_value<BoundButton, std::int64_t>(m, "BoundButton",
        "Bound handle for a Model's button: .value counts the clicks; observe() fires on each.")
        .def_prop_ro("text", [](const BoundButton& h) { return h.state().text; })
        .def_prop_ro("has_icon", [](const BoundButton& h) { return h.state().icon != nullptr; })
        .def_prop_rw("checked", [](const BoundButton& h) { return h.state().checked; },
                     [](BoundButton& h, std::optional<bool> v) { h.modify([v](Button& b) { b.checked = v; }); },
                     "A toggle's state (True or False), or None for a plain button. Any thread; posted.");
    bind_widget_value<BoundComboBox, std::string>(m, "BoundComboBox",
        "Bound handle for a Model's combo box: .value is the text (free text, not only an option).")
        .def_prop_rw("options", [](const BoundComboBox& h) { return h.state().options; },
                     [](BoundComboBox& h, std::vector<std::string> v) {
                         h.modify([v = std::move(v)](ComboBox& c) { c.options = v; });
                     });
    bind_widget_value<BoundPopup, bool>(m, "BoundPopup",
        "Bound handle for a Model's popup (place its widgets with vb.popup(handle, fn)): .value "
        "and .open are whether it is open.")
        .def_prop_rw("open", &BoundPopup::get, &validated_set<BoundPopup, bool>);

    bind_menu<BoundMenu>(m, "BoundMenu",
        "Bound handle for a Model's menu: on_choice(cb) runs cb(label) each time the user picks an "
        "item (click, keys or shortcut); .value counts those choices.");
    bind_menu<BoundContextMenu>(m, "BoundContextMenu",
        "Bound handle for a Model's right-click menu (place it with vb.context_menu(handle)); as "
        "BoundMenu.");

    bind_widget_value<BoundLabel, std::string>(m, "BoundLabel",
        "Bound handle for a Model's label: .value is its text.")
        .def_prop_ro("heading", [](const BoundLabel& h) { return h.state().heading; });
    bind_widget_value<BoundTextArea, std::string>(m, "BoundTextArea",
        "Bound handle for a Model's text area: .value is its text.")
        .def_prop_ro("read_only", [](const BoundTextArea& h) { return h.state().read_only; })
        .def_prop_ro("rows", [](const BoundTextArea& h) { return h.state().rows; });
    bind_widget_value<BoundTabs, std::int64_t>(m, "BoundTabs",
        "Bound handle for a Model's tab bar (place it with vb.tabs(handle, pages)): .value is the "
        "selected tab's index.");
    bind_widget_value<BoundDialog, bool>(m, "BoundDialog",
        "Bound handle for a Model's modal dialog (place it with vb.dialog(handle)): .open shows it; "
        "on_choice(cb) runs cb(button) when the user picks a button.")
        .def_prop_rw("open", &BoundDialog::get, &validated_set<BoundDialog, bool>)
        .def_prop_ro("chosen", [](const BoundDialog& h) -> std::optional<std::string> {
            auto d = h.state();
            if (!d.chosen || *d.chosen >= d.buttons.size()) return std::nullopt;
            return d.buttons[*d.chosen];
        }, "The button the user picked last, or None (closed with Escape, or not answered yet).")
        .def("on_choice", [](nb::object self, nb::callable cb) {
            auto& h = nb::cast<BoundDialog&>(self);
            if (!h.field) throw nb::value_error("on_choice(): handle is not bound to a Model");
            auto* f = h.field;
            auto last = std::make_shared<std::uint64_t>(f->get().activations);
            auto conn = f->on_change().connect([wc = WeakCallback(cb), last](const Dialog& d) {
                if (d.activations == *last) return;
                *last = d.activations;
                if (d.chosen && *d.chosen < d.buttons.size()) wc(d.buttons[*d.chosen]);
            });
            if (h.owner) conn.keep_alive(h.owner);
            return keep_connection(self, std::move(conn), cb);
        }, nb::arg("callback"), "Runs callback(button) on the logic thread when the user picks a button.");

    nb::class_<BoundDockLayout>(m, "BoundDockLayout", nb::dynamic_attr(), nb::is_weak_referenceable(),
        "Bound handle for a Model's dock layout (prism.dock_layout): .value is the layout as nested "
        "dicts; the user's moves, split sizes and tab choices show up in it. Place the dock with "
        "vb.dock(handle, panels).")
        .def_prop_rw("value", [](const BoundDockLayout& h) { return dock_to_py(h.get().root); },
                     [](BoundDockLayout& h, nb::handle v) {
                         auto root = dock_from_py(v);
                         h.modify([root = std::move(root)](DockLayout& l) { l.root = root; });   // floating stays
                     },
                     "The docked panels' arrangement. Any thread. Writes are posted to the logic thread.")
        .def_prop_rw("floating", [](const BoundDockLayout& h) { return h.get().floating; },
                     [](BoundDockLayout& h, std::vector<std::string> v) {
                         h.modify([v = std::move(v)](DockLayout& l) { l.floating = v; });
                     },
                     "The panels in windows of their own (desktop). Any thread; writes are posted.");

    bind_list<int>(m, "Int");
    bind_list<double>(m, "Float");
    bind_list<std::string>(m, "Str");
    // List<bool>/BoundList<bool> disabled — vector<bool> proxy incompatible with const T& Signal; use int list for bool data
}

static void bind_plot(nb::module_& m) {
    nb::class_<BoundDocument>(m, "BoundDocument", nb::dynamic_attr(), nb::is_weak_referenceable(),
                              "Bound handle for a Model's document field (prism.document()). Any "
                              "thread: reads block until the logic thread answers, edits are posted.")
        .def_prop_ro("text", &BoundDocument::text, "The whole text, lines joined by newlines.")
        .def("set_text", &BoundDocument::set_text, nb::arg("text"), nb::arg("markdown") = false,
             "Replace the text, as a file opened: no undo back to what was there. markdown=True "
             "reads it as Markdown (headings, lists, emphasis...).")
        .def("to_markdown", &BoundDocument::to_markdown, "The document written as Markdown.")
        .def("find_all", &BoundDocument::find_all, nb::arg("needle"), nb::arg("case_sensitive") = true,
             nb::arg("whole_word") = false, "Byte offsets (start, end) of every match, in order.")
        .def("replace_all", &BoundDocument::replace_all, nb::arg("needle"), nb::arg("with_"),
             nb::arg("case_sensitive") = true, nb::arg("whole_word") = false,
             "Replace every match, as one undo step. Returns how many.")
        .def("observe", [](nb::object self, nb::callable cb) {
            return keep_connection(self, nb::cast<BoundDocument&>(self).observe(cb), cb);
        }, nb::arg("callback"), kObserveDoc);
#ifdef PRISM_PYTHON_MARKDOWN
    m.attr("has_markdown") = true;
#else
    m.attr("has_markdown") = false;
#endif

    nb::class_<BoundPlot>(m, "BoundPlot", nb::dynamic_attr(), nb::is_weak_referenceable(),
                          "Bound handle for a Model's plot field. All mutation methods may "
                          "be called from any thread — each is one posted operation on the "
                          "logic thread (replace_series is a single post, so concurrent "
                          "calls never leave the plot mid-update); series_count/series_len "
                          "are dispatched reads that block until the logic thread answers.")
        .def("add_series", &BoundPlot::add_series, nb::arg("xs"), nb::arg("ys"), nb::arg("color")="", nb::arg("thickness")=2.f, nb::arg("fill")=false,
             "Append a series. color is '#rrggbb' (malformed strings raise ValueError at the call site).")
        .def("clear_series", &BoundPlot::clear_series, "Remove all series.")
        .def("notify", &BoundPlot::notify, "Retrigger a redraw after mutating series in place.")
        .def("replace_series", &BoundPlot::replace_series, nb::arg("xs"), nb::arg("ys") = nb::none(), nb::kw_only(),
             nb::arg("color") = nb::none(), nb::arg("thickness") = 2.f, nb::arg("fill") = false,
             "Atomically replace all series: replace_series(xs, ys) for one series, or "
             "replace_series([(xs, ys, color), ...]) for several.")
        .def("set_labels", &BoundPlot::set_labels, nb::kw_only(), nb::arg("x") = nb::none(), nb::arg("y") = nb::none(),
             "Set axis labels; None leaves the axis unchanged.")
        .def("series_count", &BoundPlot::series_count, "Dispatched read. Number of series.")
        .def("series_len", &BoundPlot::series_len, nb::arg("i"), "Dispatched read. Point count of series i.")
        .def("reset_view", &BoundPlot::reset_view, "Reset the viewport to fit all series.")
        .def_prop_rw("x_label", &BoundPlot::get_x_label, &BoundPlot::set_x_label, "X axis label.")
        .def_prop_rw("y_label", &BoundPlot::get_y_label, &BoundPlot::set_y_label, "Y axis label.");
    nb::class_<PlotHandle>(m, "PlotHandle", nb::dynamic_attr(), nb::is_weak_referenceable(),
                           "Standalone plot (owns its state) — for quick tests and non-model "
                           "usage. Same methods and thread rules as BoundPlot.")
        .def(nb::init<>())
        .def("add_series", &PlotHandle::add_series, nb::arg("xs"), nb::arg("ys"), nb::arg("color")="", nb::arg("thickness")=2.f, nb::arg("fill")=false,
             "Append a series. color is '#rrggbb' (malformed strings raise ValueError at the call site).")
        .def("clear_series", &PlotHandle::clear_series, "Remove all series.")
        .def("notify", &PlotHandle::notify, "Retrigger a redraw after mutating series in place.")
        .def("replace_series", &PlotHandle::replace_series, nb::arg("xs"), nb::arg("ys") = nb::none(), nb::kw_only(),
             nb::arg("color") = nb::none(), nb::arg("thickness") = 2.f, nb::arg("fill") = false,
             "Atomically replace all series: replace_series(xs, ys) for one series, or "
             "replace_series([(xs, ys, color), ...]) for several.")
        .def("set_labels", &PlotHandle::set_labels, nb::kw_only(), nb::arg("x") = nb::none(), nb::arg("y") = nb::none(),
             "Set axis labels; None leaves the axis unchanged.")
        .def("series_count", &PlotHandle::series_count, "Dispatched read. Number of series.")
        .def("series_len", &PlotHandle::series_len, nb::arg("i"), "Dispatched read. Point count of series i.")
        .def("reset_view", &PlotHandle::reset_view, "Reset the viewport to fit all series.")
        .def_prop_rw("x_label", &PlotHandle::get_x_label, &PlotHandle::set_x_label, "X axis label.")
        .def_prop_rw("y_label", &PlotHandle::get_y_label, &PlotHandle::set_y_label, "Y axis label.");
}

static void bind_tree_table(nb::module_& m) {
    g_tree_row_type = new nb::object(
        nb::module_::import_("collections").attr("namedtuple")(
            "TreeRow",
            nb::make_tuple("label", "depth", "has_children", "expanded", "selected", "checked")));
    // namedtuple() infers __module__ from the calling frame — meaningless from
    // C++; point it at the public package instead.
    nb::setattr(*g_tree_row_type, "__module__", nb::str("prism"));
    m.attr("TreeRow") = *g_tree_row_type;

    nb::class_<BoundTable>(m, "BoundTable", nb::dynamic_attr(), nb::is_weak_referenceable(),
                           "Bound handle for a Model's table over a Python object "
                           "(prism.table_field): place it with vb.table(handle).")
        .def("refresh", &BoundTable::refresh,
             "The source changed: the table reads it again. Any thread; posted to the logic thread.")
        .def_prop_rw("columns", &BoundTable::get_columns, &BoundTable::set_columns,
                     "The columns shown, by source index, left to right ([]: all). Dragging a "
                     "header rewrites it; setting it hides or reorders columns. Any thread.");

    nb::class_<BoundTree>(m, "BoundTree", nb::dynamic_attr(), nb::is_weak_referenceable(),
                          "Bound handle for a Model's tree field. refresh() may be called "
                          "from any thread (posted to the logic thread); rows() is a "
                          "dispatched read that blocks until the logic thread answers.")
        .def("refresh", &BoundTree::refresh,
             "Any thread; posted to the logic thread. Re-pulls rows from the tree source.")
        .def_prop_rw("selected", &BoundTree::get_selected, &BoundTree::set_selected,
                     "The current node's id (None: none). Setting it selects that node alone. Any thread.")
        .def_prop_ro("selection", &BoundTree::selection,
                     "Every selected node's id, in the order the rows show (Ctrl/Shift-click, Shift+arrows).")
        .def("observe_selection", [](nb::object self, nb::callable cb) {
            return keep_connection(self, nb::cast<BoundTree&>(self).observe_selection(cb), cb);
        }, nb::arg("callback"), kObserveDoc)
        .def("rows", &BoundTree::rows,
             "Any thread; dispatched read. Returns the visible rows as a list of TreeRow "
             "namedtuples (label, depth, has_children, expanded, selected).");
    nb::class_<TreeHandle>(m, "TreeHandle", nb::dynamic_attr(), nb::is_weak_referenceable(),
                           "Standalone tree controller (owns its source) — for quick tests "
                           "and non-model usage. Same thread rules as BoundTree.")
        .def(nb::init<nb::object>(), nb::arg("source"))
        .def("refresh", [](TreeHandle& h){ auto* p=h.ctrl.get(); list_op_dispatch(h.ctrl, [p](){ p->refresh(); }); },
             "Any thread; posted to the logic thread. Re-pulls rows from the tree source.")
        .def("rows", [](TreeHandle& h){
            auto* p = h.ctrl.get();
            auto snapshot = dispatch_sync_read<std::vector<prism::ui::TreeRow>>([p](){ return snapshot_tree_rows(p); });
            return tree_rows_to_pylist(snapshot);
         },
         "Any thread; dispatched read. Returns the visible rows as a list of TreeRow "
         "namedtuples (label, depth, has_children, expanded, selected).");
}

static void bind_view(nb::module_& m) {
    nb::class_<ViewBuilder>(m, "ViewBuilder",
                            "Passed to Model.view(self, vb); each call appends a widget to "
                            "the view being built. All methods run on the logic thread "
                            "during view (re)builds.")
        .def("popup", [](ViewBuilder& vb, nb::object h, nb::callable content){
                if (!nb::isinstance<BoundPopup>(h)) throw nb::type_error("ViewBuilder.popup: needs a prism.popup() handle");
                auto& b = nb::cast<BoundPopup&>(h);
                if (!b.field) throw nb::value_error("ViewBuilder.popup: handle is not bound to a Model");
                // Runs now (to learn the placed fields) and on each opening, on the logic thread:
                // take the GIL; PyHolder releases the callable safely from any thread.
                auto holder = std::make_shared<PyHolder>(content);
                vb.popup(*b.field, [holder](ViewBuilder& inner) {
                    nb::gil_scoped_acquire gil;
                    try { holder->obj(nb::cast(&inner, nb::rv_policy::reference)); }
                    catch (...) { report_python_callback_error(); }
                });
            }, nb::arg("handle"), nb::arg("content"),
             "A popup holding the widgets content(vb) places; built each time it opens.")
        .def("widget", [](ViewBuilder& vb, nb::object h, nb::object enabled, nb::object drag,
                          nb::object drop, nb::object accepts) {
                auto place = [&] {
                    if (drag.is_none() && drop.is_none()) return py_widget_dispatch(vb, h);
                    vb.with_drag(py_drag_role(drag, drop, accepts), [&] { py_widget_dispatch(vb, h); });
                };
                if (enabled.is_none()) place();
                else py_enabled_if(vb, enabled, place);
            }, nb::arg("handle"), nb::arg("enabled") = nb::none(), nb::arg("drag") = nb::none(),
             nb::arg("drop") = nb::none(), nb::arg("accepts") = nb::none(),
             "Add the widget for a bound scalar/shared/derived/slider/checkbox/list handle. "
             "enabled: a bool field, derived bool or checkbox; while false the widget is greyed out "
             "and takes no input. drag: a callable giving the Python object a drag from this "
             "widget carries (None: nothing to drag). drop: callable(value, x, y) for objects "
             "dropped here, of type accepts (a type or tuple of types; any object if None).")
        .def("table", [](ViewBuilder& vb, nb::object h, nb::object sort_column, bool descending) {
                if (!nb::isinstance<BoundTable>(h)) throw nb::type_error("ViewBuilder.table: needs a prism.table_field() handle");
                auto* s = nb::cast<BoundTable&>(h).slot;
                if (!s) throw nb::value_error("ViewBuilder.table: handle is not bound to a Model");
                auto t = vb.table(s->data);
                std::move(t).depends_on(s->revision);
                std::move(t).columns(s->columns);
                if (!sort_column.is_none()) std::move(t).sort(nb::cast<std::size_t>(sort_column), descending);
            }, nb::arg("handle"), nb::arg("sort_column") = nb::none(), nb::arg("descending") = false,
             "A table over the handle's Python source; sorted by sort_column if given (the user can "
             "click a header to sort too).")
        .def("tabs", [](ViewBuilder& vb, nb::object h, nb::list pages) {
                if (!nb::isinstance<BoundTabs>(h)) throw nb::type_error("ViewBuilder.tabs: needs a prism.tabs() handle");
                std::vector<std::pair<std::string, std::shared_ptr<PyHolder>>> built;
                for (auto item : pages) {
                    auto t = nb::cast<nb::tuple>(item);
                    if (t.size() != 2) throw nb::type_error("ViewBuilder.tabs: each page is (title, fn)");
                    built.push_back({nb::cast<std::string>(t[0]), std::make_shared<PyHolder>(nb::borrow<nb::object>(t[1]))});
                }
                vb.tabs(*nb::cast<BoundTabs&>(h).field, [&] {
                    for (auto& [title, holder] : built)
                        // Built on the logic thread when the page is shown: take the GIL there.
                        vb.tab(title, [holder](ViewBuilder& inner) {
                            nb::gil_scoped_acquire gil;
                            try { holder->obj(nb::cast(&inner, nb::rv_policy::reference)); }
                            catch (...) { report_python_callback_error(); }
                        });
                });
            }, nb::arg("handle"), nb::arg("pages"),
             "Tabs: pages, a list of (title, fn) where fn(vb) places the page's widgets; the handle's "
             ".value is the selected page.")
        .def("dialog", [](ViewBuilder& vb, nb::object h, nb::object content) {
                if (!nb::isinstance<BoundDialog>(h)) throw nb::type_error("ViewBuilder.dialog: needs a prism.dialog() handle");
                auto& d = *nb::cast<BoundDialog&>(h).field;
                if (content.is_none()) return vb.dialog(d);
                auto holder = std::make_shared<PyHolder>(std::move(content));
                vb.dialog(d, [holder](ViewBuilder& inner) {
                    nb::gil_scoped_acquire gil;
                    try { holder->obj(nb::cast(&inner, nb::rv_policy::reference)); }
                    catch (...) { report_python_callback_error(); }
                });
            }, nb::arg("handle"), nb::arg("content") = nb::none(),
             "A modal dialog, shown when the handle's .open is True; content(vb), if given, places a "
             "form between its message and its buttons.")
        .def("section", [](ViewBuilder& vb, nb::object h, nb::object content) {
                if (!nb::isinstance<BoundExpander>(h)) throw nb::type_error("ViewBuilder.section: needs a prism.expander() handle");
                auto holder = std::make_shared<PyHolder>(std::move(content));
                // Built on the logic thread when the section opens: take the GIL there.
                vb.section(*nb::cast<BoundExpander&>(h).field, [holder](ViewBuilder& inner) {
                    nb::gil_scoped_acquire gil;
                    try { holder->obj(nb::cast(&inner, nb::rv_policy::reference)); }
                    catch (...) { report_python_callback_error(); }
                });
            }, nb::arg("handle"), nb::arg("content"),
             "A section the user folds and unfolds: the handle's header, then content(vb), built only "
             "while it is open.")
        .def("dock", [](ViewBuilder& vb, nb::object h, nb::list panels) {
                if (!nb::isinstance<BoundDockLayout>(h))
                    throw nb::type_error("ViewBuilder.dock: needs a prism.dock_layout() handle");
                std::vector<ViewBuilder::DockPanel> built;
                for (auto item : panels) {
                    auto t = nb::cast<nb::tuple>(item);
                    if (t.size() != 3) throw nb::type_error("ViewBuilder.dock: each panel is (name, title, fn)");
                    // Built on the logic thread when the dock (re)builds: take the GIL there.
                    auto holder = std::make_shared<PyHolder>(nb::borrow<nb::object>(t[2]));
                    built.push_back({nb::cast<std::string>(t[0]), nb::cast<std::string>(t[1]),
                                     [holder](ViewBuilder& inner) {
                                         nb::gil_scoped_acquire gil;
                                         try { holder->obj(nb::cast(&inner, nb::rv_policy::reference)); }
                                         catch (...) { report_python_callback_error(); }
                                     }});
                }
                vb.dock(*nb::cast<BoundDockLayout&>(h).field, std::move(built));
            }, nb::arg("handle"), nb::arg("panels"),
             "A dock: panels, a list of (name, title, fn) where fn(vb) places the panel's widgets, "
             "arranged as the layout handle says. The user drags tabs to move panels.")
        .def("context_menu", [](ViewBuilder& vb, nb::object h) {
                if (!nb::isinstance<BoundContextMenu>(h))
                    throw nb::type_error("ViewBuilder.context_menu: needs a prism.context_menu() handle");
                vb.context_menu(*nb::cast<BoundContextMenu&>(h).field);
            }, nb::arg("handle"),
             "A right click anywhere in the current container opens this menu at the pointer.")
        .def("enabled_if", [](ViewBuilder& vb, nb::object flag, nb::callable fn) {
                py_enabled_if(vb, flag, [&] { fn(); });
            }, nb::arg("flag"), nb::arg("fn"),
             "Everything fn() places is greyed out and takes no input while flag (a bool field, "
             "derived bool or checkbox) is false.")
        .def("list", [](ViewBuilder& vb, nb::object h, bool reorderable, bool selectable, bool follow_tail){
                py_list_dispatch(vb, h, reorderable, selectable, follow_tail);
             },
             nb::arg("handle"), nb::arg("reorderable") = false, nb::arg("selectable") = false,
             nb::arg("follow_tail") = false,
             "Add a list widget for a bound list handle (list_field()). reorderable: the user "
             "can drag rows to another place. selectable: a click or the arrow keys select a row, "
             "read and set through handle.selected (None: no row). follow_tail: rows appended while "
             "the last row shows scroll to the new last row, as in a terminal; scrolling up "
             "stops that until the user scrolls back down.")
        .def("text_editor", [](ViewBuilder& vb, nb::object h, bool rich, bool read_only, int rows) {
                auto handle = vb.text_editor(as_document(h, "ViewBuilder.text_editor").bound(),
                                             {.read_only = read_only, .rich = rich});
                if (rows > 0) std::move(handle).rows(rows);
            }, nb::arg("handle"), nb::arg("rich") = false, nb::arg("read_only") = false, nb::arg("rows") = 0,
             "Edit a document field (prism.document()): a caret, selection, undo, the mouse. rich: "
             "Ctrl+B/I/E style the selection, \"# \" or \"- \" at a line's start makes a heading or a "
             "list item. rows: so many lines tall (inside a scroll area); 0 fills the space.")
        .def("code_editor", [](ViewBuilder& vb, nb::object h, std::string language, int rows) {
                auto handle = vb.code_editor(as_document(h, "ViewBuilder.code_editor").bound(), language);
                if (rows > 0) std::move(handle).rows(rows);
            }, nb::arg("handle"), nb::arg("language") = "", nb::arg("rows") = 0,
             "Edit a document field as code: line numbers, the language's colors (\"python\", "
             "\"cpp\", \"sql\", \"json\", \"toml\", \"shell\"), Tab to indent, matching brackets.")
        .def("text_view", [](ViewBuilder& vb, nb::object h, int rows) {
                auto handle = vb.text_view(as_document(h, "ViewBuilder.text_view").bound());
                if (rows > 0) std::move(handle).rows(rows);
            }, nb::arg("handle"), nb::arg("rows") = 0,
             "Show a document field, read-only: only the lines in sight are laid out.")
        .def("canvas", [](ViewBuilder& vb, nb::object h){ py_canvas_dispatch(vb, h); }, nb::arg("handle"),
             "Add a canvas widget for a bound plot handle (plot_field()).")
        .def("tree", [](ViewBuilder& vb, nb::object h){ py_tree_dispatch(vb, h); }, nb::arg("handle"),
             "Add a tree widget for a bound tree handle (tree_field()).")
        .def("hstack", [](ViewBuilder& vb, nb::args args){
            // hstack(handle1, handle2, ...) -> Row container with those widgets
            // single callable arg -> container with callable body (for lambda capturing vb)
            if (args.size() == 1 && nb::isinstance<nb::callable>(args[0])) {
                auto fn = nb::cast<nb::callable>(args[0]);
                vb.hstack([&]{ fn(); });
                return;
            }
            vb.hstack([&]{
                for (auto a : args) py_widget_dispatch(vb, nb::cast<nb::object>(a));
            });
        }, "Row container. hstack(h1, h2, ...) places those handles' widgets side by side; "
           "hstack(fn) with a single callable runs fn() inside the row's scope instead — "
           "for a lambda capturing vb that mixes in further vb.* calls.")
        .def("toolbar", [](ViewBuilder& vb, nb::callable fn) { vb.toolbar([&] { fn(); }); }, nb::arg("fn"),
             "A row of tools across the window (buttons, toggles), on the window's chrome colour.")
        .def("status_bar", [](ViewBuilder& vb, nb::callable fn) { vb.status_bar([&] { fn(); }); }, nb::arg("fn"),
             "A row of short status texts, usually last in the window.")
        .def("group", [](ViewBuilder& vb, std::string title, nb::callable fn) { vb.group(std::move(title), [&] { fn(); }); },
             nb::arg("title"), nb::arg("fn"), "Widgets that belong together, outlined, under a title.")
        .def("vstack", [](ViewBuilder& vb, nb::args args){
            if (args.size() == 1 && nb::isinstance<nb::callable>(args[0])) {
                auto fn = nb::cast<nb::callable>(args[0]);
                vb.vstack([&]{ fn(); });
                return;
            }
            vb.vstack([&]{
                for (auto a : args) py_widget_dispatch(vb, nb::cast<nb::object>(a));
            });
        }, "Column container — same dual calling convention as hstack(): vstack(h1, h2, ...) "
           "stacks the handles' widgets vertically, vstack(fn) runs fn() in the column's scope.");
}

static void bind_model(nb::module_& m) {
    nb::class_<PyModel>(m, "Model", nb::type_slots(pymodel_type_slots))
        .def(nb::init<>())
        .def("_set_view_callback", &PyModel::set_view_callback, nb::arg("callback"))
        .def("_set_tooltip_internal", &PyModel::set_tooltip, nb::arg("handle"), nb::arg("text"))
        // Single internal allocator set — deduped (public add_* was duplicate dead code)
        .def("_add_int_internal", [](PyModel& self, int v){
                auto [owner, p] = self.add_int_slot(v);
                BoundField<int> h; h.owner = std::move(owner); h.field = p; return h;
            }, nb::arg("value")=0)
        .def("_add_float_internal", [](PyModel& self, double v){
                auto [owner, p] = self.add_float_slot(v);
                BoundField<double> h; h.owner = std::move(owner); h.field = p; return h;
            }, nb::arg("value")=0.0)
        .def("_add_str_internal", [](PyModel& self, std::string v){
                auto [owner, p] = self.add_str_slot(std::move(v));
                BoundField<std::string> h; h.owner = std::move(owner); h.field = p; return h;
            }, nb::arg("value")="")
        .def("_add_bool_internal", [](PyModel& self, bool v){
                auto [owner, p] = self.add_bool_slot(v);
                BoundField<bool> h; h.owner = std::move(owner); h.field = p; return h;
            }, nb::arg("value")=false)
        .def("_add_slider_internal", [](PyModel& self, double v, double mn, double mx, std::string orientation, double step,
                                         std::string label, std::string unit){
                bool vertical;
                if (orientation == "horizontal") vertical = false;
                else if (orientation == "vertical") vertical = true;
                else throw nb::value_error(("slider(): orientation must be 'horizontal' or 'vertical', got '" + orientation + "'").c_str());
                auto alloc = self.add_slider_slot(v, mn, mx, vertical, step, std::move(label), std::move(unit));
                BoundSliderValue h; h.owner = std::move(alloc.owner); h.field_h = alloc.field_h; h.field_v = alloc.field_v; return h;
            }, nb::arg("value")=0.0, nb::arg("min")=0.0, nb::arg("max")=1.0, nb::arg("orientation")="horizontal",
               nb::arg("step")=0.0, nb::arg("label")="", nb::arg("unit")="")
        .def("_add_checkbox_internal", [](PyModel& self, bool v, std::string label){
                auto [owner, p] = self.add_checkbox_slot(v, label);
                BoundCheckboxValue h; h.owner = std::move(owner); h.field = p; h.label = std::move(label); return h;
            }, nb::arg("value")=false, nb::arg("label")="")
        .def("_add_expander_internal", [](PyModel& self, std::string title, bool open){
                auto [owner, p] = self.add_widget_slot(Expander{.title = std::move(title), .open = open});
                return make_bound_expander(std::move(owner), p);
            }, nb::arg("title"), nb::arg("open"))
        .def("_add_link_internal", [](PyModel& self, std::string text, std::string url){
                auto [owner, p] = self.add_widget_slot(Link{.text = std::move(text), .url = std::move(url)});
                return make_bound_link(std::move(owner), p);
            }, nb::arg("text"), nb::arg("url"))
        .def("_add_color_internal", [](PyModel& self, std::string hex){
                auto c = prism::ui::color_detail::parse_hex(hex);
                if (!c) throw nb::value_error("color: expected \"#RRGGBB\"");
                auto [owner, p] = self.add_widget_slot(*c);
                return make_bound_color(std::move(owner), p);
            }, nb::arg("hex"))
        .def("_add_date_internal", [](PyModel& self, std::string iso){
                auto d = prism::ui::date_detail::parse_iso(iso);
                if (!d) throw nb::value_error("date: expected a real \"YYYY-MM-DD\"");
                auto [owner, p] = self.add_widget_slot(*d);
                return make_bound_date(std::move(owner), p);
            }, nb::arg("iso"))
        .def("_add_time_internal", [](PyModel& self, std::string text){
                auto t = prism::ui::time_detail::parse(text);
                if (!t) throw nb::value_error("time_of_day: expected \"HH:MM\"");
                auto [owner, p] = self.add_widget_slot(*t);
                return make_bound_time(std::move(owner), p);
            }, nb::arg("text"))
        .def("_add_picture_internal", [](PyModel& self, std::optional<std::string> path,
                                         std::optional<std::pair<float, float>> size, std::string description){
                Picture pic{.description = std::move(description)};
                if (size) pic.size = Size{Width{size->first}, Height{size->second}};
                if (path) pic.image = picture_image(*path, pic.size);
                auto [owner, p] = self.add_widget_slot(std::move(pic));
                return make_bound_picture(std::move(owner), p);
            }, nb::arg("path").none(), nb::arg("size").none(), nb::arg("description"))
        .def("_add_choice_internal", [](PyModel& self, std::optional<std::string> v, std::vector<std::string> options,
                                        std::string placeholder){
                Choice c{.options = std::move(options), .placeholder = std::move(placeholder)};
                c.selected = option_index(c.options, v);
                auto [owner, p] = self.add_widget_slot(std::move(c));
                return make_bound_choice(std::move(owner), p);
            }, nb::arg("value").none(), nb::arg("options"), nb::arg("placeholder"))
        .def("_add_radio_internal", [](PyModel& self, std::string v, std::vector<std::string> options){
                auto it = std::ranges::find(options, v);
                if (it == options.end()) throw nb::value_error(("radio(): '" + v + "' is not one of the options").c_str());
                RadioGroup g{.selected = static_cast<std::size_t>(it - options.begin()), .options = std::move(options)};
                auto [owner, p] = self.add_widget_slot(std::move(g));
                return make_bound_radio(std::move(owner), p);
            }, nb::arg("value"), nb::arg("options"))
        .def("_add_spinbox_int_internal", [](PyModel& self, std::int64_t v, std::int64_t mn, std::int64_t mx, std::int64_t step){
                auto [owner, p] = self.add_widget_slot(SpinBox<std::int64_t>{.value = v, .min = mn, .max = mx, .step = step});
                return make_bound_spinbox<std::int64_t>(std::move(owner), p);
            }, nb::arg("value"), nb::arg("min"), nb::arg("max"), nb::arg("step"))
        .def("_add_spinbox_float_internal", [](PyModel& self, double v, double mn, double mx, double step){
                auto [owner, p] = self.add_widget_slot(SpinBox<double>{.value = v, .min = mn, .max = mx, .step = step});
                return make_bound_spinbox<double>(std::move(owner), p);
            }, nb::arg("value"), nb::arg("min"), nb::arg("max"), nb::arg("step"))
        .def("_add_progress_internal", [](PyModel& self, double v, double mn, double mx, std::string label, bool busy){
                auto [owner, p] = self.add_widget_slot(ProgressBar{.value = v, .min = mn, .max = mx, .label = std::move(label), .busy = busy});
                return make_bound_progress(std::move(owner), p);
            }, nb::arg("value"), nb::arg("min"), nb::arg("max"), nb::arg("label"), nb::arg("busy") = false)
        .def("_add_button_internal", [](PyModel& self, std::string text, std::optional<std::string> icon, std::string keys,
                                        std::optional<bool> checked){
                std::shared_ptr<const prism::render::Image> img;
                if (icon) {
                    auto loaded = prism::render::load_image(*icon, Size{Width{16}, Height{16}});
                    if (!loaded) throw nb::value_error(("button icon: " + loaded.error()).c_str());
                    img = *loaded;
                }
                prism::ui::Shortcut shortcut{};
                if (!keys.empty()) {
                    auto s = prism::ui::parse_shortcut(keys);
                    if (!s) throw nb::value_error(("button \"" + text + "\": unknown shortcut \"" + keys + "\"").c_str());
                    shortcut = *s;
                }
                auto [owner, p] = self.add_widget_slot(Button{.text = std::move(text), .click_count = 0, .icon = std::move(img),
                                                              .shortcut = shortcut, .checked = checked});
                return make_bound_button(std::move(owner), p);
            }, nb::arg("text"), nb::arg("icon") = nb::none(), nb::arg("shortcut") = "",
               nb::arg("checked") = nb::none())
        .def("_add_combobox_internal", [](PyModel& self, std::string v, std::vector<std::string> options, std::string placeholder){
                auto [owner, p] = self.add_widget_slot(ComboBox{.value = std::move(v), .placeholder = std::move(placeholder),
                                                                .max_length = 0, .options = std::move(options)});
                return make_bound_combobox(std::move(owner), p);
            }, nb::arg("value"), nb::arg("options"), nb::arg("placeholder"))
        .def("_add_menu_internal", [](PyModel& self, std::string title, std::vector<nb::dict> items){
                auto [owner, p] = self.add_widget_slot(Menu{.title = std::move(title), .items = py_menu_items(items)});
                return make_bound_menu(std::move(owner), p);
            }, nb::arg("title"), nb::arg("items"))
        .def("_add_context_menu_internal", [](PyModel& self, std::vector<nb::dict> items){
                auto [owner, p] = self.add_widget_slot(ContextMenu{.items = py_menu_items(items)});
                return make_bound_menu(std::move(owner), p);
            }, nb::arg("items"))
        .def("_add_label_internal", [](PyModel& self, std::string text, bool heading, bool wrap){
                auto [owner, p] = self.add_widget_slot(Label<std::string>{.value = std::move(text), .heading = heading,
                                                                          .wrap = wrap});
                return make_bound_text<BoundLabel>(std::move(owner), p);
            }, nb::arg("text"), nb::arg("heading"), nb::arg("wrap") = false)
        .def("_add_text_area_internal", [](PyModel& self, std::string text, std::size_t rows, bool read_only,
                                           std::string placeholder){
                auto [owner, p] = self.add_widget_slot(TextArea<std::string>{
                    .value = std::move(text), .placeholder = std::move(placeholder), .rows = rows,
                    .read_only = read_only, .font = read_only ? FontRole::Ui : FontRole::Mono});
                return make_bound_text<BoundTextArea>(std::move(owner), p);
            }, nb::arg("text"), nb::arg("rows"), nb::arg("read_only"), nb::arg("placeholder"))
        .def("_add_tabs_internal", [](PyModel& self, std::int64_t selected){
                auto [owner, p] = self.add_widget_slot(TabBar<>{.selected = static_cast<std::size_t>(selected)});
                BoundTabs h;
                h.owner = std::move(owner);
                h.field = p;
                h.read = [](const TabBar<>& t) { return static_cast<std::int64_t>(t.selected); };
                h.write = [](TabBar<>& t, const std::int64_t& v) { t.selected = static_cast<std::size_t>(std::max<std::int64_t>(0, v)); };
                return h;
            }, nb::arg("selected"))
        .def("_add_dialog_internal", [](PyModel& self, std::string title, std::string message, std::vector<std::string> buttons){
                auto [owner, p] = self.add_widget_slot(Dialog{.title = std::move(title), .message = std::move(message),
                                                              .buttons = std::move(buttons)});
                BoundDialog h;
                h.owner = std::move(owner);
                h.field = p;
                h.read = [](const Dialog& d) { return d.open; };
                h.write = [](Dialog& d, const bool& v) {
                    if (v && !d.open) d.chosen.reset();   // a new question: no answer yet
                    d.open = v;
                };
                return h;
            }, nb::arg("title"), nb::arg("message"), nb::arg("buttons"))
        .def("_add_dock_layout_internal", [](PyModel& self, nb::handle value){
                auto [owner, p] = self.add_widget_slot(DockLayout{dock_from_py(value)});
                BoundDockLayout h;
                h.owner = std::move(owner);
                h.field = p;
                h.read = [](const DockLayout& l) { return l; };
                h.write = [](DockLayout& l, const DockLayout& v) { l = v; };
                return h;
            }, nb::arg("value"))
        .def("_add_popup_internal", [](PyModel& self, bool modal){
                auto [owner, p] = self.add_widget_slot(Popup{.open = false, .modal = modal});
                return make_bound_popup(std::move(owner), p);
            }, nb::arg("modal"))
        // Shared internal allocators
        .def("_add_shared_int_internal", [](PyModel& self, int v){
                auto [owner, p] = self.add_shared_int_slot(v);
                BoundShared<int> h; h.owner = std::move(owner); h.shared = p; return h;
            }, nb::arg("value")=0)
        .def("_add_shared_float_internal", [](PyModel& self, double v){
                auto [owner, p] = self.add_shared_float_slot(v);
                BoundShared<double> h; h.owner = std::move(owner); h.shared = p; return h;
            }, nb::arg("value")=0.0)
        .def("_add_shared_str_internal", [](PyModel& self, std::string v){
                auto [owner, p] = self.add_shared_str_slot(std::move(v));
                BoundShared<std::string> h; h.owner = std::move(owner); h.shared = p; return h;
            }, nb::arg("value")="")
        .def("_add_shared_bool_internal", [](PyModel& self, bool v){
                auto [owner, p] = self.add_shared_bool_slot(v);
                BoundShared<bool> h; h.owner = std::move(owner); h.shared = p; return h;
            }, nb::arg("value")=false)
        .def("_add_channel_int_internal", [](PyModel& self){
                auto [owner, p] = self.add_channel_int_slot();
                BoundChannel<int> h; h.owner = std::move(owner); h.channel = p; return h;
            })
        .def("_add_channel_float_internal", [](PyModel& self){
                auto [owner, p] = self.add_channel_float_slot();
                BoundChannel<double> h; h.owner = std::move(owner); h.channel = p; return h;
            })
        .def("_add_channel_str_internal", [](PyModel& self){
                auto [owner, p] = self.add_channel_str_slot();
                BoundChannel<std::string> h; h.owner = std::move(owner); h.channel = p; return h;
            })
        .def("_add_channel_bool_internal", [](PyModel& self){
                auto [owner, p] = self.add_channel_bool_slot();
                BoundChannel<bool> h; h.owner = std::move(owner); h.channel = p; return h;
            })
        .def("_add_derived_int_internal", [](PyModel& self, nb::object fn, std::string name, nb::args deps){
                std::vector<nb::object> v; v.reserve(deps.size());
                for (size_t i=0;i<deps.size();++i) v.push_back(nb::cast<nb::object>(deps[i]));
                auto [owner, p] = self.add_derived_slot_vec<int>(fn, v, name);
                BoundDerived<int> h; h.owner = std::move(owner); h.derived = p; return h;
            })
        .def("_add_derived_float_internal", [](PyModel& self, nb::object fn, std::string name, nb::args deps){
                std::vector<nb::object> v; v.reserve(deps.size());
                for (size_t i=0;i<deps.size();++i) v.push_back(nb::cast<nb::object>(deps[i]));
                auto [owner, p] = self.add_derived_slot_vec<double>(fn, v, name);
                BoundDerived<double> h; h.owner = std::move(owner); h.derived = p; return h;
            })
        .def("_add_derived_str_internal", [](PyModel& self, nb::object fn, std::string name, nb::args deps){
                std::vector<nb::object> v; v.reserve(deps.size());
                for (size_t i=0;i<deps.size();++i) v.push_back(nb::cast<nb::object>(deps[i]));
                auto [owner, p] = self.add_derived_slot_vec<std::string>(fn, v, name);
                BoundDerived<std::string> h; h.owner = std::move(owner); h.derived = p; return h;
            })
        .def("_add_derived_bool_internal", [](PyModel& self, nb::object fn, std::string name, nb::args deps){
                std::vector<nb::object> v; v.reserve(deps.size());
                for (size_t i=0;i<deps.size();++i) v.push_back(nb::cast<nb::object>(deps[i]));
                auto [owner, p] = self.add_derived_slot_vec<bool>(fn, v, name);
                BoundDerived<bool> h; h.owner = std::move(owner); h.derived = p; return h;
            })
        .def("_add_list_int_internal", [](PyModel& self, nb::list py){
                std::vector<int> vec; vec.reserve(nb::len(py)); for(auto h: py) vec.push_back(nb::cast<int>(h));
                auto [owner, p] = self.add_list_int_slot(std::move(vec));
                BoundList<int> h; h.selected = &static_cast<SlotList<int>&>(*owner).selected; h.owner = std::move(owner); h.list = p; return h;
            }, nb::arg("values") = nb::list())
        .def("_add_list_float_internal", [](PyModel& self, nb::list py){
                std::vector<double> vec; vec.reserve(nb::len(py)); for(auto h: py) vec.push_back(nb::cast<double>(h));
                auto [owner, p] = self.add_list_float_slot(std::move(vec));
                BoundList<double> h; h.selected = &static_cast<SlotList<double>&>(*owner).selected; h.owner = std::move(owner); h.list = p; return h;
            }, nb::arg("values") = nb::list())
        .def("_add_list_str_internal", [](PyModel& self, nb::list py){
                std::vector<std::string> vec; vec.reserve(nb::len(py)); for(auto h: py) vec.push_back(nb::cast<std::string>(h));
                auto [owner, p] = self.add_list_str_slot(std::move(vec));
                BoundList<std::string> h; h.selected = &static_cast<SlotList<std::string>&>(*owner).selected; h.owner = std::move(owner); h.list = p; return h;
            }, nb::arg("values") = nb::list())
        .def("_add_document_internal", [](PyModel& self, std::string text, bool markdown) {
                auto [owner, p] = self.add_document_slot(document_from(text, markdown));
                BoundDocument h; h.owner = std::move(owner); h.doc = p; return h;
            }, nb::arg("text") = "", nb::arg("markdown") = false)
        .def("_add_plot_internal", [](PyModel& self){
                auto [owner, p] = self.add_plot_slot();
                BoundPlot h; h.owner = std::move(owner); h.plot = p; return h;
            })
        .def("_add_table_internal", [](PyModel& self, nb::object py_obj){
                auto [owner, p] = self.add_table_slot(std::move(py_obj));
                BoundTable h; h.owner = std::move(owner); h.slot = p; return h;
            }, nb::arg("source"))
        .def("_add_tree_internal", [](PyModel& self, nb::object py_obj){
                auto [owner, p] = self.add_tree_slot(py_obj);
                BoundTree h; h.owner = std::move(owner); h.ctrl = p; return h;
            }, nb::arg("source"))
        .def("_release_py_refs_internal", [](PyModel& self){ self.release_py_refs(); });
}

static void bind_app(nb::module_& m) {
    m.def("_txn_begin", [](){
        txn_marks.push_back(txn_queue.size());
        ++txn_depth;
    });
    m.def("_txn_commit", [](){
        if (txn_depth == 0) return;
        if (--txn_depth == 0) {
            txn_marks.clear();
            txn_flush_batch();
        } else {
            txn_marks.pop_back();
        }
    });
    m.def("_txn_abort", [](){
        if (txn_depth == 0) return;
        size_t mark = txn_marks.empty() ? 0 : txn_marks.back();
        txn_queue.resize(mark);
        txn_marks.pop_back();
        if (--txn_depth == 0) {
            txn_marks.clear();
            txn_queue.clear();
            txn_queue.shrink_to_fit();
        }
    });

    m.def("_is_running", [](){ return g_has_handle.load(std::memory_order_acquire) || g_run_guard.load(std::memory_order_acquire); });
    m.def("_request_quit", [](){
        // Must set the flag under the same mutex wait_for() locks — otherwise a
        // notify landing between the waiter's predicate check and its actual
        // wait (inside wait_for's own lock) is a lost wakeup: the flag flip and
        // the notify are invisible to a waiter that hasn't started waiting yet
        // and has no lock held to serialize against.
        {
            std::lock_guard<std::mutex> lk(g_headless_cv_mutex);
            g_headless_quit_requested.store(true, std::memory_order_release);
        }
        g_headless_cv.notify_all();
    });
    // Test helper: the tooltips of the widgets the model's view builds, in layout order.
    m.def("_tooltips", [](PyModel& model) {
        // A second tree over a model an app is driving would race the logic thread.
        if (g_has_handle.load(std::memory_order_acquire) || g_run_guard.load(std::memory_order_acquire))
            throw std::runtime_error("_tooltips: not while an app is running");
        WidgetTree tree{model};
        auto snap = tree.build_snapshot(400, 300, 1);
        std::vector<std::string> out;
        for (auto& [id, r] : snap->geometry)
            if (auto t = tree.tooltip_of(id); !t.empty()) out.emplace_back(t);
        for (auto& [id, r] : snap->overlay_geometry)   // an open popup's widgets
            if (auto t = tree.tooltip_of(id); !t.empty()) out.emplace_back(t);
        return out;
    }, nb::arg("model"));
    // Test helper: whether each widget of the model's view is enabled, in layout order.
    m.def("_enabled", [](PyModel& model) {
        if (g_has_handle.load(std::memory_order_acquire) || g_run_guard.load(std::memory_order_acquire))
            throw std::runtime_error("_enabled: not while an app is running");
        WidgetTree tree{model};
        auto snap = tree.build_snapshot(400, 300, 1);
        std::vector<bool> out;
        for (WidgetId id : tree.leaf_ids()) out.push_back(tree.enabled(id));
        return out;
    }, nb::arg("model"));
    // Test helper: every text the model's view draws (after two frames, so built-on-demand parts
    // such as a dock are there).
    m.def("_table_set_cell", [](nb::object src, size_t r, size_t c, std::string text) {
        PyTableData d{std::make_shared<nb::object>(src), {}, {}};
        nb::gil_scoped_release rel;   // as the logic thread calls it
        return d.set_cell(r, c, text);
    });
    // Test helpers: a Python tree source's rename (None: it has none) and a click on a node's box
    // (the node's state afterwards), through the same TreeController the widget uses.
    m.def("_tree_rename", [](nb::object src, int64_t id, std::string text) -> std::optional<bool> {
        auto s = PythonTreeSource::make(std::make_shared<nb::object>(src));
        if (!s.rename) return std::nullopt;
        nb::gil_scoped_release rel;
        return s.rename(static_cast<prism::ui::TreeNodeId>(id), text);
    });
    m.def("_tree_toggle_check", [](nb::object src, int64_t id) -> std::optional<bool> {
        auto s = PythonTreeSource::make(std::make_shared<nb::object>(src));
        nb::gil_scoped_release rel;
        prism::ui::TreeController c{s};
        c.toggle_check(static_cast<prism::ui::TreeNodeId>(id));
        return s.checked ? s.checked(static_cast<prism::ui::TreeNodeId>(id)) : std::nullopt;
    });
    m.def("_table_cell_check", [](nb::object src, size_t r, size_t c) {
        PyTableData d{std::make_shared<nb::object>(src), {}, {}};
        nb::gil_scoped_release rel;
        return prism::ui::wrap_column_storage(d).cell_check(r, c);
    });
    m.def("_table_editable", [](nb::object src, size_t c) {
        PyTableData d{std::make_shared<nb::object>(src), {}, {}};
        nb::gil_scoped_release rel;
        return d.editable(c);
    });
    m.def("_texts", [](PyModel& model) {
        if (g_has_handle.load(std::memory_order_acquire) || g_run_guard.load(std::memory_order_acquire))
            throw std::runtime_error("_texts: not while an app is running");
        WidgetTree tree{model};
        auto snap = tree.build_snapshot(600, 400, 1);
        snap = tree.build_snapshot(600, 400, 2);
        std::vector<std::string> out;
        auto collect = [&](const prism::render::DrawList& dl) {
            for (auto& c : dl.commands)
                if (auto* t = std::get_if<prism::render::TextCmd>(&c)) out.push_back(t->text);
        };
        for (auto& dl : snap->draw_lists) collect(*dl);
        collect(snap->overlay);   // popups and dialogs
        return out;
    }, nb::arg("model"));
    // Test helper: drags from the widget showing `from` to the one showing `to` (its centre, or
    // its lower half), as a user would.
    m.def("_drag_text", [](PyModel& model, std::string from, std::string to, bool to_lower_half) {
        if (g_has_handle.load(std::memory_order_acquire) || g_run_guard.load(std::memory_order_acquire))
            throw std::runtime_error("_drag_text: not while an app is running");
        WidgetTree tree{model};
        auto snap = tree.build_snapshot(400, 300, 1);
        auto rect_of = [&](const std::string& text) -> Rect {
            for (std::size_t i = 0; i < snap->draw_lists.size(); ++i)
                for (auto& c : snap->draw_lists[i]->commands)
                    if (auto* t = std::get_if<prism::render::TextCmd>(&c); t && t->text == text) return snap->geometry[i].second;
            throw nb::value_error(("no widget shows " + text).c_str());
        };
        Rect a = rect_of(from), b = rect_of(to);
        Point p{a.origin.x + DX{5}, a.origin.y + DY{a.extent.h.raw() / 2}};
        Point q{b.origin.x + DX{5}, b.origin.y + DY{b.extent.h.raw() * (to_lower_half ? 0.75f : 0.5f)}};
        using prism::app::widget_detail::route_event;
        route_event(tree, snap.get(), MouseButton{p, 1, true});
        route_event(tree, snap.get(), MouseMove{Point{p.x, p.y + DY{6}}});
        route_event(tree, snap.get(), MouseMove{q});
        route_event(tree, snap.get(), MouseButton{q, 1, false});
    }, nb::arg("model"), nb::arg("from_text"), nb::arg("to_text"), nb::arg("to_lower_half") = false);
    // Test helper: presses `keys` ("Ctrl+S", ...) in a tree over the model's view, as a user would.
    m.def("_press_keys", [](PyModel& model, std::vector<std::string> keys) {
        if (g_has_handle.load(std::memory_order_acquire) || g_run_guard.load(std::memory_order_acquire))
            throw std::runtime_error("_press_keys: not while an app is running");
        WidgetTree tree{model};
        auto snap = tree.build_snapshot(400, 300, 1);
        for (auto& k : keys) {
            auto s = prism::ui::parse_shortcut(k);
            if (!s) throw nb::value_error(("unknown key: " + k).c_str());
            prism::app::widget_detail::route_event(tree, snap.get(), KeyPress{s->key, s->mods});
        }
    }, nb::arg("model"), nb::arg("keys"));
    m.def("_fail_next_run", [](){ g_fail_next_run.store(true, std::memory_order_release); });

    // --- platform services: file dialogs, message boxes, drops ---
    m.def("choose_files", [](nb::callable done, std::string kind, std::string title,
                             std::vector<std::pair<std::string, std::string>> filters, std::string start) {
        static const std::map<std::string, FileDialogKind> kinds{
            {"open", FileDialogKind::Open}, {"open_many", FileDialogKind::OpenMany},
            {"save", FileDialogKind::Save}, {"folder", FileDialogKind::Folder}};
        auto k = kinds.find(kind);
        if (k == kinds.end())
            throw nb::value_error("choose_files: kind must be 'open', 'open_many', 'save' or 'folder'");
        FileDialog dialog{.kind = k->second, .title = std::move(title), .filters = {}, .start = std::move(start)};
        for (auto& [name, patterns] : filters) dialog.filters.push_back({name, patterns});
        auto holder = std::make_shared<PyHolder>(std::move(done));
        wait_for_context("prism.choose_files");
        std::lock_guard<std::mutex> lk(g_handle_mutex);
        if (!g_ctx) throw std::runtime_error("prism.choose_files: the app has ended");
        g_ctx->choose_files(std::move(dialog), [holder](FileDialogResult r) {
            if (!Py_IsInitialized()) return;
            nb::gil_scoped_acquire gil;
            try { holder->obj(r.paths, r.error); }
            catch (...) { report_python_callback_error(); }
        });
    }, nb::arg("on_done"), nb::arg("kind") = "open", nb::arg("title") = "",
       nb::arg("filters") = std::vector<std::pair<std::string, std::string>>{}, nb::arg("start") = "",
       "Any thread, while an app runs. Shows the platform's file dialog and returns at once; "
       "on_done(paths, error) runs on the logic thread: paths empty when cancelled, error set when "
       "the platform has none. kind: 'open', 'open_many', 'save' or 'folder'; filters: "
       "[(name, 'txt;md'), ...].");
    m.def("show_message", [](std::string text, std::string title, std::string kind) {
        static const std::map<std::string, MessageKind> kinds{
            {"info", MessageKind::Info}, {"warning", MessageKind::Warning}, {"error", MessageKind::Error}};
        auto k = kinds.find(kind);
        if (k == kinds.end()) throw nb::value_error("show_message: kind must be 'info', 'warning' or 'error'");
        wait_for_context("prism.show_message");
        std::lock_guard<std::mutex> lk(g_handle_mutex);
        if (!g_ctx) throw std::runtime_error("prism.show_message: the app has ended");
        g_ctx->show_message(k->second, std::move(title), std::move(text));
    }, nb::arg("text"), nb::arg("title") = "", nb::arg("kind") = "info",
       "Any thread, while an app runs. The platform's message box (ignored where there is none).");
    m.def("set_theme", [](std::string name) {
        if (name != "light" && name != "dark" && name != "system")
            throw nb::value_error("set_theme: 'light', 'dark' or 'system'");
        wait_for_context("prism.set_theme");
        // Posted outside g_handle_mutex: posting takes it too.
        try_post_any_thread([name] {
            AppContext* ctx = nullptr;
            {
                std::lock_guard<std::mutex> lk(g_handle_mutex);
                ctx = g_ctx;
            }
            if (!ctx) return;
            if (name == "system") ctx->follow_system_theme();
            else ctx->set_theme(name == "light" ? light_theme() : dark_theme());
        });
    }, nb::arg("name"),
       "Any thread, while an app runs: 'light', 'dark', or 'system' (follow the OS, also when "
       "it changes). Every window repaints.");
    m.def("_theme", []() -> std::string {
        return dispatch_sync_read<std::string>([] {
            AppContext* ctx = nullptr;
            {
                std::lock_guard<std::mutex> lk(g_handle_mutex);
                ctx = g_ctx;
            }
            if (!ctx) return std::string{};
            return ctx->theme().canvas_bg == light_theme().canvas_bg ? std::string{"light"} : std::string{"dark"};
        });
    }, "Test-only: the running app's theme, 'light' or 'dark'.");
    m.def("open_window", [](nb::object model, std::string title, int width, int height) {
        if (!nb::isinstance<PyModel>(model)) throw nb::type_error("open_window: needs a prism.Model");
        wait_for_context("prism.open_window");
        // The model stays alive until the run ends (it is the window's model); posted outside
        // g_handle_mutex: posting takes it too.
        auto holder = std::make_shared<PyHolder>(model);
        {
            std::lock_guard<std::mutex> lk(g_window_models_mutex);
            g_window_models.push_back(holder);
        }
        auto* pm = &nb::cast<PyModel&>(model);
        try_post_any_thread([pm, holder, title = std::move(title), width, height] {
            AppContext* ctx = nullptr;
            {
                std::lock_guard<std::mutex> lk(g_handle_mutex);
                ctx = g_ctx;
            }
            if (!ctx) return;
            nb::gil_scoped_acquire gil;   // building its view runs the model's view()
            ctx->open_window(*pm, {.title = title.c_str(), .width = width, .height = height});
        });
    }, nb::arg("model"), nb::arg("title") = "PRISM", nb::arg("width") = 480, nb::arg("height") = 360,
       "While an app runs (any thread): shows `model` in a window of its own (desktop). It may read "
       "the main model's fields. Closing it removes it; the app goes on.");
    m.def("_window_count", []() {
        return dispatch_sync_read<int>([] {
            AppContext* ctx = nullptr;
            {
                std::lock_guard<std::mutex> lk(g_handle_mutex);
                ctx = g_ctx;
            }
            int n = 0;
            if (ctx) ctx->registry().for_each([&](WindowId, WindowRegistry::Entry&) { ++n; });
            return n;
        });
    }, "Test-only: how many windows the running app has.");
    m.def("settings_dir", [](std::string org, std::string app) {
        return prism::backends::settings_dir(org, app).string();
    }, nb::arg("org"), nb::arg("app"),
       "The folder where this user's settings for `app` belong (created if needed): "
       "~/.local/share/<org>/<app> on Linux, %APPDATA% on Windows, Application Support on macOS.");
    m.def("on_file_drop", [](nb::object fn) {
        auto holder = fn.is_none() ? nullptr : std::make_shared<PyHolder>(std::move(fn));
        std::lock_guard<std::mutex> lk(g_file_drop_mutex);
        g_file_drop = std::move(holder);
    }, nb::arg("callback").none(),
       "callback(paths, x, y) runs on the logic thread for every file dropped on the window; "
       "None removes it. Set it before prism.run or while it runs.");
    m.def("_simulate_file_drop", [](std::vector<std::string> paths, float x, float y) {
        wait_for_context("_simulate_file_drop");
        // Posted outside g_handle_mutex: posting takes it too.
        prism::input::FileDrop drop{Point{X{x}, Y{y}}, std::move(paths)};
        try_post_any_thread([drop] { call_file_drop(drop); });
    }, nb::arg("paths"), nb::arg("x") = 0.f, nb::arg("y") = 0.f, "Test-only: a file drop, as the window would deliver it.");
    m.def("_run_headless", [](PyModel& model, int delay_ms){
        {
            bool expected = false;
            if (!g_run_guard.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                throw std::runtime_error("prism.run already running");
            g_app_closed.store(false, std::memory_order_release);
            g_headless_quit_requested.store(false, std::memory_order_release);
        }
        RunGuardReset reset_guard;
        if (g_fail_next_run.exchange(false, std::memory_order_acq_rel))
            throw std::runtime_error("_fail_next_run(): forced failure for testing");
        nb::gil_scoped_release release;
        auto backend = Backend{std::make_unique<DelayHeadlessBackend>(delay_ms)};
        auto& window = backend.create_window({});
        auto setup = [](AppContext& ctx){
            // Install GIL wrapper for logic-thread drains (mouse/tick) — SDL-free core hook
            ctx.set_logic_wrapper([](std::function<void()> fn){
                // Once the interpreter is finalized, nothing that might drain Python
                // callbacks (mouse/tick logic-thread work) may run — drop it.
                // Safe: g_post_handle is reset before run()/_run_headless() returns, so no post can reach this drain anyway.
                if (!Py_IsInitialized()) return;
                nb::gil_scoped_acquire g;
                fn();
            });
            ctx.on_file_drop(&call_file_drop);
            std::lock_guard<std::mutex> lk(g_handle_mutex);
            g_post_handle = ctx.post_handle();
            g_ctx = &ctx;
            g_has_handle.store(true, std::memory_order_release);
            g_app_closed.store(false, std::memory_order_release);
        };
        model_app(backend, window, model, setup);
    }, nb::arg("model"), nb::arg("delay_ms")=100);

    m.def("run", [](PyModel& model, std::string title, std::string ui_font, std::string mono_font){
        {
            bool expected = false;
            if (!g_run_guard.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                throw std::runtime_error("prism.run already running");
            g_app_closed.store(false, std::memory_order_release);
        }
        RunGuardReset reset_guard;
        if (g_fail_next_run.exchange(false, std::memory_order_acq_rel))
            throw std::runtime_error("_fail_next_run(): forced failure for testing");
        // Must be called from main thread on macOS
        nb::gil_scoped_release release;
        RenderConfig render_cfg;
        if (!ui_font.empty()) render_cfg.font_path = ui_font.c_str();
        if (!mono_font.empty()) render_cfg.mono_font_path = mono_font.c_str();
        auto backend = Backend::software(render_cfg);
        WindowConfig cfg;
        cfg.title = title.c_str();
        auto& window = backend.create_window(cfg);
        auto setup = [](AppContext& ctx){
            ctx.set_logic_wrapper([](std::function<void()> fn){
                // Once the interpreter is finalized, nothing that might drain Python
                // callbacks (mouse/tick logic-thread work) may run — drop it.
                // Safe: g_post_handle is reset before run()/_run_headless() returns, so no post can reach this drain anyway.
                if (!Py_IsInitialized()) return;
                nb::gil_scoped_acquire g;
                fn();
            });
            ctx.on_file_drop(&call_file_drop);
            std::lock_guard<std::mutex> lk(g_handle_mutex);
            g_post_handle = ctx.post_handle();
            g_ctx = &ctx;
            g_has_handle.store(true, std::memory_order_release);
            g_app_closed.store(false, std::memory_order_release);
        };
        model_app(backend, window, model, setup);
    }, nb::arg("model"), nb::arg("title")="PRISM App", nb::arg("ui_font")="", nb::arg("mono_font")="");
}

NB_MODULE(_prism_ext, m) {
    bind_runtime(m);
    bind_handles(m);
    bind_plot(m);
    bind_tree_table(m);
    bind_view(m);
    bind_model(m);
    bind_app(m);
}
