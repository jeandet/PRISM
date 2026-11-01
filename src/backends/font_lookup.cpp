#include <prism/backends/font_lookup.hpp>

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
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

std::filesystem::path backend_library_dir() {
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
    Dl_info info{};
    if (dladdr(reinterpret_cast<void*>(&backend_library_dir), &info) && info.dli_fname) {
        std::error_code ec;
        auto path = std::filesystem::canonical(info.dli_fname, ec);
        if (!ec) return path.parent_path();
    }
#endif
    // simplify: Windows would use GetModuleHandleEx + GetModuleFileNameW; the web build
    // preloads fonts at their build-tree paths, so it never gets here.
    return {};
}

} // namespace prism::backends
