#pragma once

#include <prism/prism.hpp>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "../daq/model.hpp"
#include "event_log.hpp"

namespace prism::lab::blocks {

// The stress panel: the two stalls this example exists to compare, and the fault injection that
// makes the instrument report a state change. Nothing here is a Field of the model's: the buttons
// write to the worker's link (a one-shot atomic, a Shared) and to the log.
struct Diagnostics {
    Diagnostics(std::atomic<bool>& stall, core::Shared<bool>& fault, EventLog& events)
        : stall_request{stall}, fault_request{fault}, log{events} {
        reactions_.push_back(lag.on_change().connect([this](const auto&) {
            stall_request = true;   // handed to the hardware thread, which does the waiting
            log.push("Stalling the simulated USB-TMC read for 2 s");
        }));
        reactions_.push_back(block.on_change().connect([this](const auto&) {
            // The comparison this example exists to make: the same blocking call, on the thread
            // that owns the model. Every frame, resize and click waits behind it.
            block_this_thread();
            log.push("Logic thread was blocked for 2 s", true);
        }));
        reactions_.push_back(trip_fault.on_change().connect([this](const auto&) {
            fault_request.set(true);
            log.push("Over-current protection triggered", true);
        }));
        reactions_.push_back(reset_fault.on_change().connect([this](const auto&) {
            fault_request.set(false);
            log.push("Protection circuit reset");
        }));
    }

    std::atomic<bool>& stall_request;
    core::Shared<bool>& fault_request;
    EventLog& log;

    core::Field<ui::Button> lag{{.text = "Stall the hardware thread (2 s)", .primary = false}};
    core::Field<ui::Button> block{{.text = "Stall the logic thread (2 s)", .primary = false}};
    core::Field<ui::Button> trip_fault{{.text = "Trip over-current protection", .primary = false}};
    core::Field<ui::Button> reset_fault{{.text = "Reset protection circuit", .primary = false}};
    core::Field<ui::Label<>> hint{
        {"Both stall buttons block for two seconds. One blocks the hardware thread; the other "
         "blocks the thread that owns the model, which is what a GUI toolkit without a separate "
         "renderer and model thread looks like.",
         /*heading=*/false, /*wrap=*/true}};

    void view(app::ViewBuilder& vb) {
        vb.scroll([&] {
            vb.widget(lag).tooltip("The hardware thread waits; the window does not");
            vb.widget(block).tooltip("The logic thread waits: the window stops responding");
            vb.widget(trip_fault);
            vb.widget(reset_fault);
            vb.widget(hint);
        });
    }

private:
    static void block_this_thread() {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
        while (std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds{20});
    }

    // Declared last, so the connections go before the fields they observe.
    std::vector<core::Connection> reactions_{};
};

} // namespace prism::lab::blocks
