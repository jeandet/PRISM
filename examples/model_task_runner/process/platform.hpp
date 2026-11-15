#pragma once

// Whether this build can start a process at all. Everything else in `process/` is portable; a
// browser has no shell, so the web build gets the flag and nothing else.

#if !defined(__EMSCRIPTEN__) && !defined(_WIN32)
#define PRISM_TASK_RUNNER_POSIX 1
#else
#define PRISM_TASK_RUNNER_POSIX 0
#endif

namespace prism::runner {

// What the app asks before offering a task: can this build start a process at all?
inline constexpr bool kNativeProcesses = PRISM_TASK_RUNNER_POSIX != 0;

} // namespace prism::runner

// PRISM_TASK_RUNNER_POSIX is left defined on purpose: the app guards its own process code with
// #if, which a constexpr bool cannot do.
