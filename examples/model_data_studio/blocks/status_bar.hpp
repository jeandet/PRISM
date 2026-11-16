#pragma once

#include <prism/prism.hpp>

#include <cmath>
#include <cstdint>
#include <fmt/format.h>
#include <string>

#include "../sql/status.hpp"

namespace prism::data_studio::blocks {

namespace status_bar_detail {

// Loading knows its fraction; a running query does not, and SQLite will not pretend it does.
// Idle is not busy either: a `busy` bar asks for a frame every 33 ms, which is not something to
// do while nothing is running.
inline ui::ProgressBar progress_bar(const Status& status) {
    if (!working(status.kind)) return {.value = 0.0, .min = 0.0, .max = 1.0};
    if (status.kind == StatusKind::Loading) return {.value = status.progress, .min = 0.0, .max = 1.0};
    return {.busy = true};
}

} // namespace status_bar_detail

// The status bar: one line of words and a progress bar, both read from `state`, so the app never
// touches either control. It also owns the loaded-row count the schema panel prints.
struct StatusBar {
    core::Field<Status> state{};
    core::Field<std::uint32_t> loaded_rows{0};

    core::Derived<std::string> text{[this] { return status_line(state.get()); }};
    core::Derived<ui::ProgressBar> bar{[this] { return status_bar_detail::progress_bar(state.get()); }};

    core::Field<std::uint32_t>& rows() { return loaded_rows; }
    std::size_t loaded() const { return loaded_rows.get(); }
    const Status& status() const { return state.get(); }
    bool busy() const { return working(state.get().kind); }

    void set(StatusKind kind, std::string text) { state.set({.kind = kind, .text = std::move(text)}); }

    // The loading line carries a fraction as well as words, so it cannot go through set().
    void set_loading(std::size_t done, std::size_t total) {
        const auto progress = total ? static_cast<double>(done) / static_cast<double>(total) : 0.0;
        loaded_rows.set(static_cast<std::uint32_t>(done));
        state.set({.kind = StatusKind::Loading,
                   .text = fmt::format("loading {} trips - {}%",
                                       with_thousands(static_cast<std::int64_t>(total)),
                                       std::lround(progress * 100.0)),
                   .progress = progress});
    }

    void set_loaded(std::size_t rows) { loaded_rows.set(static_cast<std::uint32_t>(rows)); }

    // Plain widgets only, and called inside the bar rather than placed as a block: a canvas takes
    // whatever it is given (the whole bar), and block() would wrap these in a container the spacer
    // could not spread. The bar stays in the tree and reports itself idle, so the layout never jumps.
    void view(app::WidgetTree::ViewBuilder& vb) {
        vb.widget(text);
        vb.spacer();
        vb.widget(bar);
    }
};

} // namespace prism::data_studio::blocks
