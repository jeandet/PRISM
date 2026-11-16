#pragma once

#include <prism/io/streams.hpp>

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>

#include "daq/scpi.hpp"
#include "hardware_thread.hpp"

// The bench over a real link: the simulated instrument behind a TCP port, and the client the
// hardware thread reads it with. Both are plain blocking code on their own threads; the model
// only ever sees the DaqTelemetry the client returns, as with the local simulator.
namespace prism::lab {

using namespace std::chrono_literals;

// Serves one connection: lines in, replies out, until the peer leaves or `stop` is requested.
// `now` is the instrument's clock in seconds (a test pins it).
inline void serve_connection(io::TcpStream& conn, const std::stop_token& stop, const std::function<double()>& now) {
    scpi::InstrumentState state;
    io::LineStream lines{conn};
    while (!stop.stop_requested()) {
        auto line = lines.read_line();
        if (!line) {
            if (line.error() == std::errc::timed_out) continue;   // an idle client: keep waiting
            return;
        }
        auto reply = scpi::respond(state, *line, now());
        if (!reply) continue;
        if (std::exchange(state.stall_next, false)) stall_for(stop, kStall);
        if (!lines.write_line(*reply)) return;
    }
}

// Accepts clients one after the other until `stop`.
inline void serve_instrument(io::TcpListener& listener, const std::stop_token& stop,
                             const std::function<double()>& now) {
    while (!stop.stop_requested()) {
        auto conn = listener.accept(200ms, 200ms);
        if (conn) serve_connection(*conn, stop, now);
    }
}

// The client: sends the front panel when it changed, then reads one frame (MEAS?, then WAV?).
class RemoteInstrument {
public:
    // Longer than a stall, so the stall button blocks this thread as it does locally.
    static constexpr auto kReadTimeout = kStall + 1000ms;

    static std::expected<RemoteInstrument, std::error_code> connect(const std::string& host, std::uint16_t port) {
        auto stream = io::TcpStream::connect(host, port, {.connect_timeout = 2000ms, .read_timeout = kReadTimeout});
        if (!stream) return std::unexpected(stream.error());
        return RemoteInstrument{std::make_unique<io::TcpStream>(std::move(*stream))};
    }

    std::expected<DaqTelemetry, std::error_code> read(const DaqSettings& settings, bool stall) {
        if (sent_ != settings) {
            for (const auto& command : scpi::settings_commands(settings))
                if (auto ok = lines_->write_line(command); !ok) return std::unexpected(ok.error());
            sent_ = settings;
        }
        if (stall)
            if (auto ok = lines_->write_line("SYST:STALL"); !ok) return std::unexpected(ok.error());
        auto meas = lines_->query("MEAS?");
        if (!meas) return std::unexpected(meas.error());
        auto wav = lines_->query("WAV?");
        if (!wav) return std::unexpected(wav.error());
        DaqTelemetry t;
        auto samples = scpi::read_waveform(*wav);
        if (!scpi::read_measurement(*meas, t) || !samples) return std::unexpected(std::make_error_code(std::errc::bad_message));
        t.scope_samples = std::move(*samples);
        return t;
    }

private:
    explicit RemoteInstrument(std::unique_ptr<io::TcpStream> stream)
        : stream_(std::move(stream)), lines_(std::make_unique<io::LineStream<io::TcpStream>>(*stream_)) {}

    std::unique_ptr<io::TcpStream> stream_;   // on the heap: the LineStream points at it
    std::unique_ptr<io::LineStream<io::TcpStream>> lines_;
    std::optional<DaqSettings> sent_;
};

// "host:port" -> its two halves; nullopt when it is not that.
inline std::optional<std::pair<std::string, std::uint16_t>> parse_endpoint(std::string_view text) {
    const auto colon = text.rfind(':');
    if (colon == std::string_view::npos || colon == 0) return std::nullopt;
    const auto port = scpi::parse_number<int>(text.substr(colon + 1));
    if (!port || *port <= 0 || *port > 65535) return std::nullopt;
    return std::pair{std::string(text.substr(0, colon)), static_cast<std::uint16_t>(*port)};
}

} // namespace prism::lab
