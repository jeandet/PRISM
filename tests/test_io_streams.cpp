// io::TcpStream, io::TcpListener and io::SerialPort (include/prism/io/streams.hpp): loopback TCP,
// timeouts that free a blocked thread, and a serial port on a Linux pseudo-terminal.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/io/streams.hpp>

#include <chrono>
#include <string>
#include <thread>

#if defined(__linux__)
#include <fcntl.h>
#include <pty.h>
#include <termios.h>
#include <unistd.h>
#endif

using namespace prism;
using namespace std::chrono_literals;

namespace {
// Echoes each line back upper-cased, for one connection.
std::jthread echo_server(io::TcpListener& listener) {
    return std::jthread([&listener] {
        auto conn = listener.accept(5000ms);
        if (!conn) return;
        io::LineStream lines{*conn};
        while (auto line = lines.read_line()) {
            for (char& c : *line) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            if (!lines.write_line(*line)) return;
        }
    });
}
}

TEST_CASE("a TCP query round-trips through a listener on localhost") {
    auto listener = io::TcpListener::bind("127.0.0.1", 0);
    REQUIRE(listener);
    CHECK(listener->port() != 0);
    auto server = echo_server(*listener);
    auto conn = io::TcpStream::connect("127.0.0.1", listener->port());
    REQUIRE(conn);
    io::LineStream lines{*conn};
    CHECK(lines.query("meas:volt?").value() == "MEAS:VOLT?");
    CHECK(lines.query("second").value() == "SECOND");
    conn->close();
}

TEST_CASE("a peer that never answers times out instead of blocking") {
    auto listener = io::TcpListener::bind("127.0.0.1", 0);
    REQUIRE(listener);
    std::jthread silent{[&] {
        auto conn = listener->accept(5000ms);
        std::this_thread::sleep_for(600ms);   // holds the connection open, says nothing
    }};
    auto conn = io::TcpStream::connect("127.0.0.1", listener->port(), {.read_timeout = 100ms});
    REQUIRE(conn);
    const auto began = std::chrono::steady_clock::now();
    io::LineStream lines{*conn};
    CHECK(lines.query("*IDN?").error() == std::errc::timed_out);
    CHECK(std::chrono::steady_clock::now() - began < 500ms);
}

TEST_CASE("nothing listening: connect fails; accept with nobody coming times out") {
    auto listener = io::TcpListener::bind("127.0.0.1", 0);
    REQUIRE(listener);
    const auto free_port = listener->port();
    CHECK(listener->accept(50ms).error() == std::errc::timed_out);
    listener->close();
    CHECK_FALSE(io::TcpStream::connect("127.0.0.1", free_port, {.connect_timeout = 500ms}));
}

#if defined(__linux__)
TEST_CASE("a serial port reads and writes lines (a pseudo-terminal stands in for the device)") {
    int controller = -1;
    int device = -1;
    char name[128]{};
    REQUIRE(openpty(&controller, &device, name, nullptr, nullptr) == 0);
    termios raw{};
    tcgetattr(controller, &raw);
    cfmakeraw(&raw);
    tcsetattr(controller, TCSANOW, &raw);

    auto port = io::SerialPort::open(name, {.baud = 9600, .read_timeout = 200ms});
    REQUIRE(port);
    io::LineStream lines{*port};
    REQUIRE(lines.write_line("*IDN?"));
    char got[32]{};
    std::size_t n = 0;
    for (auto waited = 0ms; n < 6 && waited < 1000ms; waited += 5ms) {
        const auto r = ::read(controller, got + n, sizeof(got) - 1 - n);
        if (r > 0) n += static_cast<std::size_t>(r);
        else std::this_thread::sleep_for(5ms);
    }
    CHECK(std::string(got, n) == "*IDN?\n");

    const std::string reply = "PRISM,PSU,0,1.0\r\n";
    REQUIRE(::write(controller, reply.data(), reply.size()) == static_cast<ssize_t>(reply.size()));
    CHECK(lines.read_line().value() == "PRISM,PSU,0,1.0");
    CHECK(lines.read_line().error() == std::errc::timed_out);   // and then silence

    port->close();
    ::close(device);
    ::close(controller);
}
#endif

TEST_CASE("opening a serial device that does not exist fails") {
    CHECK_FALSE(io::SerialPort::open("/dev/prism-no-such-port"));
}
