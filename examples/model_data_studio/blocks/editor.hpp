#pragma once

#include <prism/prism.hpp>
#include <prism/ui/shortcut.hpp>

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "../sql/catalog.hpp"
#include "../sql/errors.hpp"

namespace prism::data_studio::blocks {

// The query editor and its toolbar: a SQL code editor (colors, line numbers, Tab to indent). It
// owns the error mark: SQLite names the token it choked on, and the highlight that leaves goes away
// the moment the text changes.
struct Editor {
    text::Document text{kSavedQueries[kOpeningQuery].sql};
    core::Field<std::vector<text::Range>> error{};   // the token SQLite choked on
    core::Field<ui::Button> run{{.text = "Run",
                                 .primary = true,
                                 .shortcut = ui::parse_shortcut("Ctrl+Enter").value_or(ui::Shortcut{})}};
    core::Field<ui::Button> cancel{{.text = "Cancel", .primary = false}};
    core::Field<ui::Label<>> hint{{.value = "Ctrl+Enter runs the editor's query"}};

    Editor() {
        wire.on(text, [this](const text::Change&) { clear_mark(); });
    }

    std::string sql() const { return text.text(); }
    void open(std::string sql) { text.assign(text::Document{sql}); }

    void mark(std::optional<ByteRange> range) {
        if (range) error.set({{text.position_at(range->begin), text.position_at(range->end)}});
    }

    void clear_mark() { error.set({}); }

    void view(app::ViewBuilder& vb) {
        vb.vstack([&] {
            vb.toolbar([&] {
                vb.widget(run);
                vb.widget(cancel);
                vb.spacer();
                vb.widget(hint);
            });
            vb.code_editor(text, "sql").rows(8).highlights(error);
        });
    }

private:
    core::Wiring wire;   // captures `this`: the block never moves
};

} // namespace prism::data_studio::blocks
