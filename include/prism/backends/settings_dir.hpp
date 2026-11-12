#pragma once

#include <filesystem>
#include <string>

namespace prism::backends {

// The folder where this user's settings for `app` belong, created if needed: ~/.local/share/<org>/<app>
// on Linux, %APPDATA%\<org>\<app> on Windows, ~/Library/Application Support/<org>/<app> on macOS
// (SDL_GetPrefPath). Empty if the platform has none.
std::filesystem::path settings_dir(const std::string& org, const std::string& app);

} // namespace prism::backends
