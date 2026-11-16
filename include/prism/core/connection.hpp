#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace prism::core {

class Connection {
public:
    Connection() = default;

    Connection(std::shared_ptr<std::function<void()>> detach)
        : detach_(std::move(detach)) {}

    // Keepalive: optional shared ownership that lives as long as the Connection
    // does. Used by Python Bound* handles to keep their Slot alive even if the
    // Model is freed before the Connection is disconnected (fixes UAF after
    // removal of nanobind keep_alive on Bound* — see Python fix dea637d review).
    explicit Connection(std::shared_ptr<std::function<void()>> detach,
                        std::shared_ptr<void> keepalive)
        : detach_(std::move(detach)), keepalive_(std::move(keepalive)) {}

    ~Connection() { disconnect(); }

    Connection(Connection&& o) noexcept
        : detach_(std::move(o.detach_)), keepalive_(std::move(o.keepalive_)) {}
    Connection& operator=(Connection&& o) noexcept {
        if (this != &o) {
            disconnect();
            detach_ = std::move(o.detach_);
            keepalive_ = std::move(o.keepalive_);
        }
        return *this;
    }

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    void disconnect() {
        if (auto d = std::move(detach_)) {
            if (*d) (*d)();
        }
        keepalive_.reset();
    }

    // Attach additional ownership without replacing detach — cheap extend.
    void keep_alive(std::shared_ptr<void> h) { keepalive_ = std::move(h); }

private:
    std::shared_ptr<std::function<void()>> detach_;
    std::shared_ptr<void> keepalive_;
};

// A signal: callbacks connected to it run on emit(). Like a QObject, it may die before the
// Connections it handed out: they share its receiver list only weakly, so a Connection that
// outlives its hub disconnects from nothing. Moving a hub moves that list, and the Connections
// follow it.
template <typename... Args>
class SenderHub {
public:
    using Callback = std::function<void(Args...)>;

    SenderHub() = default;
    SenderHub(const SenderHub&) = delete;
    SenderHub& operator=(const SenderHub&) = delete;

    SenderHub(SenderHub&& other) noexcept : state_(std::exchange(other.state_, nullptr)) {}
    SenderHub& operator=(SenderHub&& other) noexcept {
        if (this != &other) state_ = std::exchange(other.state_, nullptr);   // ours die: their Connections go inert
        return *this;
    }

    [[nodiscard]] Connection connect(Callback cb) {
        if (!state_) state_ = std::make_shared<State>();   // a moved-from hub, used again
        uint64_t id;
        {
            std::lock_guard<std::mutex> lk(state_->mutex);
            id = state_->next_id++;
            state_->receivers.push_back({id, std::move(cb)});
        }
        auto detach = std::make_shared<std::function<void()>>(
            [weak = std::weak_ptr<State>(state_), id] {
                if (auto state = weak.lock()) state->remove(id);
            });
        return Connection{std::move(detach)};
    }

    void emit(Args... args) const {
        if (!state_) return;
        std::vector<Callback> snapshot;
        {
            std::lock_guard<std::mutex> lk(state_->mutex);
            snapshot.reserve(state_->receivers.size());
            for (auto& e : state_->receivers) snapshot.push_back(e.cb);
        }
        for (auto& cb : snapshot) {
            if (cb) cb(args...);
        }
    }

private:
    struct Entry {
        uint64_t id;
        Callback cb;
    };

    struct State {
        std::mutex mutex;
        uint64_t next_id = 0;
        std::vector<Entry> receivers;

        void remove(uint64_t id) {
            std::lock_guard<std::mutex> lk(mutex);
            std::erase_if(receivers, [id](auto& e) { return e.id == id; });
        }
    };

    std::shared_ptr<State> state_ = std::make_shared<State>();
};

// Pipe adaptor: hub | prism::then(f) → Connection
template <typename F>
struct Then {
    F fn;
};

template <typename F>
auto then(F&& fn) {
    return Then<std::decay_t<F>>{std::forward<F>(fn)};
}

template <typename... Args, typename F>
[[nodiscard]] Connection operator|(SenderHub<Args...>& hub, Then<F> adaptor) {
    return hub.connect(std::move(adaptor.fn));
}

} // namespace prism::core
