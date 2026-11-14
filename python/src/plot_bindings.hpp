#pragma once
// Plot helpers: series colour parsing, replace_series arguments, and the logic-thread PlotHandle.

#include "common.hpp"

// Empty string = keep the theme default. Anything else must be exactly '#rrggbb' --
// validated here, on the calling thread, so a typo raises at the call site instead of
// being silently dropped (or throwing std::stoi inside the posted logic-thread closure,
// where only on_error would ever see it).
inline std::optional<Color> parse_series_color(const std::string& s) {
    if (s.empty()) return std::nullopt;
    bool ok = s.size() == 7 && s[0] == '#'
        && s.find_first_not_of("0123456789abcdefABCDEF", 1) == std::string::npos;
    if (!ok)
        throw nb::value_error(("plot color must be '#rrggbb', got '" + s + "'").c_str());
    auto hex = [&s](size_t at) { return std::stoi(s.substr(at, 2), nullptr, 16); };
    return Color::rgba((uint8_t)hex(1), (uint8_t)hex(3), (uint8_t)hex(5));
}

// replace_series() shared parsing/dispatch — one series (xs, ys, color) worth of C++ data,
// converted from Python objects on the calling thread before crossing to the logic thread.
struct ReplaceSeriesSpec {
    std::vector<double> vx, vy;
    std::optional<Color> color;
};

// Accepts either (xs, ys) for one series (ys != None) or a single list of (xs, ys, color)
// tuples for many series (ys == None) — see BoundPlot/PlotHandle::replace_series binding.
inline std::vector<ReplaceSeriesSpec> parse_replace_series_args(nb::object xs_or_series, nb::object ys, nb::object color) {
    auto to_doubles = [](nb::object seq) {
        nb::list l = nb::cast<nb::list>(seq);
        std::vector<double> v; v.reserve(nb::len(l));
        for (auto h : l) v.push_back(nb::cast<double>(h));
        return v;
    };
    std::vector<ReplaceSeriesSpec> specs;
    if (ys.is_none()) {
        constexpr const char* shape_error = "replace_series(): each series must be (xs, ys) or (xs, ys, color)";
        for (auto item : nb::cast<nb::list>(xs_or_series)) {
            // nb::cast<nb::tuple> silently converts any sequence (incl. a bare list) via
            // PySequence_Tuple — check the real type first so a bare list/short tuple is
            // rejected here, not read out of bounds by tuple::operator[] below.
            if (!nb::isinstance<nb::tuple>(item)) throw nb::type_error(shape_error);
            nb::tuple t = nb::borrow<nb::tuple>(item);
            size_t n = nb::len(t);
            if (n != 2 && n != 3) throw nb::type_error(shape_error);
            ReplaceSeriesSpec spec;
            spec.vx = to_doubles(t[0]);
            spec.vy = to_doubles(t[1]);
            if (n > 2 && !t[2].is_none()) spec.color = parse_series_color(nb::cast<std::string>(t[2]));
            specs.push_back(std::move(spec));
        }
    } else {
        ReplaceSeriesSpec spec;
        spec.vx = to_doubles(xs_or_series);
        spec.vy = to_doubles(ys);
        if (!color.is_none()) spec.color = parse_series_color(nb::cast<std::string>(color));
        specs.push_back(std::move(spec));
    }
    return specs;
}

// List form (ys omitted) carries color per-series in each tuple — a top-level color/fill
// would silently do nothing, so reject it instead of accepting it as a no-op.
inline void reject_stray_kwargs_in_list_form(const nb::object& ys, const nb::object& color, bool fill) {
    if (ys.is_none() && (!color.is_none() || fill))
        throw nb::type_error("replace_series(): color/fill are not valid with the list form — set color per series in the (xs, ys, color) tuple");
}

inline void replace_series_dispatch(std::shared_ptr<void> keep, prism::plot::PlotModel* p, std::vector<ReplaceSeriesSpec> specs, float thickness, bool fill) {
    auto fn = [p, specs = std::move(specs), thickness, fill]() mutable {
        p->clear_series();
        for (auto& s : specs) {
            prism::plot::XYData data{std::move(s.vx), std::move(s.vy)};
            prism::plot::SeriesStyle style;
            style.thickness = thickness;
            style.fill = fill;
            if (s.color) style.color = *s.color;
            p->add_series(std::move(data), style);
        }
        p->notify();
    };
    list_op_dispatch(std::move(keep), std::move(fn));
}

// Plot support — Slot only (Bound* defined after list_op_dispatch)
struct SlotPlot : SlotBase {
    prism::plot::PlotModel plot;
    void build(ViewBuilder& vb) override {
        vb.canvas(plot)
            .depends_on(plot.x_range).depends_on(plot.y_range)
            .depends_on(plot.view).depends_on(plot.cursor)
            .depends_on(plot.revision);
    }
};
// Standalone Plot handle — state lives behind a shared_ptr (like FieldHandle/ListHandle
// above) so posted mutation closures can capture it as `keep` and outlive a `del` that
// races the logic thread draining the post.
struct PlotHandle {
    std::shared_ptr<prism::plot::PlotModel> plot = std::make_shared<prism::plot::PlotModel>();
    void add_series(nb::list xs, nb::list ys, std::string color_str = "", float thickness = 2.f, bool fill = false) {
        auto color = parse_series_color(color_str);
        std::vector<double> vx, vy;
        vx.reserve(nb::len(xs)); vy.reserve(nb::len(ys));
        for (auto h : xs) vx.push_back(nb::cast<double>(h));
        for (auto h : ys) vy.push_back(nb::cast<double>(h));
        auto* p = plot.get();
        auto fn = [p, vx = std::move(vx), vy = std::move(vy), color, thickness, fill]() mutable {
            prism::plot::XYData data{std::move(vx), std::move(vy)};
            prism::plot::SeriesStyle style; style.thickness=thickness; style.fill=fill;
            if (color) style.color = *color;
            p->add_series(std::move(data), style);
        };
        list_op_dispatch(plot, std::move(fn));
    }
    void clear_series(){ auto* p=plot.get(); list_op_dispatch(plot, [p](){ p->clear_series(); }); }
    void notify(){ auto* p=plot.get(); list_op_dispatch(plot, [p](){ p->notify(); }); }
    // Single-post clear+add(+add...)+notify — see parse_replace_series_args for the two call forms.
    void replace_series(nb::object xs_or_series, nb::object ys = nb::none(), nb::object color = nb::none(),
                         float thickness = 2.f, bool fill = false) {
        reject_stray_kwargs_in_list_form(ys, color, fill);
        replace_series_dispatch(plot, plot.get(), parse_replace_series_args(xs_or_series, ys, color), thickness, fill);
    }
    void set_labels(nb::object x = nb::none(), nb::object y = nb::none()) {
        auto* p = plot.get();
        bool has_x = !x.is_none(), has_y = !y.is_none();
        std::string xs = has_x ? nb::cast<std::string>(x) : std::string();
        std::string ys = has_y ? nb::cast<std::string>(y) : std::string();
        list_op_dispatch(plot, [p, has_x, has_y, xs, ys](){
            if (has_x) p->x_label.set(xs);
            if (has_y) p->y_label.set(ys);
        });
    }
    void set_x_label(std::string s){ field_set_dispatch(plot, &plot->x_label, std::move(s)); }
    void set_y_label(std::string s){ field_set_dispatch(plot, &plot->y_label, std::move(s)); }
    std::string get_x_label() const { auto* p=plot.get(); return dispatch_sync_read<std::string>([p](){ return p->x_label.get(); }); }
    std::string get_y_label() const { auto* p=plot.get(); return dispatch_sync_read<std::string>([p](){ return p->y_label.get(); }); }
    size_t series_count() const { auto* p=plot.get(); return dispatch_sync_read<size_t>([p](){ return p->series_count(); }); }
    size_t series_len(size_t i) const { auto* p=plot.get(); return dispatch_sync_read<size_t>([p,i](){ return p->series_len(i); }); }
    void reset_view(){ auto* p=plot.get(); list_op_dispatch(plot, [p](){ p->reset_view(); }); }
};
