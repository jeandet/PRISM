#include "catalog_tab.hpp"

#include <algorithm>

#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace gallery {

Samples::Samples() : chore_tree{prism::ui::wrap_tree_storage(chores)} {
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
    wire.on(open_dialog, [this](const auto&) { show(dialog); });
    wire.on(open_popup, [this](const auto&) { show(popup); });
}

std::vector<Entry> catalog_entries(Samples& s) {
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

CatalogTab::CatalogTab() : tree{prism::ui::wrap_tree_storage(storage)} {
    for (std::size_t i = 0; i < tree.rows.size(); ++i)   // open every category
        if (tree.rows[i].has_children) tree.on_row_clicked(i, tree.rows[i]);
    tree.on_row_clicked(1, tree.rows[1]);   // and show the first widget
    wire.on(tree.selected, [this](const auto& id) {
        if (id) page.set({.selected = static_cast<std::size_t>(*id)});
    });
    page.set({.selected = static_cast<std::size_t>(tree.selected.get().value_or(0))});
}

void CatalogTab::view(Vb& vb) {
    vb.hstack([&] {
        vb.tree(tree, /*detail=*/false);
        vb.handle();
        vb.pages(page, [&] {
            for (std::size_t i = 0; i < entries.size(); ++i)
                vb.tab(entries[i].name, [this, i](Vb& p) { page_view(p, i); });
        });
    });
}

void CatalogTab::page_view(Vb& vb, std::size_t i) {
    vb.widget(text[i].name);
    vb.widget(text[i].doc);
    if (!entries[i].code.empty())   // as tall as the snippet, up to a few lines
        vb.code_view(text[i].code, "cpp").rows(static_cast<int>(std::min<std::size_t>(text[i].code.size(), 6)));
    if (!entries[i].sample) return;
    vb.gap(12.f);
    vb.widget(text[i].try_it);
    vb.gap(4.f);
    entries[i].sample(vb);
}

} // namespace gallery
