#pragma once

#include <prism/core/connection.hpp>
#include <prism/core/read_log.hpp>
#include <prism/core/transaction.hpp>

#include <algorithm>
#include <array>
#include <concepts>
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

// A value computed from other observables, kept up to date on the logic thread.
//
//   core::Derived<int> area{[this] { return width.get() * height.get(); }};
//
// With no source list it follows what its compute reads, again at every recompute: a branch not
// taken is not followed until it is. core::untracked(fn) reads without following. With a list
// (`{compute, width, height}`) it follows exactly those; debug builds warn about a value read
// but not listed.
template <typename T>
struct Derived {
    template <typename Fn>
        requires std::invocable<Fn&>
    Derived(Fn&& compute)
        : compute_(std::forward<Fn>(compute))
        , tracking_(true)
        , value_(tracked_compute())
    {}

    template <typename Fn, typename Source, typename... Sources>
    Derived(Fn&& compute, Source& source, Sources&... sources)
        : compute_(std::forward<Fn>(compute))
        , value_(first_compute(source, sources...))
    {
        subscribe(source);
        (subscribe(sources), ...);
    }

    // The connections capture `this`: moving a Derived would leave them pointing at the
    // moved-from object.
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
    bool tracking_ = false;
    read_log_detail::Log follows_;        // when tracking_: what the last compute read, connected
    T value_;
    SenderHub<const T&> changed_;
    std::vector<Connection> connections_;
    std::vector<Connection> observers_;

    void recompute() {
        T new_value = tracking_ ? tracked_compute() : compute_();
        if (value_ == new_value) return;
        value_ = std::move(new_value);
        emit_or_defer(static_cast<void*>(&changed_), [this] { changed_.emit(value_); });
    }

    // The reads of this compute become the values followed; the ones it no longer read are
    // dropped, which disconnects them.
    T tracked_compute() {
        if (!follows_.on_change) follows_.on_change = [this] { recompute(); };
        follows_.kept = std::exchange(follows_.reads, {});
        T value = [&] {
            read_log_detail::Watch watch{&follows_};
            return compute_();
        }();
        follows_.kept.clear();
        return value;
    }

    template <typename Source>
    void subscribe(Source& source) {
        connections_.push_back(
            source.on_change().connect([this](const auto&) { recompute(); })
        );
    }

    // simplify: only the first compute is checked, so a read behind a branch it did not take is
    // missed. Upgrade path: check every recompute in debug builds, or drop the list.
    template <typename... Sources>
    T first_compute([[maybe_unused]] const Sources&... sources) {
#ifndef NDEBUG
        read_log_detail::Log log;
        T first = [&] {
            read_log_detail::Watch watch{&log};
            return compute_();
        }();
        const std::array<const void*, sizeof...(Sources)> listed{
            static_cast<const void*>(std::addressof(sources))...};
        for (const auto& read : log.reads)
            if (std::ranges::find(listed, read.address) == listed.end()) {
                derived_detail::warn_unlisted_source<T>();
                break;
            }
        return first;
#else
        return compute_();
#endif
    }
};

} // namespace prism::core
