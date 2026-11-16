#pragma once

#include <prism/core/connection.hpp>

#include <functional>
#include <utility>
#include <vector>

// What a computation read. Every observable's get() calls note_read(); a Derived with no source
// list records its compute's reads here and follows them (core/derived.hpp). With a list, debug
// builds use the same record to warn about a value read but not listed.
namespace prism::core::read_log_detail {

// One value read, and the connection that follows it (empty when the log only records).
struct Read {
    const void* address;
    Connection connection;
};

// A compute's reads. With on_change set, each new value read is connected to it at the moment of
// the read, while it is surely alive (one built and dropped inside the compute dies before the
// compute returns; its connection then goes quiet). A value followed last time hands its
// connection over from `kept` instead of connecting again.
struct Log {
    std::vector<Read> reads;
    std::vector<Read> kept;
    std::function<void()> on_change;
};

// Defined once, in libprism (src/core/shared_state.cpp): an inline variable gets one copy per
// shared object that hides its symbols (a Python extension does), and the copies disagree. A
// function, not an extern thread_local: a Windows DLL cannot export a thread_local.
Log*& watching();

inline Read* find_read(std::vector<Read>& reads, const void* address) {
    for (auto& read : reads)
        if (read.address == address) return &read;
    return nullptr;
}

template <typename Source>
void note_read(const Source* source) {
    Log* log = watching();
    if (log == nullptr || find_read(log->reads, source) != nullptr) return;
    if (Read* old = find_read(log->kept, source)) {
        log->reads.push_back(std::move(*old));
        return;
    }
    Connection connection;
    if (log->on_change)
        connection = const_cast<Source*>(source)->on_change().connect(
            [fn = log->on_change](const auto&) { fn(); });
    log->reads.push_back({source, std::move(connection)});
}

// Records into `log` (nullptr: records nothing) while it lives, then puts the previous log back:
// a Derived built inside another one's compute must not be credited with the outer one's reads.
class Watch {
public:
    explicit Watch(Log* log) : previous_(std::exchange(watching(), log)) {}
    ~Watch() { watching() = previous_; }

    Watch(const Watch&) = delete;
    Watch& operator=(const Watch&) = delete;

private:
    Log* previous_;
};

} // namespace prism::core::read_log_detail

namespace prism::core {

// Runs fn() without recording its reads: a Derived that peeks at a value it must not follow.
template <typename F>
decltype(auto) untracked(F&& fn) {
    read_log_detail::Watch none{nullptr};
    return std::forward<F>(fn)();
}

} // namespace prism::core
