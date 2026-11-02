// Data: a 10,000-row table sorted by clicking a header, and a list the user adds to. (The Catalog
// tab shows a tree.)
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

struct DataTab {
    prism::core::Field<prism::ui::Label<>> title{{.value = "Data", .heading = true}};
    Readings readings;
    prism::core::List<std::string> todo;
    prism::core::Field<prism::ui::TextField<>> new_item{{.placeholder = "New item, then Add"}};
    prism::core::Field<prism::ui::Button> add{{"Add"}};
    prism::core::Field<prism::ui::Button> remove{{.text = "Remove last", .primary = false}};
    std::vector<prism::core::Connection> wiring;

    DataTab() {
        for (auto s : {"Try sorting the table", "Drag the handle between the panes", "Add an item here"}) todo.push_back(s);
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
