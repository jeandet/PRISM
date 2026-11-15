#pragma once

#include <prism/prism.hpp>

#include <cstdint>
#include <string>

#include "../sql/catalog.hpp"
#include "../sql/status.hpp"

namespace prism::data_studio::blocks {

namespace schema_detail {

inline core::Point at(float x, float y) { return core::Point{core::X{x}, core::Y{y}}; }

} // namespace schema_detail

// The schema panel: the two tables and their columns, with the trips row count beside the table
// once the dataset has loaded. It reads the count the status bar owns, nothing else.
struct Schema {
    explicit Schema(core::Field<std::uint32_t>& loaded_rows) : rows{loaded_rows} {}

    core::Field<std::uint32_t>& rows;

    void view(app::WidgetTree::ViewBuilder& vb) { vb.canvas(*this).depends_on(rows); }

    void canvas(render::DrawList& dl, core::Rect bounds, const ui::WidgetNode& node) {
        const auto& theme = ui::node_theme(node);
        constexpr float kIndent = 12.f;
        constexpr float kTypeColumn = 132.f;
        constexpr float kRowHeight = 15.f;
        auto x = bounds.origin.x.raw() + 12.f;
        auto y = bounds.origin.y.raw() + 10.f;
        const auto count = rows.get();

        dl.clip_push(bounds.origin, bounds.extent);
        for (auto& table : kSchema) {
            dl.text(std::string{table.name}, schema_detail::at(x, y), 13.f, theme.text);
            if (table.name == "trips" && count > 0)
                dl.text(rows_text(count), schema_detail::at(x + kTypeColumn, y + 1.f), 12.f, theme.accent);
            y += kRowHeight + 4.f;
            for (auto& column : table.columns) {
                dl.text(std::string{column.name}, schema_detail::at(x + kIndent, y), 11.5f, theme.text_muted, 0.f,
                        render::TextAnchor::TopLeft, render::FontRole::Mono);
                dl.text(std::string{column.type}, schema_detail::at(x + kTypeColumn, y), 11.5f,
                        theme.text_placeholder, 0.f, render::TextAnchor::TopLeft, render::FontRole::Mono);
                y += kRowHeight;
            }
            y += 8.f;
        }
        dl.clip_pop();
    }
};

} // namespace prism::data_studio::blocks
