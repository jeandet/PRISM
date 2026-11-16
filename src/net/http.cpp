#include <prism/net/http.hpp>

#include <prism/app/model_app.hpp>

#include <httplib.h>

#include <algorithm>
#include <cctype>

namespace prism::net {

namespace {

bool same_ignoring_case(std::string_view a, std::string_view b) {
    return std::ranges::equal(a, b, [](unsigned char x, unsigned char y) { return std::tolower(x) == std::tolower(y); });
}

// "http://host:8080/a/b?c" -> {"http://host:8080", "/a/b?c"}; nullopt when it has no scheme.
struct UrlParts {
    std::string origin;
    std::string target;
};
std::optional<UrlParts> split_url(std::string_view url) {
    const auto scheme_end = url.find("://");
    if (scheme_end == std::string_view::npos || scheme_end == 0) return std::nullopt;
    const auto path_begin = url.find('/', scheme_end + 3);
    if (path_begin == std::string_view::npos) return UrlParts{std::string(url), "/"};
    return UrlParts{std::string(url.substr(0, path_begin)), std::string(url.substr(path_begin))};
}

httplib::Headers to_httplib(const Headers& headers) {
    httplib::Headers out;
    for (const auto& [name, value] : headers) out.emplace(name, value);
    return out;
}

Response from_httplib(const httplib::Result& result) {
    if (!result) return {.error = httplib::to_string(result.error())};
    Response r{.status = result->status, .body = result->body};
    for (const auto& [name, value] : result->headers) r.headers.emplace_back(name, value);
    return r;
}

httplib::Result perform(httplib::Client& client, const Request& rq, const std::string& target) {
    const auto headers = to_httplib(rq.headers);
    if (rq.method == "GET") return client.Get(target, headers);
    if (rq.method == "DELETE") return client.Delete(target, headers, rq.body, rq.content_type);
    if (rq.method == "POST") return client.Post(target, headers, rq.body, rq.content_type);
    if (rq.method == "PUT") return client.Put(target, headers, rq.body, rq.content_type);
    if (rq.method == "PATCH") return client.Patch(target, headers, rq.body, rq.content_type);
    return httplib::Result{nullptr, httplib::Error::Unknown};
}

} // namespace

std::optional<std::string_view> Response::header(std::string_view name) const {
    for (const auto& [key, value] : headers)
        if (same_ignoring_case(key, name)) return value;
    return std::nullopt;
}

Response send(const Request& request) {
    const auto parts = split_url(request.url);
    if (!parts) return {.error = "not a URL: " + request.url};
    httplib::Client client{parts->origin};
    if (!client.is_valid()) return {.error = "unsupported URL (https needs a TLS build): " + request.url};
    client.set_connection_timeout(request.timeout);
    client.set_read_timeout(request.timeout);
    client.set_write_timeout(request.timeout);
    client.set_follow_location(true);
    Response r = from_httplib(perform(client, request, parts->target));
    if (r.status == 0 && r.error.empty()) r.error = "unsupported method: " + request.method;
    return r;
}

Client::~Client() {
    worker_ = std::jthread{};   // stop, then join: the request in flight finishes first
}

void Client::attach(app::AppContext& ctx) { deliver_ = ctx.poster(); }

void Client::send(Request request, std::function<void(Response)> on_reply) {
    std::lock_guard lock{mutex_};
    jobs_.push_back({std::move(request), std::move(on_reply)});
    if (!worker_.joinable()) start_worker();
    wake_.notify_one();
}

void Client::start_worker() {
    worker_ = std::jthread([this](std::stop_token stop) { run(stop); });
}

void Client::run(const std::stop_token& stop) {
    while (true) {
        Job job;
        {
            std::unique_lock lock{mutex_};
            if (!wake_.wait(lock, stop, [this] { return !jobs_.empty(); })) return;
            job = std::move(jobs_.front());
            jobs_.pop_front();
        }
        auto deliver_reply = [reply = net::send(job.request), on_reply = std::move(job.on_reply)]() mutable {
            on_reply(std::move(reply));
        };
        if (stop.stop_requested()) return;
        if (deliver_) deliver_(std::move(deliver_reply));
        else deliver_reply();   // no app to hand it to: on this thread
    }
}

} // namespace prism::net
