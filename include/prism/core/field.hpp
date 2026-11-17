#pragma once

#include <prism/core/connection.hpp>
#include <prism/core/logic_thread.hpp>
#include <prism/core/read_log.hpp>
#include <prism/core/value.hpp>
#include <prism/core/transaction.hpp>

#include <cassert>
#include <concepts>
#include <functional>
#include <vector>

namespace prism::core {

template <typename T, typename Derived>
struct ObservableValue {
    T value{};

    ObservableValue() = default;
    ObservableValue(T init) : value(std::move(init)) {}

    const T& get() const {
        read_log_detail::note_read(static_cast<const Derived*>(this));
        return value;
    }
    operator const T&() const {
        read_log_detail::note_read(static_cast<const Derived*>(this));
        return value;
    }

    void set(T new_value) {
        assert(may_change_model() && "a model field set off the app's logic thread: hand it over "
                                     "with AppContext::post, Shared<T> or Channel<T>");
        if (value == new_value) return;
        value = std::move(new_value);
        emit_or_defer(static_cast<void*>(&changed_), [this] { changed_.emit(value); });
    }

    // Changes some members in place, one notification for all of them (none if nothing changed):
    //   progress.update([](auto& p) { p.value = 0.5; p.busy = false; });
    // set({...}) builds a new value instead: every member it does not name is reset.
    template <typename F>
        requires std::invocable<F&, T&>
    void update(F&& change) {
        T next = value;
        std::forward<F>(change)(next);
        set(std::move(next));
    }

    // Sugar over set(): `price = 9.99`, `count += 1`, `++count`, `name += "!"`. Each is one set(),
    // so an equal result does not notify and a transaction defers the notification.
    Derived& operator=(T new_value) { set(std::move(new_value)); return self(); }

    template <typename U> requires requires(T& t, U&& u) { t += std::forward<U>(u); }
    Derived& operator+=(U&& u) { return apply([&](T& v) { v += std::forward<U>(u); }); }
    template <typename U> requires requires(T& t, U&& u) { t -= std::forward<U>(u); }
    Derived& operator-=(U&& u) { return apply([&](T& v) { v -= std::forward<U>(u); }); }
    template <typename U> requires requires(T& t, U&& u) { t *= std::forward<U>(u); }
    Derived& operator*=(U&& u) { return apply([&](T& v) { v *= std::forward<U>(u); }); }
    template <typename U> requires requires(T& t, U&& u) { t /= std::forward<U>(u); }
    Derived& operator/=(U&& u) { return apply([&](T& v) { v /= std::forward<U>(u); }); }
    template <typename U> requires requires(T& t, U&& u) { t %= std::forward<U>(u); }
    Derived& operator%=(U&& u) { return apply([&](T& v) { v %= std::forward<U>(u); }); }
    template <typename U> requires requires(T& t, U&& u) { t |= std::forward<U>(u); }
    Derived& operator|=(U&& u) { return apply([&](T& v) { v |= std::forward<U>(u); }); }
    template <typename U> requires requires(T& t, U&& u) { t &= std::forward<U>(u); }
    Derived& operator&=(U&& u) { return apply([&](T& v) { v &= std::forward<U>(u); }); }
    template <typename U> requires requires(T& t, U&& u) { t ^= std::forward<U>(u); }
    Derived& operator^=(U&& u) { return apply([&](T& v) { v ^= std::forward<U>(u); }); }

    Derived& operator++() requires requires(T& t) { ++t; } { return apply([](T& v) { ++v; }); }
    Derived& operator--() requires requires(T& t) { --t; } { return apply([](T& v) { --v; }); }
    T operator++(int) requires requires(T& t) { ++t; } { T old = value; ++*this; return old; }
    T operator--(int) requires requires(T& t) { --t; } { T old = value; --*this; return old; }

    // `name == "ada"` and `name < "b"`: the conversion to const T& alone cannot reach a
    // templated operator such as std::string's. The other side is converted to T first; a
    // constraint naming `==` on it would find these friends again through its template
    // arguments (an iterator over fields) and never end.
    template <typename U> requires std::convertible_to<const U&, T> && std::equality_comparable<T>
    friend bool operator==(const ObservableValue& a, const U& b) { return a.get() == T(b); }
    template <typename U> requires std::convertible_to<const U&, T> && std::three_way_comparable<T>
    friend auto operator<=>(const ObservableValue& a, const U& b) { return a.get() <=> T(b); }

    void observe(std::function<void(const T&)> cb) {
        observers_.push_back(changed_.connect(std::move(cb)));
    }

    SenderHub<const T&>& on_change() { return changed_; }

private:
    Derived& self() { return static_cast<Derived&>(*this); }
    template <typename F>
    Derived& apply(F&& change) {
        update(std::forward<F>(change));
        return self();
    }

    SenderHub<const T&> changed_;
    std::vector<Connection> observers_;
};

template <typename T>
struct Field : ObservableValue<T, Field<T>> {
    static constexpr bool is_prism_field = true;
    using ObservableValue<T, Field<T>>::ObservableValue;
    using ObservableValue<T, Field<T>>::operator=;
};

} // namespace prism::core
