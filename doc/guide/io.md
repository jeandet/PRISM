# HTTP, TCP and serial

## HTTP (opt-in)

`net/http.hpp` makes requests in the style of Python's `requests`. It is built when meson finds
cpp-httplib (`-Dhttp=auto`, never in the browser) and linked with `prism_net_dep`. A `net::Client`
sends on a thread of its own and hands each reply back on the logic thread, so the reply may set
fields:

```cpp
struct Weather {
    core::Field<ui::Label<>> status{{"..."}};
    net::Client http;

    void attach(app::AppContext& ctx) {
        http.attach(ctx);
        http.get("http://example.org/now", [this](net::Response r) {
            status.set({r.ok() ? std::string(r.text()) : r.error});
        });
    }
};
```

With `prism/app/task.hpp` the same reads as a coroutine, each `co_await` resuming on the logic
thread. `net::json<T>(response)` (`net/json.hpp`) reads a JSON body into a struct.

```cpp
app::Task refresh() {
    auto r = co_await http.get("http://example.org/now");
    status.set({std::string(r.text())});
}
```

Off the logic thread, `net::get(url)` and `net::post(url, body)` just block. The Python SDK has
no binding: Python has `requests` and `httpx`.

## Byte streams: TCP and serial (opt-in)

`io/byte_stream.hpp` is PRISM's `QIODevice`: the `ByteStream` concept (`write`, `read_some`,
`close`) that a TCP connection, a serial port and a test double share, and `LineStream` on top for
the line protocols instruments speak. `io/streams.hpp` (prism-io, standalone Asio, `-Dio=auto`)
ships `TcpStream`, `TcpListener` and `SerialPort`, blocking with timeouts, for a worker thread:

```cpp
auto port = io::SerialPort::open("/dev/ttyUSB0", {.baud = 9600});
io::LineStream lines{*port};
auto reply = lines.query("*IDN?");   // a std::expected: the line, or why not (timed_out, ...)
```

The lab bench example drives its instrument this way over TCP (`--instrument host:port`).
