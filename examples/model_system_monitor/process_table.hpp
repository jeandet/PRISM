#pragma once

#include "proc_metrics.hpp"

#include <fmt/format.h>

#include <array>
#include <string>
#include <string_view>
#include <vector>

// The process list as a table (a hand-written ColumnStorage: needs no reflection, so the monitor
// builds everywhere, the web included). Clicking a header sorts it.
class ProcessTable {
public:
    void update(std::vector<ProcessInfo> processes) { processes_ = std::move(processes); }

    std::size_t column_count() const { return 4; }
    std::size_t row_count() const { return processes_.size(); }
    std::string_view header(std::size_t c) const {
        static constexpr std::array<std::string_view, 4> h{"PID", "Name", "CPU %", "Mem %"};
        return h[c];
    }
    std::string cell_text(std::size_t r, std::size_t c) const {
        const auto& p = processes_[r];
        switch (c) {
        case 0: return fmt::to_string(p.pid);
        case 1: return p.name;
        case 2: return fmt::format("{:.1f}", p.cpu_percent);
        case 3: return fmt::format("{:.1f}", p.mem_percent);
        default: return {};
        }
    }

private:
    std::vector<ProcessInfo> processes_;
};
