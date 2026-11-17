# HTTP, TCP and serial

PRISM wraps two well-known libraries for talking to the outside world. HTTP goes through
[cpp-httplib](https://github.com/yhirose/cpp-httplib), sockets and serial ports through
[Asio](https://think-async.com/Asio/). You never see either library: you see a few small types,
and replies arrive where your model may change.

Both are optional, and neither exists in the browser (a web page has no sockets).

| Part | Header | Meson option | Link with |
|---|---|---|---|
| HTTP | `prism/net/http.hpp` | `-Dhttp=auto` | `prism_net_dep` |
| JSON bodies | `prism/net/json.hpp` | (with HTTP) | `prism_net_dep` |
| Coroutines | `prism/app/task.hpp` | always | `prism_dep` |
| TCP and serial | `prism/io/streams.hpp` | `-Dio=auto` | `prism_io_dep` |
| The stream concept | `prism/io/byte_stream.hpp` | always | header only |

`auto` builds the part when meson finds the library or its wrap.

## HTTP

### A request from the UI

A `net::Client` sends requests on a thread of its own. Each reply comes back on the logic
thread, so the callback may set fields:

```cpp
#include <prism/prism.hpp>
#include <prism/net/http.hpp>
using namespace prism;

struct Weather {
    core::Field<ui::Label<>> status{{"Loading..."}};
    net::Client http;

    void attach(app::AppContext& ctx) {
        http.attach(ctx);   // replies now come back on the logic thread
        http.get("http://example.org/now", [this](net::Response r) {
            status = {r.ok() ? std::string(r.text()) : "Failed: " + r.error};
        });
    }
    void view(app::ViewBuilder& vb) { vb.widget(status); }
};

int main() {
    Weather app;
    return app::run(app, {.title = "Weather", .width = 400, .height = 80});
}
```

`attach` is a block's start-up hook: the app calls it once, with the app context, before the
first frame.

### The same, as a coroutine

With `prism/app/task.hpp`, a function returning `app::Task` may `co_await` a request. It resumes on
the logic thread, so it reads like straight-line code:

```cpp
#include <prism/app/task.hpp>

struct Weather {
    core::Field<ui::Label<>> status{{"Loading..."}};
    net::Client http;

    void attach(app::AppContext& ctx) {
        http.attach(ctx);
        refresh();
    }

    app::Task refresh() {
        net::Response r = co_await http.get("http://example.org/now");
        status = {r.ok() ? std::string(r.text()) : "Failed: " + r.error};
    }
    void view(app::ViewBuilder& vb) { vb.widget(status); }
};
```

A `Task` starts at once and runs until its first `co_await`. Nobody waits for it: it frees itself
when it ends. An exception it lets out goes to the app's error handler.

### JSON into a struct

`net::json<T>(response)` reads a JSON body into a struct, matching keys to member names:

```cpp
#include <prism/net/json.hpp>

struct Forecast {   // not in an anonymous namespace: the reader needs its member names
    double temperature = 0;
    std::string sky;
};

// in Weather, in place of the refresh() above:
app::Task refresh() {
    auto reply = co_await http.get("http://example.org/forecast.json");
    if (auto f = net::json<Forecast>(reply))
        status = {std::to_string(f->temperature) + " °C, " + f->sky};
    else
        status = {"Bad reply: " + f.error()};
}
```

Keys the struct does not have are skipped.

### Blocking calls

On a worker thread, where blocking is fine, two plain functions do it:

```cpp
net::Response r = net::get("http://example.org/api/items");
net::Response s = net::post("http://example.org/api/items", R"({"name": "ada"})");
```

Never call them on the logic thread: the window would wait with them.

### Reference

`net::Response`:

| Member | Meaning |
|---|---|
| `int status` | The HTTP status. 0 when no reply came at all. |
| `std::string error` | Why no reply came: no connection, a timeout, a bad URL. Empty otherwise. |
| `Headers headers` | The reply's headers, as name and value pairs. |
| `std::string body` | The reply's body. |
| `bool ok()` | No error, and a status from 200 to 299. |
| `std::string_view text()` | The body. |
| `header(name)` | One header's value, as a `std::optional<std::string_view>`. The name's case does not matter. |

`net::Request`, for `Client::send` when `get` and `post` are not enough:

| Member | Default | Meaning |
|---|---|---|
| `method` | `"GET"` | The HTTP method. |
| `url` | | `http://host[:port]/path?query`. `https://` needs a TLS build of cpp-httplib; without one the reply's `error` says so. |
| `headers` | none | Extra headers. |
| `body` | empty | The body to send. |
| `content_type` | empty | The body's type. |
| `timeout` | 10 s | How long to wait for the reply. |

`net::Client`:

| Call | What it does |
|---|---|
| `attach(ctx)` | Replies are delivered on the app's logic thread. Call it from a block's `attach`. |
| `get(url, on_reply)` | Sends a GET; `on_reply(Response)` runs when it is back. |
| `post(url, body, on_reply, content_type = "application/json")` | Sends a POST. |
| `send(request, on_reply)` | Sends any request. |
| `co_await get(url)`, `post(url, body, type)`, `send(request)` | The same, in a coroutine: the call without a callback returns something to `co_await`. |

Free functions, blocking:

| Call | What it does |
|---|---|
| `net::send(request)` | Any request; returns the `Response`. |
| `net::get(url, headers = {})` | A GET. |
| `net::post(url, body, content_type = "application/json")` | A POST. |

### Pitfalls

- A `Client` sends one request at a time, in order. Two slow requests wait for each other.
- A `Client` that was never attached calls `on_reply` on its own thread. There, setting a field
  is a bug: attach it first.
- Destroying the `Client` aborts the request in flight. Replies not yet delivered are dropped.
- A coroutine whose reply never comes (the app closed first) stays suspended. Its frame is not
  freed.
- Python has no binding for this: use `requests` or `httpx` there.

## TCP and serial ports

### Talking to an instrument

Many instruments, GPS receivers and modems speak in lines of text. Open the port, wrap it in a
`LineStream`, and ask:

```cpp
#include <prism/io/streams.hpp>
#include <print>
using namespace prism;

int main() {
    auto port = io::SerialPort::open("/dev/ttyUSB0", {.baud = 9600});
    if (!port) {
        std::println("cannot open: {}", port.error().message());
        return 1;
    }
    io::LineStream lines{*port};
    auto id = lines.query("*IDN?");   // sends a line, reads one back
    if (id) std::println("instrument: {}", *id);
    else std::println("no answer: {}", id.error().message());
}
```

Everything here blocks, and every read gives up after a timeout. Run it on a worker thread, and
hand the results to the model through a `core::Shared` or a `core::Channel`
([Threads](../concepts/threads.md)). The lab bench example does exactly this over TCP
(`--instrument host:port`).

### A byte stream

`io::ByteStream` is a concept: anything with these three calls is a stream. TCP connections,
serial ports and test doubles all are, so code written against the concept works with any of
them.

| Call | What it does |
|---|---|
| `write(std::span<const std::byte>)` | Writes all of it. Returns `std::expected<void, std::error_code>`. |
| `read_some(std::span<std::byte>)` | Reads what has arrived. Returns how many bytes, 0 when the other side closed, or an error. |
| `close()` | Closes the stream. |

A read waits at most the stream's timeout, then fails with `std::errc::timed_out`. A silent peer
never hangs the thread for good. `io::as_bytes(text)` turns a string into bytes for `write`.

### LineStream

`io::LineStream{stream}` reads and writes lines over any byte stream. A line ends with `\n`; a
`\r` before it is dropped.

| Call | What it does |
|---|---|
| `write_line(text)` | Writes `text` and a `\n`. |
| `read_line()` | The next line, without its end. Keeps what arrived past it for the next call. |
| `query(command)` | `write_line(command)`, then `read_line()`: one command, one reply. |

Each returns a `std::expected`: the value, or a `std::error_code`.

### TCP

```cpp
auto conn = io::TcpStream::connect("192.168.1.20", 5025, {.connect_timeout = std::chrono::seconds(2)});
if (conn) {
    io::LineStream scpi{*conn};
    auto volts = scpi.query("MEAS:VOLT?");
}
```

| Call | What it does |
|---|---|
| `TcpStream::connect(host, port, options = {})` | Connects, or returns why not. |
| `TcpStream::Options::connect_timeout` | How long to try. Default 5 s. |
| `TcpStream::Options::read_timeout` | How long a read waits. Default 2 s. |
| `TcpListener::bind(host, port)` | Listens on a port. Port 0 picks a free one. |
| `listener.port()` | The port it listens on. |
| `listener.accept(timeout, read_timeout = 2s)` | Waits up to `timeout` for the next connection, and returns it as a `TcpStream`. |
| `listener.close()` | Stops listening. |

### Serial ports

| Call | What it does |
|---|---|
| `SerialPort::open(device, options = {})` | Opens `"/dev/ttyUSB0"`, `"COM3"`... |
| `SerialPort::Options::baud` | The speed. Default 115200. Always 8 data bits, no parity, one stop bit, no flow control. |
| `SerialPort::Options::read_timeout` | How long a read waits. Default 2 s. |

### Pitfalls

- These calls block. On the logic thread they freeze the window. Use a worker.
- A read that times out is an error, not an empty answer. Check the `std::expected`.
- Streams can be moved, not copied.
