#pragma once

#include <prism/app/model_app.hpp>
#include <prism/app/window_geometry.hpp>
#include <prism/backends/settings_dir.hpp>

#include <cctype>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

// app::run: a whole main() in one call -- the window, the model, and the settings that survive
// a restart.
namespace prism::app {

// What survives a restart: a struct of fields, saved to <config dir>/<organization>/<name>/
// settings.toml whenever one changes, and loaded back at the next start. Give it with
// `.settings = &my_settings`. If the struct has a `WindowGeometry window` member, the window
// opens at the size (and place) it was left at.
// Declared only: the TOML code behind it is heavy, and an app without settings never needs it.
template <typename S>
class SettingsFile;

struct SavedSettings {
    SavedSettings() = default;
    template <typename S>
    SavedSettings(S* settings)   // implicit, so `.settings = &my_settings` reads as it says
        : open{[settings](std::filesystem::path file) -> std::shared_ptr<void> {
              static_assert(requires { sizeof(SettingsFile<S>); },
                            "passing .settings needs #include <prism/app/settings.hpp>");
              return std::make_shared<SettingsFile<S>>(*settings, std::move(file));
          }},
          window{window_of(settings)} {}

    // Loads `file` into the struct and saves each change until the result is dropped.
    std::function<std::shared_ptr<void>(std::filesystem::path)> open{};
    WindowGeometry* window = nullptr;

private:
    template <typename S>
    static WindowGeometry* window_of(S* settings) {
        if constexpr (requires { { settings->window } -> std::same_as<WindowGeometry&>; })
            return &settings->window;
        else
            return nullptr;
    }
};

struct AppConfig {
    std::string_view title = "PRISM";
    int width = 800;
    int height = 600;
    SavedSettings settings{};
    std::string_view organization{};   // the settings folder's parent (SDL asks for one)
    std::string_view name{};            // the settings folder; default: the title, dashed
    DecorationMode decoration = DecorationMode::Custom;
    // When set, the app closes itself this long after setup, exactly as if the user closed the
    // main window (for CI and headless runs). When not set, run() reads PRISM_QUIT_AFTER_MS.
    std::optional<std::chrono::milliseconds> quit_after{};
};

namespace run_detail {

// "PRISM Synth Rack" -> "prism-synth-rack": letters and digits, runs of anything else as one dash.
inline std::string settings_name(const AppConfig& cfg) {
    if (!cfg.name.empty()) return std::string(cfg.name);
    std::string out;
    for (unsigned char c : cfg.title) {
        if (std::isalnum(c)) out.push_back(static_cast<char>(std::tolower(c)));
        else if (!out.empty() && out.back() != '-') out.push_back('-');
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out;
}

// PRISM_QUIT_AFTER_MS as a duration: the whole string must be a non-negative integer, else the
// variable is ignored (nullopt).
inline std::optional<std::chrono::milliseconds> quit_after_from_env(const char* text) {
    if (!text) return std::nullopt;
    const std::string_view s{text};
    if (s.empty()) return std::nullopt;
    unsigned long long value = 0;
    const auto res = std::from_chars(s.data(), s.data() + s.size(), value);
    if (res.ec != std::errc{} || res.ptr != s.data() + s.size()) return std::nullopt;
    if (value > static_cast<unsigned long long>(std::numeric_limits<long long>::max()))
        return std::nullopt;   // not a chrono::milliseconds count
    return std::chrono::milliseconds{static_cast<long long>(value)};
}

inline std::shared_ptr<void> open_settings(const AppConfig& cfg) {
    if (!cfg.settings.open) return nullptr;
    const auto dir = backends::settings_dir(std::string(cfg.organization), settings_name(cfg));
    return cfg.settings.open(dir / "settings.toml");
}

// Closes the main window `after` from now, exactly as if the user closed it: a timer thread
// sleeps, then hands the close to the logic thread. The poster may outlive the app — once the
// app's wake gate is closed it drops the close — so the thread needs no joining. run() uses
// this for AppConfig::quit_after / PRISM_QUIT_AFTER_MS (CI and headless runs).
inline void schedule_quit(AppContext& ctx, std::chrono::milliseconds after) {
    auto poster = ctx.poster();
    AppContext* ctxp = &ctx;   // the close runs on the logic thread, so only while the app lives
    std::thread([poster = std::move(poster), ctxp,
                 deadline = std::chrono::steady_clock::now() + after] {
        std::this_thread::sleep_until(deadline);
        poster([ctxp] { ctxp->close_main_window(); });
    }).detach();
}

} // namespace run_detail

// Opens the window, runs `model` until it closes, and saves `cfg.settings` as it changes.
// `setup` runs once on the logic thread, as model_app's does.
template <typename Model>
int run(Model& model, const AppConfig& cfg, std::function<void(AppContext&)> setup = {}) {
    const auto saved = run_detail::open_settings(cfg);   // loaded before the window opens
    const std::string title{cfg.title};   // WindowConfig wants a C string; a view may not end in one
    WindowConfig window{.title = title.c_str(), .width = cfg.width, .height = cfg.height,
                        .decoration = cfg.decoration};
    if (cfg.settings.window) window = cfg.settings.window->applied_to(window);
    // cfg wins when set; the environment variable is the no-code-change default (CI, headless).
    const auto quit_after = cfg.quit_after
        ? cfg.quit_after
        : run_detail::quit_after_from_env(std::getenv("PRISM_QUIT_AFTER_MS"));
    model_app(window, model, [&cfg, setup = std::move(setup), quit_after](AppContext& ctx) {
        if (cfg.settings.window) ctx.remember_window_size(*cfg.settings.window);
        if (setup) setup(ctx);
        if (quit_after) run_detail::schedule_quit(ctx, *quit_after);   // that long after setup
    });
    return 0;
}

} // namespace prism::app
