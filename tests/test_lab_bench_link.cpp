// The lab bench over TCP (examples/model_lab_bench/scpi_link.hpp): the simulated instrument served
// on localhost, read by the client the hardware thread uses. Same numbers as the local simulator.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "../examples/model_lab_bench/scpi_link.hpp"

#include <thread>

using namespace prism;
using namespace std::chrono_literals;

namespace {
struct Served {
    io::TcpListener listener = *io::TcpListener::bind("127.0.0.1", 0);
    std::jthread thread{[this](std::stop_token stop) { lab::serve_instrument(listener, stop, [] { return 0.42; }); }};
};
}

TEST_CASE("a frame read over TCP is the frame the simulator computes") {
    Served server;
    auto bench = lab::RemoteInstrument::connect("127.0.0.1", server.listener.port());
    REQUIRE(bench);

    lab::DaqSettings settings{};
    auto first = bench->read(settings, false);
    REQUIRE(first);
    CHECK(*first == lab::synthesize_telemetry(settings, 0.42));

    settings.target_voltage = 3.0;
    settings.wave_type = lab::WaveformType::Square;
    auto second = bench->read(settings, false);   // the change is sent before the read
    REQUIRE(second);
    CHECK(*second == lab::synthesize_telemetry(settings, 0.42));
}

TEST_CASE("a bench whose instrument went away gets an error, not a hang") {
    std::optional<Served> server{std::in_place};
    auto bench = lab::RemoteInstrument::connect("127.0.0.1", server->listener.port());
    REQUIRE(bench);
    REQUIRE(bench->read({}, false));
    server.reset();   // stops serving and closes the connection
    CHECK_FALSE(bench->read({}, false));
}

TEST_CASE("host:port is split; anything else is refused") {
    CHECK(lab::parse_endpoint("192.168.1.42:5025") == std::pair{std::string("192.168.1.42"), std::uint16_t{5025}});
    CHECK_FALSE(lab::parse_endpoint("5025"));
    CHECK_FALSE(lab::parse_endpoint("host:"));
    CHECK_FALSE(lab::parse_endpoint("host:70000"));
}
