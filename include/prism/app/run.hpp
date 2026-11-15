#pragma once

#include <prism/app/model_app.hpp>
#include <prism/app/settings.hpp>
#include <prism/app/window_geometry.hpp>
#include <prism/backends/settings_dir.hpp>

#include <cctype>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

// app::run: a whole main() in one call -- the window, the model, and the settings that survive
// a restart.
namespace prism::app {

// What survives a restart: a struct of fields, saved to <config dir>/<organization>/<name>/
// settings.toml whenever one changes, and loaded back at the next start. Give it with
// `.settings = &my_settings`. If the struct has a `WindowGeometry window` member, the window
// opens at the size (and place) it was left at.
struct SavedSettings {
    SavedSettings() = default;
    template <typename S>
    SavedSettings(S* settings)   // implicit, so `.settings = &my_settings` reads as it says
        : open{[settings](std::filesystem::path file) -> std::shared_ptr<void> {
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

inline std::shared_ptr<void> open_settings(const AppConfig& cfg) {
    if (!cfg.settings.open) return nullptr;
    const auto dir = backends::settings_dir(std::string(cfg.organization), settings_name(cfg));
    return cfg.settings.open(dir / "settings.toml");
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
    model_app(window, model, [&cfg, setup = std::move(setup)](AppContext& ctx) {
        if (cfg.settings.window) ctx.remember_window_size(*cfg.settings.window);
        if (setup) setup(ctx);
    });
    return 0;
}

} // namespace prism::app
