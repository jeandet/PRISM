#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/render/frame_cache.hpp>

#include <string>
#include <vector>

using prism::render::FrameCache;

TEST_CASE("FrameCache returns what was inserted, and nothing for unknown keys") {
    FrameCache<int> c;
    CHECK(c.find("a") == nullptr);
    c.insert("a", 1);
    REQUIRE(c.find("a") != nullptr);
    CHECK(*c.find("a") == 1);
}

TEST_CASE("FrameCache evicts entries left unused for more than max_idle frames") {
    FrameCache<int> c;
    std::vector<int> evicted;
    auto on_evict = [&](int v) { evicted.push_back(v); };
    c.insert("kept", 1);
    c.insert("dropped", 2);
    for (int frame = 0; frame < 3; ++frame) {
        c.find("kept");                 // used every frame
        c.end_frame(2, on_evict);
    }
    CHECK(c.find("dropped") == nullptr);
    CHECK(c.find("kept") != nullptr);
    CHECK(evicted == std::vector<int>{2});
}

TEST_CASE("FrameCache keeps an entry used within the idle window") {
    FrameCache<int> c;
    int evictions = 0;
    c.insert("a", 1);
    c.end_frame(2, [&](int) { ++evictions; });
    c.end_frame(2, [&](int) { ++evictions; });
    CHECK(evictions == 0);
    CHECK(c.find("a") != nullptr);
}

TEST_CASE("FrameCache::clear evicts everything") {
    FrameCache<int> c;
    int evictions = 0;
    c.insert("a", 1);
    c.insert("b", 2);
    c.clear([&](int) { ++evictions; });
    CHECK(evictions == 2);
    CHECK(c.size() == 0);
}
