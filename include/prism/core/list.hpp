#pragma once

#include <prism/core/connection.hpp>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <ranges>
#include <vector>

namespace prism::core {

template <typename T>
class List {
public:
    using value_type = T;
    void push_back(T item) {
        items_.push_back(std::move(item));
        inserted_.emit(items_.size() - 1, items_.back());
    }

    void erase(size_t index) {
        items_.erase(items_.begin() + static_cast<ptrdiff_t>(index));
        removed_.emit(index);
    }

    // Takes the item at `from` out and puts it back at `to` (an index in the list as it is after
    // the move). Reported as updates of the slots that changed, so views redraw them in place.
    void move(size_t from, size_t to) {
        if (from == to || from >= items_.size() || to >= items_.size()) return;
        auto first = items_.begin() + static_cast<ptrdiff_t>(std::min(from, to));
        auto last = items_.begin() + static_cast<ptrdiff_t>(std::max(from, to)) + 1;
        if (from < to) std::rotate(first, first + 1, last);
        else std::rotate(first, last - 1, last);
        for (size_t i = std::min(from, to); i <= std::max(from, to); ++i) updated_.emit(i, items_[i]);
    }

    void set(size_t index, T value) {
        items_[index] = std::move(value);
        updated_.emit(index, items_[index]);
    }

    // The list becomes `new_values`: rows past its end are removed or added, and the ones in
    // both are updated -- only those that changed, so views keep the widgets of the others.
    void replace_all(std::ranges::range auto&& new_values) {
        size_t n = std::ranges::size(new_values);
        while (items_.size() > n) erase(items_.size() - 1);
        size_t i = 0;
        for (auto&& v : new_values) {
            if (i >= items_.size()) push_back(std::forward<decltype(v)>(v));
            else if (!same(items_[i], v)) set(i, std::forward<decltype(v)>(v));
            ++i;
        }
    }

    [[nodiscard]] const T& operator[](size_t i) const { return items_[i]; }
    [[nodiscard]] size_t size() const { return items_.size(); }
    [[nodiscard]] bool empty() const { return items_.empty(); }

    auto begin() const { return items_.begin(); }
    auto end() const { return items_.end(); }

    SenderHub<size_t, const T&>& on_insert() { return inserted_; }
    SenderHub<size_t>& on_remove() { return removed_; }
    SenderHub<size_t, const T&>& on_update() { return updated_; }

private:
    static bool same(const T& a, const auto& b) {
        if constexpr (std::equality_comparable_with<T, decltype(b)>) return a == b;
        else return false;
    }

    std::vector<T> items_;
    SenderHub<size_t, const T&> inserted_;
    SenderHub<size_t> removed_;
    SenderHub<size_t, const T&> updated_;
};

} // namespace prism::core
