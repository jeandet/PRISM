#pragma once

#include <cstddef>
#include <utility>

// Debug-only guard for Derived<T>'s source list (core/derived.hpp). Every observable get() calls
// note_read(); a Derived arms a Watch around its first compute, then asks whether what it read was
// all listed as sources. Leaving a source out is otherwise silent: the value just goes stale.
// A release build has no log and no thread_local -- note_read() compiles to nothing.
namespace prism::core::read_log_detail {

#ifndef NDEBUG

// Fixed size: the log sits inside the Derived being built, and a debug check that allocated would
// move the timing of what it is measuring.
inline constexpr std::size_t kMaxReads = 64;

struct Log {
    const void* reads[kMaxReads]{};
    std::size_t count = 0;
};

// Defined once, in libprism (src/core/shared_state.cpp): an inline variable gets one copy per
// shared object that hides its symbols (a Python extension does), and the copies disagree. A
// function, not an extern thread_local: a Windows DLL cannot export a thread_local.
Log*& watching();

inline void note_read(const void* address) {
    Log* log = watching();
    if (log == nullptr || log->count == kMaxReads) return;
    log->reads[log->count++] = address;
}

// Watches the reads done during its lifetime (a local: it never outlives the log it replaced), then puts the previous log back: a Derived built
// inside another one's compute must not be credited with the outer one's reads.
class Watch {
public:
    Watch() : previous_(std::exchange(watching(), &log_)) {}
    ~Watch() { watching() = previous_; }

    Watch(const Watch&) = delete;
    Watch& operator=(const Watch&) = delete;

    // The first compute is over: what this object reads from now on is not part of it.
    void end() noexcept { watching() = previous_; }

    [[nodiscard]] bool read_something_unlisted(const void* const* listed, std::size_t count) const {
        for (std::size_t i = 0; i < log_.count; ++i)
            if (not_listed(log_.reads[i], listed, count)) return true;
        return false;
    }

private:
    static bool not_listed(const void* read, const void* const* listed, std::size_t count) {
        for (std::size_t i = 0; i < count; ++i)
            if (listed[i] == read) return false;
        return true;
    }

    Log log_{};
    Log* previous_ = nullptr;
};

#else

inline void note_read([[maybe_unused]] const void* address) {}

#endif

} // namespace prism::core::read_log_detail
