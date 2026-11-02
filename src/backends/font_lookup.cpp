#include <prism/backends/font_lookup.hpp>

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#elif !defined(__EMSCRIPTEN__)
#include <dlfcn.h>
#endif

namespace prism::backends {

std::string find_font(const char* override_path, const char* build_path,
                      const std::filesystem::path& library_dir, const std::filesystem::path& install_rel) {
    namespace fs = std::filesystem;
    if (override_path) return override_path;
    std::error_code ec;
    if (build_path && fs::exists(build_path, ec)) return build_path;
    if (library_dir.empty() || install_rel.empty()) return {};
    for (auto prefix : {library_dir / "..", library_dir / ".." / ".."}) {
        auto candidate = prefix / install_rel;
        if (fs::exists(candidate, ec)) return fs::canonical(candidate, ec).string();
    }
    return {};
}

// The folder of the module (shared library or executable) this function is in. The web build
// preloads fonts at their build-tree paths, so it never needs this.
std::filesystem::path backend_library_dir() {
#if defined(_WIN32)
    HMODULE module = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&backend_library_dir), &module)) {
        std::wstring path(MAX_PATH, L'\0');
        DWORD n = 0;
        while ((n = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()))) == path.size())
            path.resize(path.size() * 2);   // truncated: try a longer buffer
        if (n > 0) {
            path.resize(n);
            return std::filesystem::path(path).parent_path();
        }
    }
#elif !defined(__EMSCRIPTEN__)
    Dl_info info{};
    if (dladdr(reinterpret_cast<void*>(&backend_library_dir), &info) && info.dli_fname) {
        std::error_code ec;
        auto path = std::filesystem::canonical(info.dli_fname, ec);
        if (!ec) return path.parent_path();
    }
#endif
    return {};
}

} // namespace prism::backends
