#pragma once

#include <prism/core/mpsc_queue.hpp>
#include <prism/ui/clipboard.hpp>

#include <functional>
#include <string>

namespace prism::backends {

// Clipboard for backends whose system clipboard belongs to one thread (SDL: the main thread).
// set_text() queues and wakes that thread from anywhere, without blocking; the owning thread
// calls drain() and applies each text in order. Backend-neutral: only the apply step is SDL.
class QueuedClipboard final : public ui::Clipboard {
public:
    explicit QueuedClipboard(std::function<void()> wake) : wake_(std::move(wake)) {}

    void set_text(std::string utf8) const override {
        pending_.push(std::move(utf8));
        if (wake_) wake_();
    }

    template <typename Apply>
    void drain(Apply&& apply) {
        while (auto text = pending_.pop()) apply(*text);
    }

private:
    mutable core::mpsc_queue<std::string> pending_;
    std::function<void()> wake_;
};

} // namespace prism::backends
