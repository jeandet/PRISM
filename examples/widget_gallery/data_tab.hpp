// Data: a 10,000-row table sorted by clicking a header, and a list the user adds to. (The Catalog
// tab shows a tree.)
#pragma once

#include "gallery_icons.hpp"

#include <prism/prism.hpp>

#include <fmt/format.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace gallery {

// Synthetic readings, computed from the row number: nothing is stored but the rows the user
// watches (a checkbox column) and the notes typed into the last column (double-click or F2).
struct Readings {
    std::map<std::size_t, std::string> notes;
    std::set<std::size_t> watched;
    std::shared_ptr<const prism::render::Image> alert = icon("info.svg");

    static constexpr std::array<const char*, 5> sensors{"battery", "bus", "motor", "radio", "heater"};
    static constexpr std::array<const char*, 5> units{"V", "V", "A", "dBm", "°C"};

    std::size_t column_count() const { return 7; }
    std::size_t row_count() const { return 10'000; }
    std::string_view header(std::size_t c) const {
        static constexpr std::array<const char*, 7> h{"#", "Watch", "Sensor", "Value", "Unit", "Status", "Note"};
        return h[c];
    }
    std::string cell_text(std::size_t r, std::size_t c) const {
        double v = value(r);
        switch (c) {
        case 0: return fmt::format("{:05}", r);
        case 1: return watched.contains(r) ? "yes" : "no";   // sorts the watched rows together
        case 2: return sensors[r % sensors.size()];
        case 3: return fmt::format("{:.2f}", v);
        case 4: return units[r % units.size()];
        case 5: return v > 40.0 ? "high" : v < 5.0 ? "low" : "ok";
        case 6: { auto it = notes.find(r); return it == notes.end() ? std::string{} : it->second; }
        default: return {};
        }
    }
    std::optional<bool> cell_check(std::size_t r, std::size_t c) const {
        return c == 1 ? std::optional<bool>{watched.contains(r)} : std::nullopt;
    }
    std::shared_ptr<const prism::render::Image> cell_image(std::size_t r, std::size_t c) const {
        return c == 5 && cell_text(r, c) != "ok" ? alert : nullptr;
    }
    bool editable(std::size_t c) const { return c == 1 || c == 6; }
    bool set_cell(std::size_t r, std::size_t c, std::string_view text) {
        if (c == 1) {
            if (text == "true") watched.insert(r);
            else watched.erase(r);
        } else if (text.empty()) {
            notes.erase(r);
        } else {
            notes[r] = std::string(text);
        }
        return true;
    }

private:
    static double value(std::size_t r) {
        double t = static_cast<double>(r);
        return 25.0 + 20.0 * std::sin(t * 0.37) + 5.0 * std::cos(t * 1.91);
    }
};

struct DataTab {
    prism::core::Field<prism::ui::Label<>> title{{.value = "Data", .heading = true}};
    Readings readings;
    // The columns shown (a header drag reorders them) and a right-click menu that hides them.
    prism::core::Field<std::vector<std::size_t>> columns{};
    prism::core::Field<prism::ui::ContextMenu> column_menu{{.items = column_items(readings)}};
    std::uint64_t menu_seen = 0;
    prism::core::List<std::string> todo;
    prism::core::Field<prism::ui::TextField<>> new_item{{.placeholder = "New item, then Add"}};
    prism::core::Field<prism::ui::Button> add{{"Add"}};
    prism::core::Field<prism::ui::Button> remove{{.text = "Remove last", .primary = false}};
    std::vector<prism::core::Connection> wiring;

    DataTab();
    void view(prism::app::WidgetTree::ViewBuilder& vb);   // data_tab.cpp

private:
    static std::vector<prism::ui::MenuItem> column_items(const Readings& r) {
        std::vector<prism::ui::MenuItem> items;
        for (std::size_t c = 0; c < r.column_count(); ++c)
            items.push_back({.text = std::string(r.header(c)), .checked = true});
        return items;
    }
    void show_ticked(prism::ui::ContextMenu m);
};

} // namespace gallery
