#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/connection.hpp>

#include <memory>
namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}


TEST_CASE("SenderHub notifies connected receiver") {
    prism::SenderHub<int> hub;
    int received = -1;
    auto conn = hub.connect([&](int v) { received = v; });
    hub.emit(42);
    CHECK(received == 42);
}

TEST_CASE("SenderHub notifies multiple receivers") {
    prism::SenderHub<int> hub;
    int a = 0, b = 0;
    auto c1 = hub.connect([&](int v) { a = v; });
    auto c2 = hub.connect([&](int v) { b = v; });
    hub.emit(7);
    CHECK(a == 7);
    CHECK(b == 7);
}

TEST_CASE("Connection disconnects on destruction") {
    prism::SenderHub<int> hub;
    int received = 0;
    {
        auto conn = hub.connect([&](int v) { received = v; });
        hub.emit(1);
        CHECK(received == 1);
    }
    hub.emit(2);
    CHECK(received == 1);
}

TEST_CASE("Connection is move-only") {
    prism::SenderHub<int> hub;
    int received = 0;
    auto c1 = hub.connect([&](int v) { received = v; });
    auto c2 = std::move(c1);
    hub.emit(5);
    CHECK(received == 5);
}

TEST_CASE("Connection disconnect is idempotent") {
    prism::SenderHub<int> hub;
    auto conn = hub.connect([](int) {});
    conn.disconnect();
    conn.disconnect();
}

TEST_CASE("SenderHub with no args") {
    prism::SenderHub<> hub;
    int calls = 0;
    auto conn = hub.connect([&] { ++calls; });
    hub.emit();
    hub.emit();
    CHECK(calls == 2);
}

TEST_CASE("pipe: hub | prism::then(f) fires on emit") {
    prism::SenderHub<int> hub;
    int received = -1;
    auto conn = hub | prism::then([&](int v) { received = v; });
    hub.emit(42);
    CHECK(received == 42);
}

TEST_CASE("pipe: Connection disconnects on destruction") {
    prism::SenderHub<int> hub;
    int received = 0;
    {
        auto conn = hub | prism::then([&](int v) { received = v; });
        hub.emit(1);
        CHECK(received == 1);
    }
    hub.emit(2);
    CHECK(received == 1);
}

TEST_CASE("pipe: multiple pipes on same hub") {
    prism::SenderHub<int> hub;
    int a = 0, b = 0;
    auto c1 = hub | prism::then([&](int v) { a = v; });
    auto c2 = hub | prism::then([&](int v) { b = v; });
    hub.emit(7);
    CHECK(a == 7);
    CHECK(b == 7);
}

TEST_CASE("Receiver can disconnect itself during emit") {
    prism::SenderHub<> hub;
    int calls = 0;
    prism::Connection conn;
    conn = hub.connect([&] {
        ++calls;
        conn.disconnect();
    });
    hub.emit();
    hub.emit();
    CHECK(calls == 1);
}

// Qt's other half: the sender may die first. The watcher's Connection then outlives its hub, and
// destroying or disconnecting it must not touch the dead hub (ASAN reports it if it does).
TEST_CASE("a Connection that outlives its hub disconnects harmlessly") {
    prism::Connection conn;
    {
        auto hub = std::make_unique<prism::SenderHub<int>>();
        conn = hub->connect([](int) {});
    }
    conn.disconnect();   // the hub is gone: nothing to remove it from
    CHECK(true);
}

TEST_CASE("a moved hub keeps its connections: they follow the hub, and disconnect from it") {
    int calls = 0;
    prism::SenderHub<int> first;
    prism::Connection conn = first.connect([&](int) { ++calls; });
    prism::SenderHub<int> moved{std::move(first)};
    moved.emit(1);
    CHECK(calls == 1);
    conn.disconnect();
    moved.emit(2);
    CHECK(calls == 1);   // disconnected from the hub it moved into
}
