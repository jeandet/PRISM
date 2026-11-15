// The state every part of PRISM must see as one object, whichever shared object (libprism, an
// app, a Python extension with hidden symbols) the code reading it was compiled into.
#include <prism/app/model_app.hpp>
#include <prism/core/error_hub.hpp>
#include <prism/core/logic_thread.hpp>
#include <prism/core/read_log.hpp>

namespace prism::core {
std::atomic<std::thread::id> logic_thread{};
} // namespace prism::core

namespace prism::core::error_hub_detail {
std::mutex error_handler_mutex;
ErrorHandler error_handler;
} // namespace prism::core::error_hub_detail

#ifndef NDEBUG
namespace prism::core::read_log_detail {
thread_local Log* watching = nullptr;
} // namespace prism::core::read_log_detail
#endif

namespace prism::app {
thread_local bool detail_is_logic_thread = false;
thread_local bool detail_in_mutation_batch = false;
} // namespace prism::app
