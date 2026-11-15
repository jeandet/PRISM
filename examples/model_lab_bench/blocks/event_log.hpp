#pragma once

#include <prism/prism.hpp>

#include <chrono>
#include <cstddef>
#include <fmt/format.h>
#include <string>
#include <string_view>

namespace prism::lab::blocks {

// The event log: stamped lines, newest last, capped so a long session cannot grow without bound.
// It stamps on arrival, so everything it prints shares the model's clock.
struct EventLog {
    core::List<std::string> lines{};

    void push(std::string_view message, bool is_error = false) {
        lines.push_back(
            fmt::format("[{:7.3f}s] {} {}", seconds_since_start(), is_error ? "[ERR ]" : "[INFO]", message));
        if (lines.size() > kMaxLines) lines.erase(0);
    }

    void view(app::WidgetTree::ViewBuilder& vb) { vb.list(lines); }

private:
    static constexpr std::size_t kMaxLines = 150;
    using steady = std::chrono::steady_clock;

    double seconds_since_start() const {
        return std::chrono::duration<double>(steady::now() - start_).count();
    }

    steady::time_point start_{steady::now()};
};

} // namespace prism::lab::blocks
