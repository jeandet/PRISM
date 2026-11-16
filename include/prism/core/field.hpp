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

    void observe(std::function<void(const T&)> cb) {
        observers_.push_back(changed_.connect(std::move(cb)));
    }

    SenderHub<const T&>& on_change() { return changed_; }

private:
    SenderHub<const T&> changed_;
    std::vector<Connection> observers_;
};

template <typename T>
struct Field : ObservableValue<T, Field<T>> {
    static constexpr bool is_prism_field = true;
    using ObservableValue<T, Field<T>>::ObservableValue;
};

} // namespace prism::core
