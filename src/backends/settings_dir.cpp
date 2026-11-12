#include <prism/backends/settings_dir.hpp>

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

namespace prism::backends {

std::filesystem::path settings_dir(const std::string& org, const std::string& app) {
    char* dir = SDL_GetPrefPath(org.c_str(), app.c_str());
    if (!dir) return {};
    std::filesystem::path out{reinterpret_cast<const char8_t*>(dir)};   // UTF-8, also on Windows
    SDL_free(dir);
    return out;
}

} // namespace prism::backends
