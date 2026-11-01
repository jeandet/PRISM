#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace prism::render {

// Per-frame resource cache for renderers (rasterized text, later images): an entry survives as
// long as some frame within the last `max_idle` frames used it. Backend-neutral and owned by one
// render thread, so it is not synchronized. V is a cheap handle (texture pointer + size);
// releasing it is the caller's job, via the callback given to end_frame()/clear().
// Call end_frame() after the frame is drawn, and keep max_idle >= 1: a resource may still be
// referenced by the GPU work of the frame that last used it until that frame is presented.
// Pointers from find()/insert() stay valid until the next end_frame() or clear().
template <typename V>
class FrameCache {
    struct Entry {
        V value;
        std::uint64_t last_used;
    };
    std::unordered_map<std::string, Entry> entries_;
    std::uint64_t frame_ = 0;

public:
    V* find(const std::string& key) {
        auto it = entries_.find(key);
        if (it == entries_.end()) return nullptr;
        it->second.last_used = frame_;
        return &it->second.value;
    }

    // The key must be new (find() first): replacing would drop a value without releasing it.
    V& insert(std::string key, V value) {
        auto [it, inserted] = entries_.try_emplace(std::move(key), Entry{std::move(value), frame_});
        assert(inserted && "FrameCache::insert on an existing key leaks the old value");
        return it->second.value;
    }

    template <typename OnEvict>
    void end_frame(std::uint64_t max_idle, OnEvict&& on_evict) {
        assert(max_idle >= 1 && "evicting what this frame drew would free it before present");
        std::erase_if(entries_, [&](auto& kv) {
            if (frame_ - kv.second.last_used < max_idle) return false;
            on_evict(kv.second.value);
            return true;
        });
        ++frame_;
    }

    template <typename OnEvict>
    void clear(OnEvict&& on_evict) {
        for (auto& [key, e] : entries_) on_evict(e.value);
        entries_.clear();
    }

    std::size_t size() const { return entries_.size(); }
};

} // namespace prism::render
