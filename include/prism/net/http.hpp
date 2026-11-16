#pragma once

#include <chrono>
#include <condition_variable>
#include <coroutine>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

// HTTP in the style of Python's requests: one call per request, a Response with the status, the
// headers and the body. Three ways to call it:
//
//   auto r = net::get("http://host/api");                  // blocking, on a worker thread
//   http.get(url, [this](net::Response r) { ... });        // net::Client: the reply on the logic thread
//   auto r = co_await http.get(url);                       // the same, in a coroutine (app/task.hpp)
//
// No HTTP library leaks from here: the implementation (cpp-httplib) is src/net/http.cpp, in the
// prism-net library, built when meson's `http` option finds it (not in the browser: no sockets).
namespace prism::app { class AppContext; }

namespace prism::net {

using Headers = std::vector<std::pair<std::string, std::string>>;

struct Request {
    std::string method = "GET";
    std::string url;   // http://host[:port]/path?query, or https:// when built with TLS
    Headers headers{};
    std::string body{};
    std::string content_type{};
    std::chrono::milliseconds timeout{10'000};
};

struct Response {
    int status = 0;        // 0: no reply at all; `error` says why
    Headers headers{};
    std::string body{};
    std::string error{};   // a transport failure: no connection, timeout, bad URL

    [[nodiscard]] bool ok() const { return error.empty() && status >= 200 && status < 300; }
    [[nodiscard]] std::string_view text() const { return body; }
    [[nodiscard]] std::optional<std::string_view> header(std::string_view name) const;
};

// Blocking: the calling thread waits for the reply. Never on the logic thread -- use a Client.
Response send(const Request& request);
inline Response get(std::string url, Headers headers = {}) {
    return send({.url = std::move(url), .headers = std::move(headers)});
}
inline Response post(std::string url, std::string body, std::string content_type = "application/json") {
    return send({.method = "POST", .url = std::move(url), .body = std::move(body), .content_type = std::move(content_type)});
}

// Sends requests on a thread of its own and hands each reply back where the model may change:
// through `deliver`, which an app gets from attach(ctx) (the logic thread, with a publish after).
// simplify: one thread, requests in order; add a pool when an app needs parallel requests.
class Client {
public:
    using Deliver = std::function<void(std::function<void()>)>;

    Client() = default;
    explicit Client(Deliver deliver) : deliver_(std::move(deliver)) {}
    ~Client();   // waits for the request in flight; replies not yet delivered are dropped

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    // Replies are delivered on ctx's logic thread. Call from a block's attach().
    void attach(app::AppContext& ctx);

    void send(Request request, std::function<void(Response)> on_reply);
    void get(std::string url, std::function<void(Response)> on_reply) {
        send({.url = std::move(url)}, std::move(on_reply));
    }
    void post(std::string url, std::string body, std::function<void(Response)> on_reply,
              std::string content_type = "application/json") {
        send({.method = "POST", .url = std::move(url), .body = std::move(body), .content_type = std::move(content_type)},
             std::move(on_reply));
    }

    // co_await-able: resumes the coroutine where a callback would run, with the Response.
    // simplify: a reply never delivered (the app closed first) leaves the coroutine suspended
    // and its frame unfreed; add cancellation when coroutines outlive windows in real apps.
    struct Reply {
        Client* client;
        Request request;
        Response response{};
        bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<> h) {
            client->send(std::move(request), [this, h](Response r) {
                response = std::move(r);
                h.resume();
            });
        }
        Response await_resume() { return std::move(response); }
    };
    [[nodiscard]] Reply send(Request request) { return {this, std::move(request)}; }
    [[nodiscard]] Reply get(std::string url) { return send(Request{.url = std::move(url)}); }
    [[nodiscard]] Reply post(std::string url, std::string body, std::string content_type = "application/json") {
        return send(Request{.method = "POST", .url = std::move(url), .body = std::move(body),
                            .content_type = std::move(content_type)});
    }

private:
    struct Job {
        Request request;
        std::function<void(Response)> on_reply;
    };
    void start_worker();
    void run(const std::stop_token& stop);

    Deliver deliver_;
    std::mutex mutex_;
    std::condition_variable_any wake_;
    std::deque<Job> jobs_;
    std::jthread worker_;   // last: stopped and joined before the queue it reads is destroyed
};

} // namespace prism::net
