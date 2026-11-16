#pragma once

#include <prism/prism.hpp>

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "../sql/catalog.hpp"

namespace prism::data_studio::blocks {

// The saved queries, and the note that explains the selected one. Clicking a row asks the app to
// put that query in the editor: the editor is not this panel's to touch.
struct Queries {
    explicit Queries(std::function<void(std::size_t)> open) : open_{std::move(open)} {
        for (auto& query : kSavedQueries) names.push_back(std::string{query.name});
    }

    core::List<std::string> names{};
    core::Field<std::optional<std::size_t>> selected{};
    // Wrapped: a saved query's note is a sentence, and the panel is narrower than one.
    core::Derived<ui::Label<>> note{
        [this] {
            auto index = selected.get();
            auto text = index && *index < kSavedQueries.size() ? std::string{kSavedQueries[*index].note}
                                                              : std::string{};
            return ui::Label<>{.value = std::move(text), .wrap = true};
        }};

    void select(std::size_t index) { selected.set(index); }

    void view(app::WidgetTree::ViewBuilder& vb) {
        vb.vstack([&] {
            vb.list<std::string>(names, [this](std::size_t index, const std::string&) { open_(index); })
                .selection(selected);
            vb.gap(6.f);
            vb.widget(note);
        });
    }

private:
    std::function<void(std::size_t)> open_;
};

} // namespace prism::data_studio::blocks
