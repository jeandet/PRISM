#pragma once

#include <prism/core/connection.hpp>
#include <prism/core/read_log.hpp>
#include <prism/core/transaction.hpp>

#include <array>
#include <cstddef>
#include <cstdio>
#include <functional>
#include <memory>
#include <string_view>
#include <vector>

#if __cpp_impl_reflection
#include <meta>
#endif

namespace prism::core::derived_detail {

template <typename T>
constexpr std::string_view type_name() {
#if __cpp_impl_reflection
    return std::meta::display_string_of(^^T);
#else
    return {};
#endif
}

template <typename T>
void warn_unlisted_source() {
    constexpr std::string_view name = type_name<T>();
    if constexpr (name.empty())
        std::fputs("prism: a Derived reads a value it does not list as a source\n", stderr);
    else
        std::fprintf(stderr, "prism: a Derived<%.*s> reads a value it does not list as a source\n",
                     static_cast<int>(name.size()), name.data());
}

} // namespace prism::core::derived_detail

namespace prism::core {

template <typename T>
struct Derived {
    template <typename Fn, typename... Sources>
    Derived(Fn&& compute, Sources&... sources)
        : compute_(std::forward<Fn>(compute))
        , value_(first_compute(sources...))
    {
        (subscribe(sources), ...);
    }

    // subscribe() captures `this` in the source's on_change callback: moving a
    // Derived would leave that callback pointing at the moved-from object.
    Derived(Derived&&) = delete;
    Derived& operator=(Derived&&) = delete;

    const T& get() const {
        read_log_detail::note_read(this);
        return value_;
    }
    operator const T&() const {
        read_log_detail::note_read(this);
        return value_;
    }

    SenderHub<const T&>& on_change() { return changed_; }

    void observe(std::function<void(const T&)> cb) {
        observers_.push_back(changed_.connect(std::move(cb)));
    }

private:
    std::function<T()> compute_;
    T value_;
    SenderHub<const T&> changed_;
    std::vector<Connection> connections_;
    std::vector<Connection> observers_;

    void recompute() {
        T new_value = compute_();
        if (value_ == new_value) return;
        value_ = std::move(new_value);
        emit_or_defer(static_cast<void*>(&changed_), [this] { changed_.emit(value_); });
    }

    template <typename Source>
    void subscribe(Source& source) {
        connections_.push_back(
            source.on_change().connect([this](const auto&) { recompute(); })
        );
    }

    // simplify: only the first compute is checked, so a read behind a branch it did not take is
    // missed, and so is a read past read_log_detail::kMaxReads. Upgrade path: check every
    // recompute in debug builds.
    template <typename... Sources>
    T first_compute([[maybe_unused]] const Sources&... sources) {
#ifndef NDEBUG
        read_log_detail::Watch watch;
        T first = compute_();
        watch.end();
        const std::array<const void*, sizeof...(Sources)> listed{
            static_cast<const void*>(std::addressof(sources))...};
        if (watch.read_something_unlisted(listed.data(), listed.size()))
            derived_detail::warn_unlisted_source<T>();
        return first;
#else
        return compute_();
#endif
    }
};

} // namespace prism::core
