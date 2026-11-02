#pragma once

#include <prism/app/platform_services.hpp>
#include <prism/core/mpsc_queue.hpp>

#include <functional>
#include <string>
#include <variant>

namespace prism::backends {

// Platform services for backends whose dialogs belong to one thread (SDL: the main thread).
// Requests queue and wake that thread from anywhere, without blocking; the owning thread calls
// drain() and shows each one, in order. Backend-neutral: only showing them is SDL, as with
// QueuedClipboard.
class QueuedPlatformServices final : public app::PlatformServices {
public:
    struct FileRequest {
        app::WindowId parent;
        app::FileDialog dialog;
        std::function<void(app::FileDialogResult)> done;
    };
    struct MessageRequest {
        app::WindowId parent;
        app::MessageKind kind;
        std::string title;
        std::string text;
    };

    explicit QueuedPlatformServices(std::function<void()> wake) : wake_(std::move(wake)) {}

    void show_file_dialog(app::WindowId parent, app::FileDialog dialog,
                          std::function<void(app::FileDialogResult)> done) override {
        push(FileRequest{parent, std::move(dialog), std::move(done)});
    }
    void show_message(app::WindowId parent, app::MessageKind kind, std::string title, std::string text) override {
        push(MessageRequest{parent, kind, std::move(title), std::move(text)});
    }

    template <typename OnFile, typename OnMessage>
    void drain(OnFile&& on_file, OnMessage&& on_message) {
        while (auto r = pending_.pop()) {
            if (auto* f = std::get_if<FileRequest>(&*r)) on_file(*f);
            else on_message(std::get<MessageRequest>(*r));
        }
    }

private:
    void push(std::variant<FileRequest, MessageRequest> r) {
        pending_.push(std::move(r));
        if (wake_) wake_();
    }

    core::mpsc_queue<std::variant<FileRequest, MessageRequest>> pending_;
    std::function<void()> wake_;
};

} // namespace prism::backends
