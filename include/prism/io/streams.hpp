#pragma once

#include <prism/io/byte_stream.hpp>

#include <chrono>
#include <cstdint>
#include <memory>

// The ByteStreams PRISM ships: a TCP connection, a TCP listener that hands out connections, and a
// serial port. Blocking, with timeouts, for a worker thread. Asio stays out of this header: the
// code is src/io/streams.cpp in the prism-io library (meson's `io` option; not in the browser).
namespace prism::io {

using std::chrono::milliseconds;

class TcpStream {
public:
    struct Options {
        milliseconds connect_timeout{5000};
        milliseconds read_timeout{2000};
    };
    static std::expected<TcpStream, std::error_code> connect(std::string_view host, std::uint16_t port,
                                                             Options options);
    static std::expected<TcpStream, std::error_code> connect(std::string_view host, std::uint16_t port) {
        return connect(host, port, Options{});
    }

    TcpStream(TcpStream&&) noexcept;
    TcpStream& operator=(TcpStream&&) noexcept;
    ~TcpStream();

    std::expected<void, std::error_code> write(std::span<const std::byte> bytes);
    std::expected<std::size_t, std::error_code> read_some(std::span<std::byte> into);
    void close();

    struct Impl;
    explicit TcpStream(std::unique_ptr<Impl> impl);

private:
    std::unique_ptr<Impl> impl_;
};

// Accepts connections on a port; port 0 picks a free one (port() says which).
class TcpListener {
public:
    static std::expected<TcpListener, std::error_code> bind(std::string_view host, std::uint16_t port);

    TcpListener(TcpListener&&) noexcept;
    TcpListener& operator=(TcpListener&&) noexcept;
    ~TcpListener();

    [[nodiscard]] std::uint16_t port() const;
    // Waits up to `timeout` for the next connection; its reads time out after `read_timeout`.
    std::expected<TcpStream, std::error_code> accept(milliseconds timeout, milliseconds read_timeout = milliseconds{2000});
    void close();

    struct Impl;

private:
    explicit TcpListener(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

class SerialPort {
public:
    struct Options {
        unsigned baud = 115200;   // 8 data bits, no parity, one stop bit, no flow control
        milliseconds read_timeout{2000};
    };
    // "/dev/ttyUSB0", "COM3"
    static std::expected<SerialPort, std::error_code> open(std::string_view device, Options options);
    static std::expected<SerialPort, std::error_code> open(std::string_view device) { return open(device, Options{}); }

    SerialPort(SerialPort&&) noexcept;
    SerialPort& operator=(SerialPort&&) noexcept;
    ~SerialPort();

    std::expected<void, std::error_code> write(std::span<const std::byte> bytes);
    std::expected<std::size_t, std::error_code> read_some(std::span<std::byte> into);
    void close();

    struct Impl;

private:
    explicit SerialPort(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

static_assert(ByteStream<TcpStream>);
static_assert(ByteStream<SerialPort>);

} // namespace prism::io
