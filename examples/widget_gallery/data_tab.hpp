// Data: a 10,000-row table sorted by clicking a header, a tree that loads its children when
// opened, and a list the user adds to.
#pragma once

#include <prism/prism.hpp>

#include <fmt/format.h>

#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace gallery {

// Synthetic readings, computed from the row number: nothing is stored.
struct Readings {
    static constexpr std::array<const char*, 5> sensors{"battery", "bus", "motor", "radio", "heater"};
    static constexpr std::array<const char*, 5> units{"V", "V", "A", "dBm", "°C"};

    std::size_t column_count() const { return 5; }
    std::size_t row_count() const { return 10'000; }
    std::string_view header(std::size_t c) const {
        static constexpr std::array<const char*, 5> h{"#", "Sensor", "Value", "Unit", "Status"};
        return h[c];
    }
    std::string cell_text(std::size_t r, std::size_t c) const {
        double v = value(r);
        switch (c) {
        case 0: return fmt::format("{:05}", r);
        case 1: return sensors[r % sensors.size()];
        case 2: return fmt::format("{:.2f}", v);
        case 3: return units[r % units.size()];
        case 4: return v > 40.0 ? "high" : v < 5.0 ? "low" : "ok";
        default: return {};
        }
    }

private:
    static double value(std::size_t r) {
        double t = static_cast<double>(r);
        return 25.0 + 20.0 * std::sin(t * 0.37) + 5.0 * std::cos(t * 1.91);
    }
};

// A fixed hierarchy as a hand-written TreeStorage (the tier that needs no reflection).
struct Catalog {
    struct Entry {
        std::string label;
        std::size_t parent;   // npos: a root
    };
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);
    std::vector<Entry> entries{
        {"Widgets", npos},  {"Inputs", 0},       {"TextField", 1},  {"ComboBox", 1},  {"SpinBox", 1},
        {"Slider", 1},      {"Display", 0},      {"Label", 6},      {"ProgressBar", 6}, {"Image", 6},
        {"Popups", npos},   {"Menu", 10},        {"Dialog", 10},    {"Popup", 10},    {"Tooltip", 10},
        {"Data", npos},     {"Table", 15},       {"Tree", 15},      {"List", 15},     {"Plot", 15},
    };

    std::size_t root_count() const { return children_of(npos).size(); }
    prism::ui::TreeNodeId root_at(std::size_t i) const { return children_of(npos)[i]; }
    std::size_t child_count(prism::ui::TreeNodeId id) const { return children_of(id).size(); }
    prism::ui::TreeNodeId child_at(prism::ui::TreeNodeId id, std::size_t i) const { return children_of(id)[i]; }
    std::string label(prism::ui::TreeNodeId id) const { return entries[id].label; }
    bool has_children(prism::ui::TreeNodeId id) const { return child_count(id) > 0; }

private:
    // simplify: a scan per call, fine for twenty entries; index by parent for a real catalog.
    std::vector<prism::ui::TreeNodeId> children_of(std::size_t parent) const {
        std::vector<prism::ui::TreeNodeId> out;
        for (std::size_t i = 0; i < entries.size(); ++i)
            if (entries[i].parent == parent) out.push_back(i);
        return out;
    }
};

struct DataTab {
    prism::core::Field<prism::ui::Label<>> title{{.value = "Data", .heading = true}};
    Readings readings;
    Catalog catalog;
    prism::ui::TreeController tree{prism::ui::wrap_tree_storage(catalog)};
    prism::core::List<std::string> todo;
    prism::core::Field<prism::ui::TextField<>> new_item{{.placeholder = "New item, then Add"}};
    prism::core::Field<prism::ui::Button> add{{"Add"}};
    prism::core::Field<prism::ui::Button> remove{{.text = "Remove last", .primary = false}};
    std::vector<prism::core::Connection> wiring;

    DataTab() {
        for (auto s : {"Try sorting the table", "Open a branch of the tree", "Add an item here"}) todo.push_back(s);
        wiring.push_back(add.on_change().connect([this](const auto&) {
            if (new_item.get().value.empty()) return;
            todo.push_back(new_item.get().value);
            new_item.set({.placeholder = new_item.get().placeholder});
        }));
        wiring.push_back(remove.on_change().connect([this](const auto&) {
            if (todo.size() > 0) todo.erase(todo.size() - 1);
        }));
    }

    void view(prism::app::WidgetTree::ViewBuilder& vb) {
        vb.widget(title);
        vb.hstack([&] {
            vb.table(readings).sort(2, /*descending=*/true);
            vb.handle();
            vb.vstack([&] {
                vb.tree(tree);
                vb.hstack([&] {
                    vb.widget(new_item);
                    vb.gap(8.f);
                    vb.widget(add);
                    vb.gap(8.f);
                    vb.widget(remove);
                });
                vb.list(todo);
            });
        });
    }
};

} // namespace gallery
