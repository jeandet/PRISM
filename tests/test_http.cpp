// net::send / net::Client (include/prism/net/http.hpp) against an in-process server on localhost:
// no network needed. The server is cpp-httplib's, the library prism-net wraps.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/task.hpp>
#include <prism/net/http.hpp>
#include <prism/net/json.hpp>

#include <httplib.h>

#include <deque>
#include <functional>
#include <string>
#include <thread>

using namespace prism;
using namespace std::chrono_literals;

namespace {
struct TestServer {
    httplib::Server server;
    int port = 0;
    std::jthread thread;

    TestServer() {
        server.Get("/hello", [](const httplib::Request&, httplib::Response& res) {
            res.set_header("X-Greeting", "hi");
            res.set_content("hello", "text/plain");
        });
        server.Get("/point", [](const httplib::Request&, httplib::Response& res) {
            res.set_content(R"({"x": 1.5, "y": -2, "extra": true})", "application/json");
        });
        server.Post("/echo", [](const httplib::Request& req, httplib::Response& res) {
            res.set_content(req.body, req.get_header_value("Content-Type"));
        });
        port = server.bind_to_any_port("127.0.0.1");
        thread = std::jthread([this] { server.listen_after_bind(); });
        server.wait_until_ready();
    }
    ~TestServer() { server.stop(); }

    std::string url(std::string_view path) const { return "http://127.0.0.1:" + std::to_string(port) + std::string(path); }
};

// Stands in for an app's logic thread: what the client delivers queues here, and the test thread
// runs it, so callbacks and coroutines resume on the test thread.
struct FakeLogicThread {
    std::mutex mutex;
    std::deque<std::function<void()>> queue;

    net::Client::Deliver deliver() {
        return [this](std::function<void()> fn) {
            std::lock_guard lock{mutex};
            queue.push_back(std::move(fn));
        };
    }
    // Runs what was delivered until `done` holds, or gives up after 5 s.
    void run_until(const std::function<bool()>& done) {
        for (auto waited = 0ms; !done() && waited < 5000ms; waited += 2ms) {
            std::deque<std::function<void()>> batch;
            {
                std::lock_guard lock{mutex};
                batch.swap(queue);
            }
            for (auto& fn : batch) fn();
            std::this_thread::sleep_for(2ms);
        }
    }
};
}

// Not in the anonymous namespace: net::json<T> needs a T with linkage (clang, see net/json.hpp).
struct Point {
    double x = 0;
    double y = 0;
};

TEST_CASE("a blocking get returns the status, the body and the headers") {
    TestServer server;
    auto r = net::get(server.url("/hello"));
    CHECK(r.ok());
    CHECK(r.status == 200);
    CHECK(r.text() == "hello");
    CHECK(r.header("x-greeting") == "hi");   // header names ignore case
    CHECK_FALSE(r.header("missing"));
}

TEST_CASE("a missing page is a status, not an error; no server is an error") {
    TestServer server;
    auto missing = net::get(server.url("/nope"));
    CHECK(missing.status == 404);
    CHECK(missing.error.empty());
    CHECK_FALSE(missing.ok());

    auto refused = net::get("http://127.0.0.1:1/");
    CHECK(refused.status == 0);
    CHECK_FALSE(refused.error.empty());
    CHECK_FALSE(refused.ok());

    CHECK_FALSE(net::get("not a url").error.empty());
}

TEST_CASE("a post sends its body and content type") {
    TestServer server;
    auto r = net::post(server.url("/echo"), R"({"a":1})");
    CHECK(r.ok());
    CHECK(r.text() == R"({"a":1})");
    CHECK(r.header("Content-Type") == "application/json");
}

TEST_CASE("json<T> reads the body into a struct, skipping unknown keys") {
    TestServer server;
    auto point = net::json<Point>(net::get(server.url("/point")));
    REQUIRE(point.has_value());
    CHECK(point->x == 1.5);
    CHECK(point->y == -2);
    CHECK_FALSE(net::json<Point>(net::get(server.url("/hello"))).has_value());
}

TEST_CASE("a Client's reply comes back through its deliver function, on the thread that runs it") {
    TestServer server;
    FakeLogicThread logic;
    net::Client http{logic.deliver()};
    std::optional<net::Response> got;
    std::thread::id ran_on;
    http.get(server.url("/hello"), [&](net::Response r) {
        got = std::move(r);
        ran_on = std::this_thread::get_id();
    });
    logic.run_until([&] { return got.has_value(); });
    REQUIRE(got);
    CHECK(got->text() == "hello");
    CHECK(ran_on == std::this_thread::get_id());
}

namespace {
app::Task fetch_both(net::Client& http, const TestServer& server, std::string& out, std::thread::id& resumed_on) {
    auto first = co_await http.get(server.url("/hello"));
    auto second = co_await http.post(server.url("/echo"), first.body + " again", "text/plain");
    out = second.body;
    resumed_on = std::this_thread::get_id();
}
}

TEST_CASE("a coroutine awaits Client replies in order and resumes where they are delivered") {
    TestServer server;
    FakeLogicThread logic;
    net::Client http{logic.deliver()};
    std::string out;
    std::thread::id resumed_on;
    fetch_both(http, server, out, resumed_on);
    CHECK(out.empty());   // suspended at the first co_await: nothing delivered yet
    logic.run_until([&] { return !out.empty(); });
    CHECK(out == "hello again");
    CHECK(resumed_on == std::this_thread::get_id());
}
