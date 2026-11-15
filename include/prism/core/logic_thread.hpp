#pragma once

#include <atomic>
#include <thread>

namespace prism::core {

// The thread that owns the app's model while an app runs (model_app's logic thread); none
// otherwise. Other threads hand their changes over (AppContext::post, Shared<T>, Channel<T>).
// Defined once, in libprism (src/core/shared_state.cpp): an inline variable gets one copy per
// shared object that hides its symbols (a Python extension does), and the copies disagree.
extern std::atomic<std::thread::id> logic_thread;

// Whether this thread may change the model now: no app runs, or this is its logic thread.
inline bool may_change_model() {
    auto owner = logic_thread.load(std::memory_order_relaxed);
    return owner == std::thread::id{} || owner == std::this_thread::get_id();
}

} // namespace prism::core
