#pragma once

#include <prism/core/channel.hpp>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#include "daq/instrument.hpp"
#include "daq/model.hpp"

// The glue between the model and what it reads: how often the instrument is polled, how long a
// stall lasts, and what the worker reports back. The instrument it reads is pure (daq/); this is
// the only file in the example that knows both a thread and a Channel.
namespace prism::lab {

using steady = std::chrono::steady_clock;

inline constexpr auto kStall = std::chrono::milliseconds{2000};    // a USB-TMC transaction that hangs
inline constexpr auto kPollPeriod = std::chrono::milliseconds{25}; // ~40 Hz sampling
inline constexpr auto kStopSlice = std::chrono::milliseconds{20};

// A blocking hardware read that still notices a stop request: quitting must not wait out a stall.
inline void stall_for(const std::stop_token& stop, std::chrono::milliseconds total) {
    auto end = steady::now() + total;
    while (steady::now() < end && !stop.stop_requested()) std::this_thread::sleep_for(kStopSlice);
}

// The simulated instrument's blocking read. Pure logic is in synthesize_telemetry(); this is the
// glue that makes it take time, which is the only thing it adds.
inline DaqTelemetry hardware_read(const DaqSettings& settings, double time_sec, const std::stop_token& stop,
                                  std::atomic<bool>& stall_request) {
    if (stall_request.exchange(false)) stall_for(stop, kStall);
    return synthesize_telemetry(settings, time_sec);
}

// What the hardware thread reports through the Channel: state changes it observed, not polled
// guesses. Ordered and lossless, unlike the coalescing Shared the frames travel in.
inline void report_transitions(core::Channel<DaqEvent>& out, const DaqTelemetry& before,
                              const DaqTelemetry& after) {
    if (after.fault_tripped != before.fault_tripped)
        out.send({.message = after.fault_tripped ? "hardware: over-current protection tripped"
                                                 : "hardware: protection reset",
                  .is_error = after.fault_tripped});
    if (after.constant_current != before.constant_current)
        out.send({.message = after.constant_current ? "hardware: supply entered constant-current mode"
                                                    : "hardware: supply entered constant-voltage mode"});
}

} // namespace prism::lab
