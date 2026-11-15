// Catalog: every widget in a tree; picking one shows what it is for, the C++ that makes one, and a
// live sample to try. The catalog is data (one Entry per widget); its pages are built from it.
#pragma once

#include "gallery_icons.hpp"

#include <prism/prism.hpp>
#include <prism/widgets/plot.hpp>

#include <cmath>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace gallery {

using Vb = prism::app::WidgetTree::ViewBuilder;

enum class Shape { Circle, Square, Triangle };

// A to-do list as a tree: rooms hold chores; a chore has a checkbox; anything can be renamed.
struct Chores {
    struct Node {
        std::string name;
        std::vector<prism::ui::TreeNodeId> children;
        std::optional<bool> done;   // rooms have no box
    };
    std::vector<Node> nodes{{"Kitchen", {2, 3}, {}}, {"Garden", {4}, {}},
                            {"Dishes", {}, true}, {"Floor", {}, false}, {"Water the plants", {}, false}};
    std::size_t root_count() const { return 2; }
    prism::ui::TreeNodeId root_at(std::size_t i) const { return i; }
    std::size_t child_count(prism::ui::TreeNodeId id) const { return nodes[id].children.size(); }
    prism::ui::TreeNodeId child_at(prism::ui::TreeNodeId id, std::size_t i) const { return nodes[id].children[i]; }
    std::string label(prism::ui::TreeNodeId id) const { return nodes[id].name; }
    bool has_children(prism::ui::TreeNodeId id) const { return !nodes[id].children.empty(); }
    std::optional<bool> checked(prism::ui::TreeNodeId id) const { return nodes[id].done; }
    void set_checked(prism::ui::TreeNodeId id, bool on) { nodes[id].done = on; }
    bool rename(prism::ui::TreeNodeId id, std::string_view to) {
        if (to.empty()) return false;   // refused: the editor stays open, outlined in red
        nodes[id].name = std::string(to);
        return true;
    }
};

// The fields the samples show, with sample data.
struct Samples {
    prism::core::Field<prism::ui::TextField<>> text{{.value = "Ada Lovelace", .placeholder = "Name"}};
    prism::core::Field<prism::ui::Password<>> password{{.value = "hunter2", .placeholder = "Password"}};
    prism::core::Field<prism::ui::TextArea<>> area{{.value = "Several lines\nof editable text,\nwith undo.", .rows = 4}};
    prism::core::Field<prism::ui::ComboBox> combo{{.value = "Ap", .placeholder = "Fruit",
                                                   .options = {"Apple", "Apricot", "Banana", "Cherry"}}};
    prism::core::Field<prism::ui::SpinBox<int>> spin{{.value = 7, .min = 0, .max = 20, .step = 1}};
    prism::core::Field<prism::ui::Slider<>> slider{{.value = 30.0, .min = 0.0, .max = 100.0, .step = 1.0, .label = "Volume", .unit = "%"}};
    prism::core::Field<prism::ui::Checkbox> check{{.checked = true, .label = "Remember me"}};
    prism::core::Field<prism::ui::Radio<Shape>> radio{{Shape::Square}};
    prism::core::Field<Shape> dropdown{Shape::Triangle};
    prism::core::Field<prism::ui::Label<>> label{{"A label: read-only text"}};
    prism::core::Field<prism::ui::Label<>> heading{{.value = "A heading", .heading = true}};
    prism::core::Field<prism::ui::Label<>> wrapped{{.value = "A label with .wrap breaks into lines to fit its width, "
                                                             "and grows as tall as they are.", .wrap = true}};
    prism::core::Field<prism::ui::Choice> port{{.options = {"/dev/ttyUSB0", "/dev/ttyUSB1", "COM3"},
                                                .placeholder = "Pick a port"}};
    prism::core::Field<prism::ui::Link> link{{.text = "PRISM on GitHub", .url = "https://github.com/jeandet/PRISM"}};
    prism::core::Field<prism::ui::Picture> picture{{.image = picture_of("info.svg", 64), .description = "An info sign"}};
    prism::core::Field<prism::ui::Button> toggle{{.text = "Bold", .primary = false, .checked = false}};
    prism::core::Field<prism::ui::Button> tool_open{{.text = "", .icon = icon("folder.svg"), .primary = false}};
    prism::core::Field<prism::ui::Button> tool_bold{{.text = "Bold", .primary = false, .checked = true}};
    prism::core::Field<prism::ui::Label<>> status{{"Saved"}};
    prism::core::Field<prism::ui::Checkbox> grouped{{.checked = true, .label = "Inside a group"}};
    prism::core::Field<prism::ui::Expander> advanced{{.title = "Advanced"}};
    prism::core::Field<prism::ui::Checkbox> verbose{{.checked = false, .label = "Verbose logs"}};
    prism::core::Field<prism::ui::ProgressBar> progress{{.value = 0.7, .label = "70%"}};
    prism::core::Field<prism::ui::ProgressBar> busy{{.value = 0.0, .busy = true}};
    prism::core::Field<prism::ui::Button> button{{.text = "Save", .icon = icon("save.svg")}};
    prism::core::Field<prism::ui::Button> quiet{{.text = "Cancel", .primary = false}};
    prism::core::Field<prism::ui::Menu> menu{{.title = "&File", .items = {
        {.text = "&Open…", .icon = icon("folder.svg")},
        {.text = "Open &recent", .items = {{"notes.txt"}, {"todo.txt"}, {.text = "Older", .items = {{"draft.txt"}}}}},
        {.text = "&Save", .shortcut = {prism::input::keys::s, prism::input::mods::ctrl}, .icon = icon("save.svg")},
        {.separator = true},
        {.text = "&Delete", .icon = icon("trash.svg"), .enabled = false},
        {.text = "&Word wrap", .checked = true},
    }}};
    prism::core::Field<prism::ui::Button> open_dialog{{"Ask a question…"}};
    prism::core::Field<prism::ui::Dialog> dialog{{.title = "Keep changes?", .message = "You edited this sample.",
                                                  .buttons = {"Keep", "Discard"}}};
    prism::core::Field<prism::ui::Button> open_popup{{.text = "Options", .primary = false}};
    prism::core::Field<prism::ui::Popup> popup{{.open = false, .modal = false}};
    prism::core::Field<prism::ui::Checkbox> in_popup{{.checked = true, .label = "Inside the popover"}};
    prism::core::Field<prism::ui::ContextMenu> context{{.items = {{"Cut"}, {"Copy"}, {"Paste"}}}};
    prism::core::Field<prism::ui::Label<>> right_click{{"Right-click here"}};
    prism::core::Field<prism::ui::Button> hover_me{{"Rest the pointer here"}};
    // Drag and drop: chips that drag their name, and the label that takes one.
    prism::core::Field<prism::ui::Button> chip_a{{"Alpha"}};
    prism::core::Field<prism::ui::Button> chip_b{{"Beta"}};
    prism::core::Field<prism::ui::Button> chip_c{{"Gamma"}};
    prism::core::Field<prism::ui::Label<>> drop_zone{{"Drop a chip here"}};
    prism::core::List<std::string> list;
    prism::core::Field<std::optional<std::size_t>> list_selected{std::nullopt};
    prism::core::Field<prism::core::Color> ink{prism::core::Color::rgba(0x33, 0x66, 0xCC)};
    prism::core::Field<std::chrono::year_month_day> day{std::chrono::year{2026} / std::chrono::November / 14};
    prism::core::Field<prism::ui::TimeOfDay> alarm{{7, 30}};
    Chores chores;
    prism::ui::TreeController chore_tree{prism::ui::wrap_tree_storage(chores)};
    prism::plot::PlotModel plot;
    std::vector<prism::core::Connection> wiring;

    Samples() {
        for (auto s : {"Milk", "Eggs", "Flour", "Sugar"}) list.push_back(s);
        for (std::size_t i = chore_tree.rows.size(); i-- > 0;)   // open the rooms
            if (chore_tree.rows[i].has_children) chore_tree.on_row_clicked(i, chore_tree.rows[i]);
        chore_tree.selected.set(std::nullopt);
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
        E{"Slider", "Inputs", "A value in a range: drag, click, or the arrows. A label shows the value.",
          "core::Field<ui::Slider<>> volume{{.value = 30, .max = 100, .step = 1, .label = \"Volume\", .unit = \"%\"}};",
          one(s.slider)},
        E{"Checkbox", "Inputs", "On or off, with a label. A plain Field<bool> is a checkbox too.",
          "core::Field<ui::Checkbox> remember{{.label = \"Remember me\"}};", one(s.check)},
        E{"Radio", "Inputs", "One choice among an enum's values, all visible. RadioGroup takes strings instead.",
          "core::Field<ui::Radio<Shape>> shape{{Shape::Square}};", one(s.radio)},
        E{"Dropdown", "Inputs", "Any enum field: one choice, shown when opened.",
          "core::Field<Shape> shape{Shape::Triangle};", one(s.dropdown)},
        E{"Choice", "Inputs", "One of a list known only at run time (ports, recent files); nothing else, unlike a ComboBox.",
          "core::Field<ui::Choice> port{{.options = ports(), .placeholder = \"Pick a port\"}};", one(s.port)},
        E{"Color", "Inputs", "A swatch and its hex code. A click opens a saturation/brightness square and a hue bar; the arrows nudge it.",
          "core::Field<core::Color> ink{core::Color::rgba(0x33, 0x66, 0xCC)};", one(s.ink)},
        E{"Date", "Inputs", "A date. A click opens a month calendar; the arrows move by day and week, Page Up/Down by month.",
          "core::Field<std::chrono::year_month_day> day{2026y / November / 14};", one(s.day)},
        E{"Time", "Inputs", "A time of day. Click the hour or the minutes, then Up/Down, the wheel or digits; Left/Right switch.",
          "core::Field<ui::TimeOfDay> alarm{{7, 30}};", one(s.alarm)},
        E{"Display", "", "Widgets that show something: text, progress, actions.", "", nullptr},
        E{"Label", "Display", "Read-only text. .heading makes a title; .wrap breaks it into lines.",
          "core::Field<ui::Label<>> status{{\"Ready\"}};", [&s](Vb& vb) { vb.vstack(s.heading, s.label, s.wrapped); }},
        E{"Link", "Display", "Text that opens its URL in the browser; .value counts the clicks either way.",
          "core::Field<ui::Link> docs{{.text = \"Docs\", .url = \"https://...\"}};", one(s.link)},
        E{"Picture", "Display", "An image on its own, at its size or fitted into .size with its proportions kept.",
          "core::Field<ui::Picture> logo{{.image = *render::load_image(\"logo.png\")}};", one(s.picture)},
        E{"ProgressBar", "Display", "How far along work is; .busy for work of unknown length.",
          "core::Field<ui::ProgressBar> done{{.value = 0.7}};", [&s](Vb& vb) { vb.vstack(s.progress, s.busy); }},
        E{"Button", "Display", "An action: .value counts clicks, on_change() runs on each. .icon draws an image, .primary = false is quieter, .shortcut presses it from anywhere, and .checked makes a toggle.",
          "core::Field<ui::Button> save{{.text = \"Save\", .icon = icon}};",
          [&s](Vb& vb) { vb.hstack([&] { vb.widget(s.button); vb.gap(8.f); vb.widget(s.quiet); vb.gap(8.f); vb.widget(s.toggle); vb.spacer(); }); }},
        E{"Layout", "", "Containers with a look of their own.", "", nullptr},
        E{"Toolbar", "Layout", "A row of tools across the window, on its chrome colour; toggles stay pressed.",
          "vb.toolbar([&] { vb.widget(open); vb.widget(bold); });",
          [&s](Vb& vb) { vb.toolbar([&] { vb.widget(s.tool_open); vb.widget(s.tool_bold); vb.spacer(); }); }},
        E{"StatusBar", "Layout", "A row of short status texts, usually last in the window.",
          "vb.status_bar([&] { vb.widget(status); });",
          [&s](Vb& vb) { vb.status_bar([&] { vb.widget(s.status); vb.spacer(); }); }},
        E{"Group", "Layout", "Widgets that belong together, outlined under a title.",
          "vb.group(\"Options\", [&] { vb.widget(wrap); });",
          [&s](Vb& vb) { vb.group("Options", [&] { vb.widget(s.grouped); }); }},
        E{"Section", "Layout", "A titled part the user folds and unfolds; its content exists only while open.",
          "vb.section(advanced, [&](auto& inner) { inner.widget(verbose); });",
          [&s](Vb& vb) { vb.section(s.advanced, [&s](Vb& inner) { inner.widget(s.verbose); }); }},
        E{"Popups", "", "Things drawn above the window: menus, dialogs, popovers, tooltips.", "", nullptr},
        E{"Menu", "Popups", "A menu title; several side by side are a menu bar. \"&File\" makes Alt+F open it. An item can have an icon, a shortcut (Ctrl+S works from anywhere), a check mark, or be greyed out; {.separator = true} draws a line, and .items opens a submenu.",
          "core::Field<ui::Menu> file{{.title = \"&File\", .items = {\n    {.text = \"&Save\", .shortcut = {keys::s, mods::ctrl}},\n    {.separator = true},\n    {.text = \"&Word wrap\", .checked = true}}}};",
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
          "core::Field<ui::ContextMenu> actions{{.items = {{\"Copy\"}}}};",
          [&s](Vb& vb) { vb.vstack([&] { vb.widget(s.right_click); vb.context_menu(s.context); }); }},
        E{"Tooltip", "Popups", "A hint shown when the pointer rests on a widget.",
          "vb.widget(save).tooltip(\"Write the file to disk\");",
          [&s](Vb& vb) { vb.hstack([&] { vb.widget(s.hover_me).tooltip("This is a tooltip"); vb.spacer(); }); }},
        E{"Data", "", "Views over collections. Only the visible rows cost anything.", "", nullptr},
        E{"List", "Data", "One row per item of a core::List<T>; it follows push_back and erase. .selection(field) keeps the selected row; a click or the arrow keys move it.",
          "core::List<std::string> items; ... vb.list(items).selection(selected);",
          [&s](Vb& vb) { vb.list(s.list).selection(s.list_selected); }},
        E{"Tree", "Data", "Nodes from your source. Ctrl/Shift-click select several; checked() adds boxes, rename() lets F2 rename.",
          "ui::TreeController tree{ui::wrap_tree_storage(chores)}; ... vb.tree(tree, false);",
          [&s](Vb& vb) { vb.tree(s.chore_tree, /*detail=*/false); }},
        E{"Plot", "Data", "A chart: drag to pan, the wheel to zoom, right-click to fit.",
          "plot::PlotModel plot; ... vb.canvas(plot).depends_on(plot.revision);",
          [&s](Vb& vb) { vb.canvas(s.plot).depends_on(s.plot.x_range, s.plot.y_range, s.plot.view, s.plot.cursor, s.plot.revision); }},
        E{"Interaction", "", "What the user does to widgets beyond clicking them.", "", nullptr},
        E{"Drag and drop", "Interaction",
          "Drag a chip onto the zone below. A drag carries a value of any type; a target takes one type.",
          "vb.widget(chip).drag_source(value); vb.widget(zone).drop_target(on_drop);",
          [&s](Vb& vb) {
              vb.vstack([&] {
                  vb.hstack([&] {
                      vb.widget(s.chip_a).drag_source([] { return std::string("Alpha"); });
                      vb.gap(8.f);
                      vb.widget(s.chip_b).drag_source([] { return std::string("Beta"); });
                      vb.gap(8.f);
                      vb.widget(s.chip_c).drag_source([] { return std::string("Gamma"); });
                      vb.spacer();
                  });
                  vb.widget(s.drop_zone).drop_target([&s](const std::string& v, prism::ui::DropPlace) {
                      s.drop_zone.set({v});
                  });
              });
          }},
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
