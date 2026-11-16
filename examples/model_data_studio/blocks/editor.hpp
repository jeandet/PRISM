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

// The query editor and its toolbar. It owns the error mark: SQLite names the token it choked on,
// and the underline that leaves goes away the moment the user types.
struct Editor {
    core::Field<ui::TextArea<>> text{{.value = std::string{kSavedQueries[kOpeningQuery].sql}, .rows = 8}};
    core::Field<ui::Button> run{{.text = "Run",
                                 .primary = true,
                                 .shortcut = ui::parse_shortcut("Ctrl+Enter").value_or(ui::Shortcut{})}};
    core::Field<ui::Button> cancel{{.text = "Cancel", .primary = false}};
    core::Field<ui::Label<>> hint{{.value = "Ctrl+Enter runs the editor's query"}};

    Editor() {
        reactions_.push_back(text.on_change().connect([this](const auto&) { typed(); }));
    }

    std::string sql() const { return value(text); }

    void open(std::string sql) {
        auto area = text.get();
        area.value = std::move(sql);
        area.highlights.clear();
        apply(std::move(area));
    }

    void mark(std::optional<ByteRange> range) {
        if (!range) return;
        auto area = text.get();
        area.highlights = {ui::TextRange{range->begin, range->end}};
        apply(std::move(area));
    }

    void clear_mark() {
        auto area = text.get();
        if (area.highlights.empty()) return;
        area.highlights.clear();
        apply(std::move(area));
    }

    void view(app::WidgetTree::ViewBuilder& vb) {
        vb.vstack([&] {
            vb.toolbar([&] {
                vb.widget(run);
                vb.widget(cancel);
                vb.spacer();
                vb.widget(hint);
            });
            vb.widget(text);
        });
    }

private:
    // Setting the text from here is not the user typing: the mark has to survive it.
    void apply(ui::TextArea<> area) {
        marking_ = true;
        text.set(std::move(area));
        marking_ = false;
    }

    void typed() {
        if (marking_) return;
        clear_mark();
    }

    bool marking_ = false;
    // Declared last, so the connection goes before the field it observes.
    std::vector<core::Connection> reactions_{};
};

} // namespace prism::data_studio::blocks
