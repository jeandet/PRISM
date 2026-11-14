#pragma once
// BoundPlot: the bound handle for a Model's plot field.

#include "common.hpp"

// BoundPlot — must be after list_op_dispatch
struct BoundPlot {
    std::shared_ptr<SlotBase> owner;
    prism::plot::PlotModel* plot = nullptr;
    void add_series(nb::list xs, nb::list ys, std::string color_str = "", float thickness = 2.f, bool fill = false) {
        auto color = parse_series_color(color_str);
        if (!plot) return;
        std::vector<double> vx, vy;
        vx.reserve(nb::len(xs)); vy.reserve(nb::len(ys));
        for (auto h : xs) vx.push_back(nb::cast<double>(h));
        for (auto h : ys) vy.push_back(nb::cast<double>(h));
        auto* p = plot;
        auto fn = [p, vx = std::move(vx), vy = std::move(vy), color, thickness, fill]() mutable {
            prism::plot::XYData data{std::move(vx), std::move(vy)};
            prism::plot::SeriesStyle style;
            style.thickness = thickness;
            style.fill = fill;
            if (color) style.color = *color;
            p->add_series(std::move(data), style);
        };
        list_op_dispatch(owner, std::move(fn));
    }
    void clear_series() {
        if (!plot) return;
        auto* p = plot;
        list_op_dispatch(owner, [p](){ p->clear_series(); });
    }
    void notify() {
        if (!plot) return;
        auto* p = plot;
        list_op_dispatch(owner, [p](){ p->notify(); });
    }
    // Single-post clear+add(+add...)+notify — see parse_replace_series_args for the two call forms.
    void replace_series(nb::object xs_or_series, nb::object ys = nb::none(), nb::object color = nb::none(),
                         float thickness = 2.f, bool fill = false) {
        if (!plot) return;
        reject_stray_kwargs_in_list_form(ys, color, fill);
        replace_series_dispatch(owner, plot, parse_replace_series_args(xs_or_series, ys, color), thickness, fill);
    }
    void set_labels(nb::object x = nb::none(), nb::object y = nb::none()) {
        if (!plot) return;
        auto* p = plot;
        bool has_x = !x.is_none(), has_y = !y.is_none();
        std::string xs = has_x ? nb::cast<std::string>(x) : std::string();
        std::string ys = has_y ? nb::cast<std::string>(y) : std::string();
        list_op_dispatch(owner, [p, has_x, has_y, xs, ys](){
            if (has_x) p->x_label.set(xs);
            if (has_y) p->y_label.set(ys);
        });
    }
    void set_x_label(std::string s) { if(plot) field_set_dispatch(owner, &plot->x_label, std::move(s)); }
    void set_y_label(std::string s) { if(plot) field_set_dispatch(owner, &plot->y_label, std::move(s)); }
    std::string get_x_label() const { if(!plot) return ""; return dispatch_sync_read<std::string>([p=plot](){ return p->x_label.get(); }); }
    std::string get_y_label() const { if(!plot) return ""; return dispatch_sync_read<std::string>([p=plot](){ return p->y_label.get(); }); }
    size_t series_count() const { if(!plot) return 0; return dispatch_sync_read<size_t>([p=plot](){ return p->series_count(); }); }
    size_t series_len(size_t i) const { if(!plot) return 0; return dispatch_sync_read<size_t>([p=plot,i](){ return p->series_len(i); }); }
    void reset_view() { if(plot){ auto* p=plot; list_op_dispatch(owner, [p](){ p->reset_view(); }); } }
};
