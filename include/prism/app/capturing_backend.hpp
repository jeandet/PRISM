#pragma once

#include <prism/app/backend.hpp>
#include <prism/app/headless_window.hpp>

#include <memory>

namespace prism::backends {
// The bundled fonts' measurer, for a capture with no window (in libprism, by the SDL backend).
std::shared_ptr<const ui::TextMeasurer> headless_text_measurer();
}

namespace prism::app {

class CapturingBackend final : public BackendBase {
public:
    // `measurer`: real text widths for the layout (a screenshot wants them); none gives estimates.
    explicit CapturingBackend(std::shared_ptr<const render::SceneSnapshot>& snap_out,
                              const ui::TextMeasurer* measurer = nullptr)
        : snap_ref_(snap_out), measurer_(measurer) {}

    const ui::TextMeasurer* text_measurer() const override { return measurer_; }

    Window& create_window(WindowConfig cfg) override {
        window_ = HeadlessWindow{1, cfg};
        return window_;
    }

    void run(std::function<void(const WindowEvent&)> cb) override {
        cb(WindowEvent{window_.id(), WindowClose{}});
    }

    void submit(WindowId, std::shared_ptr<const render::SceneSnapshot> s) override {
        snap_ref_ = std::move(s);
    }

    void wake() override {}
    void quit() override {}

private:
    std::shared_ptr<const render::SceneSnapshot>& snap_ref_;
    HeadlessWindow window_{0, {}};
    const ui::TextMeasurer* measurer_;
};

} // namespace prism::app
