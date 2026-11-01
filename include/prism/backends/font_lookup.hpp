#pragma once

#include <filesystem>
#include <string>

namespace prism::backends {

// Where a bundled font lives: an explicit path wins; then the build tree (running from builddir);
// then the install prefix of the PRISM library itself -- <libdir>/.. or, for multiarch libdirs
// like lib/x86_64-linux-gnu, <libdir>/../.. -- joined with the datadir-relative install path.
// Relative to the library, not the executable: an interpreter (Python in a venv) or any app
// links PRISM from wherever it was installed. Empty if none exists.
std::string find_font(const char* override_path, const char* build_path,
                      const std::filesystem::path& library_dir, const std::filesystem::path& install_rel);

// Directory of the loaded PRISM backend library; empty where the platform can't tell.
std::filesystem::path backend_library_dir();

} // namespace prism::backends
