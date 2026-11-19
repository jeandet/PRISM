// The state every part of PRISM must see as one object, whichever shared object (libprism, an
// app, a Python extension with hidden symbols) the code reading it was compiled into.
#include <prism/app/model_app.hpp>
#include <prism/core/error_hub.hpp>
#include <prism/core/logic_thread.hpp>
#include <prism/core/read_log.hpp>

#include <cstdio>
#include <cstdlib>

namespace prism::core {
std::atomic<std::thread::id> logic_thread{};

[[noreturn]] void model_thread_violation() {
    std::fputs("prism: a model field set off the app's logic thread: hand it over "
               "with AppContext::post, Shared<T> or Channel<T>\n", stderr);
    std::abort();
}
} // namespace prism::core

namespace prism::core::error_hub_detail {
std::mutex error_handler_mutex;
ErrorHandler error_handler;
} // namespace prism::core::error_hub_detail

namespace prism::core::read_log_detail {
Log*& watching() {
    thread_local Log* log = nullptr;
    return log;
}
} // namespace prism::core::read_log_detail

namespace prism::app {
bool& detail_is_logic_thread() {
    thread_local bool flag = false;
    return flag;
}
bool& detail_in_mutation_batch() {
    thread_local bool flag = false;
    return flag;
}
} // namespace prism::app
