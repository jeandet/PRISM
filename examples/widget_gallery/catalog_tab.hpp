// Catalog: every widget in a tree; picking one shows what it is for, the C++ that makes one, and a
// live sample to try. The catalog is data (one Entry per widget); its pages are built from it.
#pragma once

#include "gallery_icons.hpp"

#include <prism/prism.hpp>
#include <prism/widgets/plot.hpp>

#include <cmath>
#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace gallery {

using Vb = prism::app::WidgetTree::ViewBuilder;

enum class Shape { Circle, Square, Triangle };

// The fields the samples show, with sample data.
struct Samples {
    prism::core::Field<prism::ui::TextField<>> text{{.value = "Ada Lovelace", .placeholder = "Name"}};
    prism::core::Field<prism::ui::Password<>> password{{.value = "hunter2", .placeholder = "Password"}};
    prism::core::Field<prism::ui::TextArea<>> area{{.value = "Several lines\nof editable text,\nwith undo.", .rows = 4}};
    prism::core::Field<prism::ui::ComboBox> combo{{.value = "Ap", .placeholder = "Fruit",
                                                   .options = {"Apple", "Apricot", "Banana", "Cherry"}}};
    prism::core::Field<prism::ui::SpinBox<int>> spin{{.value = 7, .min = 0, .max = 20, .step = 1}};
    prism::core::Field<prism::ui::Slider<>> slider{{.value = 0.3}};
    prism::core::Field<prism::ui::Checkbox> check{{.checked = true, .label = "Remember me"}};
    prism::core::Field<prism::ui::Radio<Shape>> radio{{Shape::Square}};
    prism::core::Field<Shape> dropdown{Shape::Triangle};
    prism::core::Field<prism::ui::Label<>> label{{"A label: read-only text"}};
    prism::core::Field<prism::ui::Label<>> heading{{.value = "A heading", .heading = true}};
    prism::core::Field<prism::ui::ProgressBar> progress{{.value = 0.7, .label = "70%"}};
    prism::core::Field<prism::ui::ProgressBar> busy{{.value = 0.0, .busy = true}};
    prism::core::Field<prism::ui::Button> button{{.text = "Save", .icon = icon("save.svg")}};
    prism::core::Field<prism::ui::Button> quiet{{.text = "Cancel", .primary = false}};
    prism::core::Field<prism::ui::Menu> menu{{.title = "&File", .items = {"&Open…", "&Save", "&Delete"},
                                              .icons = {icon("folder.svg"), icon("save.svg"), icon("trash.svg")}}};
    prism::core::Field<prism::ui::Button> open_dialog{{"Ask a question…"}};
    prism::core::Field<prism::ui::Dialog> dialog{{.title = "Keep changes?", .message = "You edited this sample.",
                                                  .buttons = {"Keep", "Discard"}}};
    prism::core::Field<prism::ui::Button> open_popup{{.text = "Options", .primary = false}};
    prism::core::Field<prism::ui::Popup> popup{{.open = false, .modal = false}};
    prism::core::Field<prism::ui::Checkbox> in_popup{{.checked = true, .label = "Inside the popover"}};
    prism::core::Field<prism::ui::ContextMenu> context{{.items = {"Cut", "Copy", "Paste"}}};
    prism::core::Field<prism::ui::Label<>> right_click{{"Right-click here"}};
    prism::core::Field<prism::ui::Button> hover_me{{"Rest the pointer here"}};
    prism::core::List<std::string> list;
    prism::plot::PlotModel plot;
    std::vector<prism::core::Connection> wiring;

    Samples() {
        for (auto s : {"Milk", "Eggs", "Flour", "Sugar"}) list.push_back(s);
        std::vector<double> xs, ys;
        for (int i = 0; i < 200; ++i) {
            xs.push_back(i * 0.05);
            ys.push_back(std::sin(i * 0.05) * std::exp(-i * 0.005));
        }
        plot.add_series(prism::plot::XYData{std::move(xs), std::move(ys)},
                        prism::plot::SeriesStyle{prism::plot::default_series_colors(prism::ui::default_theme())[0], 2.f});
        wiring.push_back(open_dialog.on_change().connect([this](const auto&) {
            auto d = dialog.get();
            d.open = true;
            dialog.set(d);
        }));
        wiring.push_back(open_popup.on_change().connect([this](const auto&) { popup.set({.open = true, .modal = false}); }));
    }
};

// One widget in the catalog, or a category (no sample) when `category` is empty.
struct Entry {
    std::string name;
    std::string category;   // the name of the category entry it belongs to
    std::string doc;
    std::string code;
    std::function<void(Vb&)> sample;   // places the live sample
};

inline std::vector<Entry> catalog_entries(Samples& s) {
    using E = Entry;
    auto one = [](auto& field) { return [&field](Vb& vb) { vb.widget(field); }; };
    return {
        E{"Inputs", "", "Widgets the user types in, picks from or drags.", "", nullptr},
        E{"TextField", "Inputs", "One line of text: selection, clipboard, undo, input methods, right-to-left text.",
          "core::Field<ui::TextField<>> name{{.placeholder = \"Name\"}};", one(s.text)},
        E{"Password", "Inputs", "A text field that shows dots and never copies its text.",
          "core::Field<ui::Password<>> secret{{.placeholder = \"Password\"}};", one(s.password)},
        E{"TextArea", "Inputs", "Several lines with word wrap and a scrollbar. .read_only shows text that can be copied, .font picks prose or code.",
          "core::Field<ui::TextArea<>> notes{{.rows = 6}};", one(s.area)},
        E{"ComboBox", "Inputs", "Free text with suggestions: type, or pick one with the arrows and Enter.",
          "core::Field<ui::ComboBox> fruit{{.options = {\"Apple\", \"Banana\"}}};", one(s.combo)},
        E{"SpinBox", "Inputs", "A number with steppers: arrows, the wheel, or type it. Integers or decimals.",
          "core::Field<ui::SpinBox<int>> copies{{.value = 1, .min = 1, .max = 99}};", one(s.spin)},
        E{"Slider", "Inputs", "A value in a range: drag, click, or the arrow keys. Horizontal or vertical.",
          "core::Field<ui::Slider<>> volume{{.value = 0.5}};", one(s.slider)},
        E{"Checkbox", "Inputs", "On or off, with a label. A plain Field<bool> is a checkbox too.",
          "core::Field<ui::Checkbox> remember{{.label = \"Remember me\"}};", one(s.check)},
        E{"Radio", "Inputs", "One choice among an enum's values, all visible. RadioGroup takes strings instead.",
          "core::Field<ui::Radio<Shape>> shape{{Shape::Square}};", one(s.radio)},
        E{"Dropdown", "Inputs", "Any enum field: one choice, shown when opened.",
          "core::Field<Shape> shape{Shape::Triangle};", one(s.dropdown)},
        E{"Display", "", "Widgets that show something: text, progress, actions.", "", nullptr},
        E{"Label", "Display", "Read-only text. .heading makes a title.",
          "core::Field<ui::Label<>> status{{\"Ready\"}};", [&s](Vb& vb) { vb.vstack(s.heading, s.label); }},
        E{"ProgressBar", "Display", "How far along work is; .busy for work of unknown length.",
          "core::Field<ui::ProgressBar> done{{.value = 0.7}};", [&s](Vb& vb) { vb.vstack(s.progress, s.busy); }},
        E{"Button", "Display", "An action: .value counts clicks, on_change() runs on each. .icon draws an image, .primary = false is quieter.",
          "core::Field<ui::Button> save{{.text = \"Save\", .icon = icon}};",
          [&s](Vb& vb) { vb.hstack([&] { vb.widget(s.button); vb.gap(8.f); vb.widget(s.quiet); vb.spacer(); }); }},
        E{"Popups", "", "Things drawn above the window: menus, dialogs, popovers, tooltips.", "", nullptr},
        E{"Menu", "Popups", "A menu title; several side by side are a menu bar. \"&File\" makes Alt+F open it; .icons puts an image before each item.",
          "core::Field<ui::Menu> file{{.title = \"&File\", .items = {\"&Open…\"}, .icons = {open_icon}}};",
          [&s](Vb& vb) { vb.hstack([&] { vb.widget(s.menu); vb.spacer(); }); }},
        E{"Dialog", "Popups", "A modal question with buttons; it can hold a form too (vb.dialog(d, content)).",
          "core::Field<ui::Dialog> ask{{.title = \"Keep?\", .buttons = {\"Keep\", \"Discard\"}}};",
          [&s](Vb& vb) { vb.hstack([&] { vb.widget(s.open_dialog); vb.spacer(); }); vb.dialog(s.dialog); }},
        E{"Popup", "Popups", "Widgets in a popover, opened by the app; a click outside closes it.",
          "core::Field<ui::Popup> options{{.modal = false}};",
          [&s](Vb& vb) {
              vb.hstack([&] {
                  vb.widget(s.open_popup);
                  vb.popup(s.popup, [&](Vb& p) { p.widget(s.in_popup); });
                  vb.spacer();
              });
          }},
        E{"ContextMenu", "Popups", "A menu on right-click anywhere in the container it is declared in.",
          "core::Field<ui::ContextMenu> actions{{.items = {\"Copy\"}}};",
          [&s](Vb& vb) { vb.vstack([&] { vb.widget(s.right_click); vb.context_menu(s.context); }); }},
        E{"Tooltip", "Popups", "A hint shown when the pointer rests on a widget.",
          "vb.widget(save).tooltip(\"Write the file to disk\");",
          [&s](Vb& vb) { vb.hstack([&] { vb.widget(s.hover_me).tooltip("This is a tooltip"); vb.spacer(); }); }},
        E{"Data", "", "Views over collections. Only the visible rows cost anything.", "", nullptr},
        E{"List", "Data", "One row per item of a core::List<T>; it follows push_back and erase.",
          "core::List<std::string> items; ... vb.list(items);", [&s](Vb& vb) { vb.list(s.list); }},
        E{"Plot", "Data", "A chart: drag to pan, the wheel to zoom, right-click to fit.",
          "plot::PlotModel plot; ... vb.canvas(plot).depends_on(plot.revision);",
          [&s](Vb& vb) { vb.canvas(s.plot).depends_on(s.plot.x_range, s.plot.y_range, s.plot.view, s.plot.cursor, s.plot.revision); }},
    };
}

// The entries as a tree (the hand-written TreeStorage tier: no reflection needed). A node's id is
// its entry's index.
struct CatalogTree {
    const std::vector<Entry>* entries;
    std::size_t root_count() const { return children_of("").size(); }
    prism::ui::TreeNodeId root_at(std::size_t i) const { return children_of("")[i]; }
    std::size_t child_count(prism::ui::TreeNodeId id) const { return children_of(category_name(id)).size(); }
    prism::ui::TreeNodeId child_at(prism::ui::TreeNodeId id, std::size_t i) const { return children_of(category_name(id))[i]; }
    std::string label(prism::ui::TreeNodeId id) const { return (*entries)[id].name; }
    bool has_children(prism::ui::TreeNodeId id) const { return child_count(id) > 0; }
    // Image icons (a tree source may provide image(id)): a folder on each category.
    std::shared_ptr<const prism::render::Image> image(prism::ui::TreeNodeId id) const {
        return (*entries)[id].category.empty() ? folder : nullptr;
    }
    std::shared_ptr<const prism::render::Image> folder = icon("folder.svg");

private:
    // A category's name, or nothing for a widget (widgets have no children).
    std::string category_name(prism::ui::TreeNodeId id) const {
        const auto& e = (*entries)[id];
        return e.category.empty() ? e.name : std::string("\x01");
    }
    // simplify: a scan per call, fine for twenty entries; index by category for a large catalog.
    std::vector<prism::ui::TreeNodeId> children_of(const std::string& category) const {
        std::vector<prism::ui::TreeNodeId> out;
        for (std::size_t i = 0; i < entries->size(); ++i)
            if ((*entries)[i].category == category) out.push_back(i);
        return out;
    }
};

// A page's text: the entry's name, what it is for, and its code (read-only, so it can be copied).
struct PageText {
    prism::core::Field<prism::ui::Label<>> name;
    prism::core::Field<prism::ui::TextArea<>> doc;
    prism::core::Field<prism::ui::TextArea<>> code;
    prism::core::Field<prism::ui::Label<>> try_it{{"Try it:"}};
    explicit PageText(const Entry& e)
        : name{{.value = e.name, .heading = true}},
          doc{{.value = e.doc, .rows = 2, .read_only = true, .font = prism::render::FontRole::Ui}},
          code{{.value = e.code, .rows = 2, .read_only = true}} {}
};

struct CatalogTab {
    Samples samples;
    std::vector<Entry> entries = catalog_entries(samples);
    std::deque<PageText> text = make_text(entries);   // deque: fields never move
    CatalogTree storage{&entries};
    prism::ui::TreeController tree{prism::ui::wrap_tree_storage(storage)};
    prism::core::Field<prism::ui::TabBar<>> page;
    std::vector<prism::core::Connection> wiring;

    CatalogTab() {
        for (std::size_t i = 0; i < tree.rows.size(); ++i)   // open every category
            if (tree.rows[i].has_children) tree.on_row_clicked(i, tree.rows[i]);
        tree.on_row_clicked(1, tree.rows[1]);   // and show the first widget
        wiring.push_back(tree.selected.on_change().connect([this](const auto& id) {
            if (id) page.set({.selected = static_cast<std::size_t>(*id)});
        }));
        page.set({.selected = static_cast<std::size_t>(tree.selected.get().value_or(0))});
    }

    void view(Vb& vb) {
        vb.hstack([&] {
            vb.tree(tree, /*detail=*/false);
            vb.handle();
            vb.pages(page, [&] {
                for (std::size_t i = 0; i < entries.size(); ++i)
                    vb.tab(entries[i].name, [this, i](Vb& p) { page_view(p, i); });
            });
        });
    }

private:
    void page_view(Vb& vb, std::size_t i) {
        vb.widget(text[i].name);
        vb.widget(text[i].doc);
        if (!entries[i].code.empty()) vb.widget(text[i].code);
        if (!entries[i].sample) return;
        vb.gap(12.f);
        vb.widget(text[i].try_it);
        vb.gap(4.f);
        entries[i].sample(vb);
    }

    static std::deque<PageText> make_text(const std::vector<Entry>& entries) {
        std::deque<PageText> out;
        for (const auto& e : entries) out.emplace_back(e);
        return out;
    }
};

} // namespace gallery
