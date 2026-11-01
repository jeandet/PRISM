#pragma once

#include <prism/app/window.hpp>
#include <prism/app/widget_tree.hpp>
#include <prism/ui/window_chrome.hpp>

#include <functional>
#include <memory>
#include <unordered_map>

namespace prism::app {
using namespace prism::ui;

class WindowRegistry {
public:
    struct Entry {
        Window* window = nullptr;
        std::unique_ptr<WidgetTree> tree;
        std::shared_ptr<const SceneSnapshot> current_snap;
        int width = 0;
        int height = 0;
        uint64_t version = 0;
    };

    template <typename Model>
    WindowId add(Window& window, Model& model) {
        WindowId id = window.id();
        Entry e;
        e.window = &window;
        e.tree = std::make_unique<WidgetTree>(model);
        if (waker_) e.tree->bind_waker(waker_);
        e.tree->set_text_measurer(measurer_);
        e.tree->set_clipboard(clipboard_);
        auto [w, h] = window.size();
        if (window.decoration_mode() == DecorationMode::Custom)
            h -= static_cast<int>(WindowChrome::title_bar_h.raw());
        e.width = w;
        e.height = h;
        entries_.emplace(id, std::move(e));
        return id;
    }

    void remove(WindowId id) { entries_.erase(id); }

    void set_clipboard(const Clipboard* c) {
        clipboard_ = c;
        for (auto& [id, entry] : entries_) entry.tree->set_clipboard(c);
    }

    void set_text_measurer(const TextMeasurer* m) {
        measurer_ = m;
        for (auto& [id, entry] : entries_) entry.tree->set_text_measurer(m);
    }

    // Binds `waker` into every tree, present and future (see WidgetTree::bind_waker).
    void set_waker(std::function<void()> waker) {
        waker_ = std::move(waker);
        for (auto& [id, entry] : entries_) entry.tree->bind_waker(waker_);
    }

    // unordered_map guarantees pointer/reference stability across insertion --
    // the dispatch loop relies on this to hold an Entry* across a re-entrant add().
    [[nodiscard]] Entry* find(WindowId id) {
        auto it = entries_.find(id);
        return it != entries_.end() ? &it->second : nullptr;
    }

    template <typename Fn>
    void for_each(Fn&& fn) {
        for (auto& [id, entry] : entries_) fn(id, entry);
    }

    template <typename Fn>
    void for_each_dirty(Fn&& fn) {
        for (auto& [id, entry] : entries_)
            if (entry.tree->any_dirty()) fn(id, entry);
    }

private:
    std::unordered_map<WindowId, Entry> entries_;
    std::function<void()> waker_;
    const TextMeasurer* measurer_ = nullptr;
    const Clipboard* clipboard_ = nullptr;
};

} // namespace prism::app
