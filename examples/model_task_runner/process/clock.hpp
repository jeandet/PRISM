#pragma once

// The clock the runs and the status bar count in. One name for it, so a script's timings and the
// elapsed time a console shows are the same kind of number.

#include <chrono>

namespace prism::runner {

using steady = std::chrono::steady_clock;

// A length in seconds as a steady duration, for a script that says when a line is due.
inline steady::duration seconds(double value) {
    return std::chrono::duration_cast<steady::duration>(std::chrono::duration<double>(value));
}

} // namespace prism::runner
