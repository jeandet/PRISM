#pragma once
// Python-backed tree sources and table data: PythonTreeSource, BoundTree/BoundTable, TreeHandle.

#include "common.hpp"

// Tree support — Python-backed TreeSource
struct PythonTreeSource {
    // Python-side node ids are signed ints (typically hash() values, which can be
    // negative); C++ stores them in the unsigned TreeNodeId. root_at/child_at cast
    // on the way in — cast back on the way out, or a negative hash reaches the
    // source as a huge unsigned int and misses every lookup (labels fall back to
    // std::to_string(id) and the tree renders giant integers).
    static int64_t to_py_id(prism::ui::TreeNodeId id) { return static_cast<int64_t>(id); }

    // Takes the ONE shared holder for the source object (SlotTree::py_src_holder),
    // so resetting *holder to None (SlotTree::clear) also neutralizes these
    // callbacks — they dereference the same shared_ptr on every call.
    static prism::ui::TreeSource make(std::shared_ptr<nb::object> held) {
        prism::ui::TreeSource src;
        src.root_count = [held]() -> size_t {
            nb::gil_scoped_acquire g;
            auto o = *held;
            if (nb::hasattr(o, "root_count")) return nb::cast<size_t>(o.attr("root_count")());
            return 0;
        };
        src.root_at = [held](size_t i) -> prism::ui::TreeNodeId {
            nb::gil_scoped_acquire g;
            auto o = *held;
            if (nb::hasattr(o, "root_at")) {
                auto v = o.attr("root_at")(i);
                // allow negative Python hash -> uint64_t
                int64_t tmp = nb::cast<int64_t>(v);
                return static_cast<prism::ui::TreeNodeId>(tmp);
            }
            return 0;
        };
        src.child_count = [held](prism::ui::TreeNodeId id) -> size_t {
            nb::gil_scoped_acquire g;
            auto o = *held;
            if (nb::hasattr(o, "child_count")) return nb::cast<size_t>(o.attr("child_count")(to_py_id(id)));
            return 0;
        };
        src.child_at = [held](prism::ui::TreeNodeId id, size_t i) -> prism::ui::TreeNodeId {
            nb::gil_scoped_acquire g;
            auto o = *held;
            if (nb::hasattr(o, "child_at")) {
                auto v = o.attr("child_at")(to_py_id(id), i);
                int64_t tmp = nb::cast<int64_t>(v);
                return static_cast<prism::ui::TreeNodeId>(tmp);
            }
            return 0;
        };
        src.label = [held](prism::ui::TreeNodeId id) -> std::string {
            nb::gil_scoped_acquire g;
            auto o = *held;
            if (nb::hasattr(o, "label")) return nb::cast<std::string>(o.attr("label")(to_py_id(id)));
            return std::to_string(id);
        };
        src.has_children = [held](prism::ui::TreeNodeId id) -> bool {
            nb::gil_scoped_acquire g;
            auto o = *held;
            if (nb::hasattr(o, "has_children")) return nb::cast<bool>(o.attr("has_children")(to_py_id(id)));
            return false;
        };
        src.attributes = [held](prism::ui::TreeNodeId id) -> std::vector<std::pair<std::string,std::string>> {
            nb::gil_scoped_acquire g;
            auto o = *held;
            std::vector<std::pair<std::string,std::string>> out;
            if (nb::hasattr(o, "attributes")) {
                auto res = o.attr("attributes")(to_py_id(id));
                if (!res.is_none()) {
                    auto d = nb::cast<nb::dict>(res);
                    for (auto kv : d) out.emplace_back(nb::cast<std::string>(kv.first), nb::cast<std::string>(kv.second));
                }
            }
            return out;
        };
        // optional icon
        src.icon = [held](prism::ui::TreeNodeId id) -> std::optional<std::string> {
            nb::gil_scoped_acquire g;
            auto o = *held;
            if (nb::hasattr(o, "icon")) {
                auto v = o.attr("icon")(to_py_id(id));
                if (!v.is_none()) return nb::cast<std::string>(v);
            }
            return std::nullopt;
        };
        add_edits(src, held);
        return src;
    }

    // A source with `checked` (and `set_checked`) gets checkboxes; one with `rename` renames in
    // place, refusing a name when it returns False (None accepts it, as for table cells).
    static void add_edits(prism::ui::TreeSource& src, const std::shared_ptr<nb::object>& held) {
        auto has = [&](const char* name) { return nb::hasattr(*held, name) && !held->attr(name).is_none(); };
        if (has("checked")) {
            src.checked = [held](prism::ui::TreeNodeId id) -> std::optional<bool> {
                nb::gil_scoped_acquire g;
                auto o = *held;
                if (!nb::hasattr(o, "checked")) return std::nullopt;
                auto v = o.attr("checked")(to_py_id(id));
                if (v.is_none()) return std::nullopt;
                return nb::cast<bool>(v);
            };
            src.set_checked = [held](prism::ui::TreeNodeId id, bool on) {
                nb::gil_scoped_acquire g;
                auto o = *held;
                if (nb::hasattr(o, "set_checked")) o.attr("set_checked")(to_py_id(id), on);
            };
        }
        if (has("rename")) {
            src.rename = [held](prism::ui::TreeNodeId id, std::string_view text) -> bool {
                nb::gil_scoped_acquire g;
                auto o = *held;
                if (!nb::hasattr(o, "rename")) return false;
                auto r = o.attr("rename")(to_py_id(id), std::string(text));
                return r.is_none() || nb::cast<bool>(r);
            };
        }
    }
};
struct SlotTree : SlotBase {
    std::shared_ptr<nb::object> py_src_holder;
    prism::ui::TreeController ctrl;
    explicit SlotTree(prism::ui::TreeSource src) : ctrl(std::move(src)) {}
    explicit SlotTree(nb::object py_obj) : py_src_holder(std::make_shared<nb::object>(py_obj)), ctrl(PythonTreeSource::make(py_src_holder)) {}
    void build(ViewBuilder& vb) override { vb.tree(ctrl); }
    void traverse(visitproc visit, void* arg) override {
        if (py_src_holder) if (PyObject* o = py_src_holder->ptr()) visit(o, arg);
    }
    // Reset to None rather than an empty object: PythonTreeSource's callbacks
    // (captured shared_ptr<nb::object>) dereference *py_src_holder via
    // nb::hasattr on every call — None is a safe no-op source, a null
    // PyObject* is not.
    void clear() override {
        if (py_src_holder) *py_src_holder = nb::none();
    }
    // A `keep` shared_ptr<SlotBase> captured by a posted/queued closure (see
    // list_op_dispatch/field_set_dispatch above) can be the reference that drops
    // this SlotTree to zero — and the mutation queue itself is destroyed with the
    // GIL released (model_app.hpp:190). Releasing py_src_holder's PyObject* must
    // still happen under the GIL when the interpreter is alive; once it's gone,
    // leak instead of decref'ing into a torn-down interpreter — see
    // release_py_object_guarded above.
    ~SlotTree() override {
        if (py_src_holder) release_py_object_guarded(*py_src_holder);
    }
};
// A table over a Python object with column_count(), row_count(), cell_text(row, col) and,
// optionally, header(col) (prism.TableSource). Called on the logic thread: each call takes the
// GIL. The ONE holder is shared with SlotTable, so clearing it to None neutralizes the calls.
struct PyTableData {
    std::shared_ptr<nb::object> held;
    mutable std::vector<std::string> headers;   // header() returns a view: kept here
    // cell_image paths, decoded once (cells are drawn on every repaint).
    mutable std::unordered_map<std::string, std::shared_ptr<const prism::render::Image>> images;
    size_t column_count() const {
        nb::gil_scoped_acquire g;
        auto o = *held;
        return nb::hasattr(o, "column_count") ? nb::cast<size_t>(o.attr("column_count")()) : 0;
    }
    size_t row_count() const {
        nb::gil_scoped_acquire g;
        auto o = *held;
        return nb::hasattr(o, "row_count") ? nb::cast<size_t>(o.attr("row_count")()) : 0;
    }
    std::string cell_text(size_t r, size_t c) const {
        nb::gil_scoped_acquire g;
        auto o = *held;
        return nb::hasattr(o, "cell_text") ? nb::cast<std::string>(nb::str(o.attr("cell_text")(r, c))) : std::string{};
    }
    std::string_view header(size_t c) const {
        nb::gil_scoped_acquire g;
        auto o = *held;
        if (headers.size() <= c) headers.resize(c + 1);
        headers[c] = nb::hasattr(o, "header") ? nb::cast<std::string>(nb::str(o.attr("header")(c))) : std::string{};
        return headers[c];
    }
    // Editable when the object has a set_cell(row, col, text); editable(col), if it has one,
    // narrows that to some columns.
    bool editable(size_t c) const {
        nb::gil_scoped_acquire g;
        auto o = *held;
        if (!nb::hasattr(o, "set_cell") || o.attr("set_cell").is_none()) return false;
        return !nb::hasattr(o, "editable") || nb::cast<bool>(o.attr("editable")(c));
    }
    // cell_check(row, col): True or False draws a checkbox (a click hands set_cell "true" or
    // "false"); None, or no such method, a plain cell.
    std::optional<bool> cell_check(size_t r, size_t c) const {
        nb::gil_scoped_acquire g;
        auto o = *held;
        if (!nb::hasattr(o, "cell_check") || o.attr("cell_check").is_none()) return std::nullopt;
        auto v = o.attr("cell_check")(r, c);
        if (v.is_none()) return std::nullopt;
        return nb::cast<bool>(v);
    }
    // cell_image(row, col): the path of an icon drawn before the text, or None. A file that
    // cannot be read shows no icon.
    std::shared_ptr<const prism::render::Image> cell_image(size_t r, size_t c) const {
        nb::gil_scoped_acquire g;
        auto o = *held;
        if (!nb::hasattr(o, "cell_image") || o.attr("cell_image").is_none()) return nullptr;
        auto v = o.attr("cell_image")(r, c);
        if (v.is_none()) return nullptr;
        auto path = nb::cast<std::string>(v);
        if (auto it = images.find(path); it != images.end()) return it->second;
        auto loaded = prism::render::load_image(path, Size{Width{16}, Height{16}});
        return images[path] = loaded ? *loaded : nullptr;
    }
    // set_cell's answer: False refuses the text (the editor stays open); None or anything else
    // takes it. An exception is reported and refuses it.
    bool set_cell(size_t r, size_t c, std::string_view text) {
        nb::gil_scoped_acquire g;
        try {
            auto res = held->attr("set_cell")(r, c, std::string(text));
            return !(nb::isinstance<nb::bool_>(res) && !nb::cast<bool>(res));
        } catch (...) {
            report_python_callback_error();
            return false;
        }
    }
};

struct SlotTable : SlotBase {
    std::shared_ptr<nb::object> py_src_holder;
    PyTableData data;
    Field<std::int64_t> revision{0};   // refresh() bumps it: the table re-reads the source
    Field<std::vector<std::size_t>> columns{};   // the columns shown, in order; empty: all
    explicit SlotTable(nb::object py_obj)
        : py_src_holder(std::make_shared<nb::object>(std::move(py_obj))), data{py_src_holder, {}, {}} {}
    void build(ViewBuilder& vb) override { vb.table(data).depends_on(revision).columns(columns); }
    void traverse(visitproc visit, void* arg) override {
        if (py_src_holder) if (PyObject* o = py_src_holder->ptr()) visit(o, arg);
    }
    void clear() override { if (py_src_holder) *py_src_holder = nb::none(); }
    ~SlotTable() override { if (py_src_holder) release_py_object_guarded(*py_src_holder); }
};

struct BoundTable {
    std::shared_ptr<SlotBase> owner;
    SlotTable* slot = nullptr;
    void refresh() {
        if (!slot) return;
        auto* s = slot;
        list_op_dispatch(owner, [s] { s->revision.set(s->revision.get() + 1); });
    }
    std::vector<std::size_t> get_columns() const {
        if (!slot) return {};
        auto* s = slot;
        return dispatch_sync_read<std::vector<std::size_t>>([s] { return s->columns.get(); });
    }
    void set_columns(std::vector<std::size_t> shown) {
        if (!slot) return;
        auto* s = slot;
        list_op_dispatch(owner, [s, shown = std::move(shown)] { s->columns.set(shown); });
    }
};

// The posted reader may run on the logic thread after the interpreter is finalized
// (see try_post_via_handle_impl's drain path), so it must not construct any nb::
// object — TreeRow is plain C++ data, safe to copy there. The namedtuple
// conversion happens back on the calling (GIL-holding) thread, in *_to_pylist below.
static std::vector<prism::ui::TreeRow> snapshot_tree_rows(const prism::ui::TreeController* p) {
    std::vector<prism::ui::TreeRow> out;
    out.reserve(p->rows.size());
    for (size_t i = 0; i < p->rows.size(); ++i) out.push_back(p->rows[i]);
    return out;
}
// rows() returns a list of this namedtuple (label, depth, has_children, expanded,
// selected, checked) rather than anonymous dicts, so rows[0].label works. Created in
// NB_MODULE; deliberately leaked — a static nb::object would decref at static
// destruction, possibly after interpreter finalization.
static nb::object* g_tree_row_type = nullptr;
static nb::list tree_rows_to_pylist(const std::vector<prism::ui::TreeRow>& rows) {
    nb::list out;
    for (auto& r : rows) {
        out.append((*g_tree_row_type)(
            nb::str(r.label.c_str()), nb::int_(r.depth), nb::bool_(r.has_children),
            nb::bool_(r.expanded), nb::bool_(r.selected),
            r.checked ? nb::object(nb::bool_(*r.checked)) : nb::none()));
    }
    return out;
}
struct BoundTree {
    std::shared_ptr<SlotBase> owner;
    prism::ui::TreeController* ctrl = nullptr;
    void refresh(){ if(ctrl){ auto* p=ctrl; list_op_dispatch(owner, [p](){ p->refresh(); }); } }
    nb::list rows(){
        if(!ctrl) return nb::list();
        auto* p = ctrl;
        auto snapshot = dispatch_sync_read<std::vector<prism::ui::TreeRow>>([p](){ return snapshot_tree_rows(p); });
        return tree_rows_to_pylist(snapshot);
    }
    // The current node and every selected one (Ctrl/Shift-click, Shift+arrows), in row order.
    std::optional<prism::ui::TreeNodeId> get_selected() const {
        if (!ctrl) return std::nullopt;
        auto* p = ctrl;
        return dispatch_sync_read<std::optional<prism::ui::TreeNodeId>>([p] { return p->selected.get(); });
    }
    void set_selected(std::optional<prism::ui::TreeNodeId> id) {
        if (!ctrl) return;
        auto* p = ctrl;
        list_op_dispatch(owner, [p, id] { p->selected.set(id); });
    }
    std::vector<prism::ui::TreeNodeId> selection() const {
        if (!ctrl) return {};
        auto* p = ctrl;
        return dispatch_sync_read<std::vector<prism::ui::TreeNodeId>>([p] { return p->selection.get(); });
    }
    Connection observe_selection(nb::callable cb) {
        if (!ctrl) throw nb::value_error("observe_selection(): handle is not bound to a Model");
        auto c = ctrl->selection.on_change().connect(WeakCallback(cb));
        if (owner) c.keep_alive(owner);
        return c;
    }
};
struct TreeHandle {
    std::shared_ptr<prism::ui::TreeController> ctrl;
    TreeHandle(prism::ui::TreeSource src): ctrl(std::make_shared<prism::ui::TreeController>(std::move(src))){}
    TreeHandle(nb::object py_obj): ctrl(std::make_shared<prism::ui::TreeController>(PythonTreeSource::make(std::make_shared<nb::object>(py_obj)))){}
};
