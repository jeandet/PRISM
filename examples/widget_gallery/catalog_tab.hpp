// Catalog: every widget in a tree; picking one shows what it is for, the C++ that makes one, and a
// live sample to try. The catalog is data (one Entry per widget); its pages are built from it.
#pragma once

#include "gallery_icons.hpp"

#include <prism/prism.hpp>
#include <prism/widgets/plot.hpp>

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
    prism::ui::TreeController chore_tree;
    prism::plot::PlotModel plot;
    std::vector<prism::core::Connection> wiring;

    Samples();
};

// One widget in the catalog, or a category (no sample) when `category` is empty.
struct Entry {
    std::string name;
    std::string category;   // the name of the category entry it belongs to
    std::string doc;
    std::string code;
    std::function<void(Vb&)> sample;   // places the live sample
};

std::vector<Entry> catalog_entries(Samples& s);   // catalog_tab.cpp

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
    prism::ui::TreeController tree;
    prism::core::Field<prism::ui::TabBar<>> page;
    std::vector<prism::core::Connection> wiring;

    CatalogTab();
    void view(Vb& vb);   // catalog_tab.cpp

private:
    void page_view(Vb& vb, std::size_t i);
    static std::deque<PageText> make_text(const std::vector<Entry>& entries) {
        std::deque<PageText> out;
        for (const auto& e : entries) out.emplace_back(e);
        return out;
    }
};

} // namespace gallery
