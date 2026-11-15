// Tour 3: a worker thread sets a core::Shared<T>; the logic thread receives it and updates the UI.
#include <prism/prism.hpp>
#include <prism/app/run.hpp>

#include <chrono>
#include <string>
#include <thread>

using namespace prism;

struct Ticker {
    core::Field<ui::Label<>> reading{{"waiting for the worker..."}};
    core::Shared<int> ticks{};

    // Declared last, so it stops before the state it writes to goes away.
    std::jthread worker{[this](std::stop_token stop) {
        for (int n = 0; !stop.stop_requested(); ++n) {
            ticks.set(n);
            std::this_thread::sleep_for(std::chrono::milliseconds{100});
        }
    }};

    // Runs once on the logic thread: from here on every set() on `ticks` wakes the app with the
    // newest value, and that callback may touch the model because it runs there too.
    void attach(app::AppContext& ctx) {
        ctx.receive(ticks, [this](int n) { reading.set({"tick " + std::to_string(n)}); });
    }

    void view(app::WidgetTree::ViewBuilder& vb) { vb.widget(reading); }
};

int main() {
    Ticker ticker;
    return app::run(ticker, {.title = "PRISM Tour 3: A Worker Thread",
                             .width = 420, .height = 120});
}

// At scale: examples/model_system_monitor/ does this for /proc, with a Channel of events besides.
