#pragma once

// The tasks the console offers, and which of them this build can actually run: what a task is,
// is data.

#include "platform.hpp"
#include "simulation.hpp"

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace prism::runner {

struct TaskSpec {
    std::string_view id{};
    std::string_view name{};
    std::string_view summary{};
    std::string_view command{};          // what the task runs, shown in the console header
    std::optional<SimKind> simulated{};  // empty: run `command` for real (desktop only)
};

// A browser has no shell: on the web only the simulated tasks and the custom box are offered
// (and a custom run explains what it cannot do there).
inline constexpr bool task_available(const TaskSpec& spec, bool native_processes) {
    return native_processes || spec.simulated.has_value() || spec.command.empty();
}

inline constexpr TaskSpec kTasks[] = {
    {.id = "build", .name = "Build", .summary = "A simulated 120-unit compile: progress, warnings, the odd error.",
     .command = "ninja -C builddir", .simulated = SimKind::Build},
    {.id = "tests", .name = "Unit tests", .summary = "A simulated test runner: suites, cases, one failure now and then.",
     .command = "ctest --output-on-failure", .simulated = SimKind::Tests},
    {.id = "flood", .name = "Flood", .summary = "200 000 lines as fast as the console takes them.",
     .command = "seq 200000", .simulated = SimKind::Flood},
    {.id = "ping", .name = "ping", .summary = "A real process: five ICMP echoes to loopback.",
     .command = "ping -c 5 127.0.0.1"},
    {.id = "ls", .name = "ls", .summary = "A real process: the contents of /usr/bin.",
     .command = "ls -la /usr/bin"},
    {.id = "uname", .name = "uname", .summary = "A real process: the kernel this runs on.",
     .command = "uname -a"},
    {.id = "custom", .name = "Custom", .summary = "Whatever the box beside the list holds, run by the shell.",
     .command = ""},
};

inline constexpr std::size_t kTaskCount = std::size(kTasks);
inline constexpr std::size_t kCustomTask = kTaskCount - 1;

// The two tasks the headless capture seeds its frame from, named so the app does not index the
// catalogue by number.
inline constexpr std::size_t kBuildTask = 0;
inline constexpr std::size_t kFloodTask = 2;
static_assert(kTasks[kBuildTask].simulated == SimKind::Build);
static_assert(kTasks[kFloodTask].simulated == SimKind::Flood);

// The tasks this build offers, in catalogue order: one tab and one row each.
inline std::vector<std::size_t> offered_tasks() {
    std::vector<std::size_t> offered;
    offered.reserve(kTaskCount);
    for (std::size_t task = 0; task < kTaskCount; ++task)
        if (task_available(kTasks[task], kNativeProcesses)) offered.push_back(task);
    return offered;
}

} // namespace prism::runner
