#pragma once

#include <prism/core/error_hub.hpp>

#include <coroutine>
#include <exception>

// Opt-in coroutines: a function returning app::Task may co_await (a net::Client reply, ...). It
// starts at once and runs until its first co_await; what follows runs where the awaited thing
// resumes it -- for net::Client, the logic thread, so it may set fields. Nobody waits for a
// Task: it frees itself at its end, and an exception it lets out goes to the app's error handler.
namespace prism::app {

struct Task {
    struct promise_type {
        Task get_return_object() noexcept { return {}; }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() noexcept { core::report_unhandled_error(std::current_exception()); }
    };
};

} // namespace prism::app
