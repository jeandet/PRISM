#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "instrument.hpp"
#include "model.hpp"

// The bench's instrument as SCPI text, both ways, with no IO: the commands that set it up, the two
// queries that read it (MEAS? the readout, WAV? the scope trace) and their replies. Numbers are
// written in their shortest exact form, so a value read back is the value that was sent.
namespace prism::lab::scpi {

inline constexpr std::string_view kIdentity = "PRISM,Simulated bench PSU,0,1.0";
inline constexpr std::array<std::string_view, 5> kWaveNames{"SIN", "SQU", "TRI", "RAMP", "NOIS"};

template <typename T>
std::string number(T value) {
    char buffer[64];
    const auto end = std::to_chars(buffer, buffer + sizeof(buffer), value).ptr;
    return std::string(buffer, end);
}

template <typename T>
std::optional<T> parse_number(std::string_view text) {
    T value{};
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || end != text.data() + text.size()) return std::nullopt;
    return value;
}

inline std::string on_off(bool on) { return on ? "ON" : "OFF"; }

// The front panel, as the commands a bench script would send.
inline std::vector<std::string> settings_commands(const DaqSettings& s) {
    return {"VOLT " + number(s.target_voltage),
            "CURR " + number(s.current_limit),
            "OUTP " + on_off(s.output_enabled),
            "FUNC " + std::string(kWaveNames[static_cast<std::size_t>(s.wave_type)]),
            "FREQ " + number(s.frequency_hz),
            "VOLT:AMPL " + number(s.amplitude_vpp),
            "VOLT:OFFS " + number(s.offset_v),
            "SYST:FAULT " + on_off(s.simulate_fault)};
}

// The instrument side: one command applied to its settings; false for one it does not know.
inline bool apply_command(DaqSettings& s, std::string_view line) {
    const auto space = line.find(' ');
    if (space == std::string_view::npos) return false;
    const auto head = line.substr(0, space);
    const auto arg = line.substr(space + 1);
    auto set_number = [&](double& field) {
        const auto v = parse_number<double>(arg);
        if (v) field = *v;
        return v.has_value();
    };
    auto set_flag = [&](bool& field) {
        if (arg != "ON" && arg != "OFF") return false;
        field = arg == "ON";
        return true;
    };
    if (head == "VOLT") return set_number(s.target_voltage);
    if (head == "CURR") return set_number(s.current_limit);
    if (head == "OUTP") return set_flag(s.output_enabled);
    if (head == "FREQ") return set_number(s.frequency_hz);
    if (head == "VOLT:AMPL") return set_number(s.amplitude_vpp);
    if (head == "VOLT:OFFS") return set_number(s.offset_v);
    if (head == "SYST:FAULT") return set_flag(s.simulate_fault);
    if (head == "FUNC") {
        const auto it = std::ranges::find(kWaveNames, arg);
        if (it == kWaveNames.end()) return false;
        s.wave_type = static_cast<WaveformType>(it - kWaveNames.begin());
        return true;
    }
    return false;
}

inline std::vector<std::string_view> split_commas(std::string_view text) {
    std::vector<std::string_view> parts;
    for (std::size_t start = 0;;) {
        const auto comma = text.find(',', start);
        parts.push_back(text.substr(start, comma - start));
        if (comma == std::string_view::npos) return parts;
        start = comma + 1;
    }
}

// MEAS?: the readout and the scope's measurements, in this order.
inline std::string measurement_reply(const DaqTelemetry& t) {
    return number(t.voltage_sense) + "," + number(t.current_sense) + "," + number(t.power_watts) + "," +
           number(t.temperature_c) + "," + number(int{t.constant_current}) + "," + number(int{t.fault_tripped}) + "," +
           number(static_cast<int>(t.wave_type)) + "," + number(t.measured_vpp) + "," + number(t.measured_vrms) + "," +
           number(t.measured_freq) + "," + number(t.v_per_div) + "," + number(t.time_per_div);
}

inline bool read_measurement(std::string_view reply, DaqTelemetry& t) {
    const auto f = split_commas(reply);
    if (f.size() != 12) return false;
    auto d = [&](std::size_t i, double& out) {
        auto v = parse_number<double>(f[i]);
        if (v) out = *v;
        return v.has_value();
    };
    auto flag = [&](std::size_t i, bool& out) {
        auto v = parse_number<int>(f[i]);
        if (v) out = *v != 0;
        return v.has_value();
    };
    const auto wave = parse_number<int>(f[6]);
    if (!wave || *wave < 0 || *wave >= static_cast<int>(kWaveNames.size())) return false;
    t.wave_type = static_cast<WaveformType>(*wave);
    return d(0, t.voltage_sense) && d(1, t.current_sense) && d(2, t.power_watts) && d(3, t.temperature_c) &&
           flag(4, t.constant_current) && flag(5, t.fault_tripped) && d(7, t.measured_vpp) &&
           d(8, t.measured_vrms) && d(9, t.measured_freq) && d(10, t.v_per_div) && d(11, t.time_per_div);
}

// WAV?: the trace of the frame the last MEAS? measured.
inline std::string waveform_reply(const std::vector<float>& samples) {
    std::string out;
    for (std::size_t i = 0; i < samples.size(); ++i) {
        if (i) out += ',';
        out += number(samples[i]);
    }
    return out;
}

inline std::optional<std::vector<float>> read_waveform(std::string_view reply) {
    std::vector<float> samples;
    if (reply.empty()) return samples;
    for (auto part : split_commas(reply)) {
        auto v = parse_number<float>(part);
        if (!v) return std::nullopt;
        samples.push_back(*v);
    }
    return samples;
}

// The instrument's side of a connection: its settings, the frame the last MEAS? measured, and
// whether the next reply should hang (SYST:STALL, the front panel's stall button over the wire).
struct InstrumentState {
    DaqSettings settings{};
    DaqTelemetry frame{};
    bool stall_next = false;
};

// One line in, the reply out; a command has none (SCPI commands are silent), nor does one the
// instrument does not know. `now` is the instrument's clock, in seconds.
inline std::optional<std::string> respond(InstrumentState& state, std::string_view line, double now) {
    if (line == "*IDN?") return std::string(kIdentity);
    if (line == "MEAS?") {
        state.frame = synthesize_telemetry(state.settings, now);
        return measurement_reply(state.frame);
    }
    if (line == "WAV?") return waveform_reply(state.frame.scope_samples);
    if (line == "SYST:STALL") {
        state.stall_next = true;
        return std::nullopt;
    }
    if (line.ends_with('?')) return "ERR";
    apply_command(state.settings, line);
    return std::nullopt;
}

} // namespace prism::lab::scpi
