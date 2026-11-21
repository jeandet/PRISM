#include <prism/io/streams.hpp>

#include <asio.hpp>

#include <string>

#if defined(__APPLE__)
#include <IOKit/serial/ioss.h>
#include <sys/ioctl.h>
#include <cerrno>
#endif

// Each stream owns its io_context and runs it only inside its own calls: a blocking API, with a
// timeout made the way Asio's blocking_tcp_client example does -- start the operation, run the
// context for at most the timeout, and cancel what is still pending.
namespace prism::io {

namespace {

// Runs `start(handler)` for at most `timeout`; the handler's error, or timed_out.
template <typename Start, typename Cancel>
std::error_code run_for(asio::io_context& io, milliseconds timeout, Start&& start, Cancel&& cancel,
                        std::size_t* transferred = nullptr) {
    std::error_code result = asio::error::would_block;
    io.restart();
    start([&](std::error_code ec, std::size_t n = 0) {
        result = ec;
        if (transferred) *transferred = n;
    });
    io.run_for(timeout);
    if (!io.stopped()) {
        cancel();
        io.run();   // the cancelled handler runs, with operation_aborted
        return std::make_error_code(std::errc::timed_out);
    }
    return result;
}

std::unexpected<std::error_code> failure(std::error_code ec) { return std::unexpected(ec); }

} // namespace

// --- TcpStream -----------------------------------------------------------------------------------

struct TcpStream::Impl {
    asio::io_context io;
    asio::ip::tcp::socket socket{io};
    milliseconds read_timeout;
};

TcpStream::TcpStream(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
TcpStream::TcpStream(TcpStream&&) noexcept = default;
TcpStream& TcpStream::operator=(TcpStream&&) noexcept = default;
TcpStream::~TcpStream() { close(); }

std::expected<TcpStream, std::error_code> TcpStream::connect(std::string_view host, std::uint16_t port, Options options) {
    auto impl = std::make_unique<Impl>();
    impl->read_timeout = options.read_timeout;
    std::error_code ec;
    asio::ip::tcp::resolver resolver{impl->io};
    const auto endpoints = resolver.resolve(std::string(host), std::to_string(port), ec);
    if (ec) return failure(ec);
    ec = run_for(impl->io, options.connect_timeout,
                 [&](auto done) { asio::async_connect(impl->socket, endpoints, [done](std::error_code e, auto) { done(e); }); },
                 [&] { impl->socket.close(); });
    if (ec) return failure(ec);
    return TcpStream{std::move(impl)};
}

std::expected<void, std::error_code> TcpStream::write(std::span<const std::byte> bytes) {
    std::error_code ec;
    asio::write(impl_->socket, asio::buffer(bytes.data(), bytes.size()), ec);
    if (ec) return failure(ec);
    return {};
}

std::expected<std::size_t, std::error_code> TcpStream::read_some(std::span<std::byte> into) {
    std::size_t n = 0;
    const auto ec = run_for(impl_->io, impl_->read_timeout,
                            [&](auto done) { impl_->socket.async_read_some(asio::buffer(into.data(), into.size()), done); },
                            [&] { impl_->socket.cancel(); }, &n);
    if (ec == asio::error::eof) return 0;
    if (ec) return failure(ec);
    return n;
}

void TcpStream::close() {
    if (!impl_) return;
    std::error_code ignored;
    impl_->socket.shutdown(asio::ip::tcp::socket::shutdown_both, ignored);
    impl_->socket.close(ignored);
}

// --- TcpListener ---------------------------------------------------------------------------------

struct TcpListener::Impl {
    asio::io_context io;
    asio::ip::tcp::acceptor acceptor{io};
};

TcpListener::TcpListener(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
TcpListener::TcpListener(TcpListener&&) noexcept = default;
TcpListener& TcpListener::operator=(TcpListener&&) noexcept = default;
TcpListener::~TcpListener() { close(); }

std::expected<TcpListener, std::error_code> TcpListener::bind(std::string_view host, std::uint16_t port) {
    auto impl = std::make_unique<Impl>();
    std::error_code ec;
    const auto address = asio::ip::make_address(std::string(host), ec);
    if (ec) return failure(ec);
    const asio::ip::tcp::endpoint endpoint{address, port};
    impl->acceptor.open(endpoint.protocol(), ec);
    if (!ec) impl->acceptor.set_option(asio::ip::tcp::acceptor::reuse_address(true), ec);
    if (!ec) impl->acceptor.bind(endpoint, ec);
    if (!ec) impl->acceptor.listen(asio::socket_base::max_listen_connections, ec);
    if (ec) return failure(ec);
    return TcpListener{std::move(impl)};
}

std::uint16_t TcpListener::port() const {
    std::error_code ec;
    return impl_->acceptor.local_endpoint(ec).port();
}

std::expected<TcpStream, std::error_code> TcpListener::accept(milliseconds timeout, milliseconds read_timeout) {
    auto stream = std::make_unique<TcpStream::Impl>();
    stream->read_timeout = read_timeout;
    // Accepted straight onto the stream's socket: it lives on the stream's own context, the
    // accept runs on this listener's.
    const auto ec = run_for(impl_->io, timeout,
                            [&](auto done) { impl_->acceptor.async_accept(stream->socket, [done](std::error_code e) { done(e); }); },
                            [&] { impl_->acceptor.cancel(); });
    if (ec) return failure(ec);
    return TcpStream{std::move(stream)};
}

void TcpListener::close() {
    if (!impl_) return;
    std::error_code ignored;
    impl_->acceptor.close(ignored);
}

// macOS's termios stops at B230400, so Asio refuses 921600 and up; IOSSIOSPEED sets any rate
// (what pyserial does). It must come after the other options: tcsetattr would reset the speed.
namespace {
std::error_code set_baud(asio::serial_port& port, unsigned baud) {
    std::error_code ec;
    port.set_option(asio::serial_port_base::baud_rate(baud), ec);
#if defined(__APPLE__)
    if (ec == asio::error::invalid_argument) {
        speed_t speed = baud;
        if (::ioctl(port.native_handle(), IOSSIOSPEED, &speed) == -1) return {errno, std::system_category()};
        return {};
    }
#endif
    return ec;
}
} // namespace

// --- SerialPort ----------------------------------------------------------------------------------

struct SerialPort::Impl {
    asio::io_context io;
    asio::serial_port port{io};
    milliseconds read_timeout;
};

SerialPort::SerialPort(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
SerialPort::SerialPort(SerialPort&&) noexcept = default;
SerialPort& SerialPort::operator=(SerialPort&&) noexcept = default;
SerialPort::~SerialPort() { close(); }

std::expected<SerialPort, std::error_code> SerialPort::open(std::string_view device, Options options) {
    auto impl = std::make_unique<Impl>();
    impl->read_timeout = options.read_timeout;
    std::error_code ec;
    impl->port.open(std::string(device), ec);
    using sp = asio::serial_port_base;
    if (!ec) impl->port.set_option(sp::character_size(8), ec);
    if (!ec) impl->port.set_option(sp::parity(sp::parity::none), ec);
    if (!ec) impl->port.set_option(sp::stop_bits(sp::stop_bits::one), ec);
    if (!ec) impl->port.set_option(sp::flow_control(sp::flow_control::none), ec);
    if (!ec) ec = set_baud(impl->port, options.baud);
    if (ec) return failure(ec);
    return SerialPort{std::move(impl)};
}

std::expected<void, std::error_code> SerialPort::write(std::span<const std::byte> bytes) {
    std::error_code ec;
    asio::write(impl_->port, asio::buffer(bytes.data(), bytes.size()), ec);
    if (ec) return failure(ec);
    return {};
}

std::expected<std::size_t, std::error_code> SerialPort::read_some(std::span<std::byte> into) {
    std::size_t n = 0;
    const auto ec = run_for(impl_->io, impl_->read_timeout,
                            [&](auto done) { impl_->port.async_read_some(asio::buffer(into.data(), into.size()), done); },
                            [&] { std::error_code ignored; impl_->port.cancel(ignored); }, &n);
    if (ec == asio::error::eof) return 0;
    if (ec) return failure(ec);
    return n;
}

void SerialPort::close() {
    if (!impl_) return;
    std::error_code ignored;
    impl_->port.close(ignored);
}

} // namespace prism::io
