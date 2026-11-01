#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/backends/queued_clipboard.hpp>

#include <atomic>
#include <string>
#include <thread>
#include <vector>

TEST_CASE("texts set from any thread are applied in order on drain, with one wake each") {
    std::atomic<int> wakes{0};
    prism::backends::QueuedClipboard cb{[&] { ++wakes; }};
    std::thread([&] { cb.set_text("a"); cb.set_text("b"); }).join();
    CHECK(wakes == 2);
    std::vector<std::string> applied;
    cb.drain([&](const std::string& s) { applied.push_back(s); });
    CHECK(applied == std::vector<std::string>{"a", "b"});
    cb.drain([&](const std::string& s) { applied.push_back(s); });
    CHECK(applied.size() == 2);   // drained once
}
