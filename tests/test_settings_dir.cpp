// The per-user settings folder, through SDL_GetPrefPath.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/backends/settings_dir.hpp>

#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

#if defined(__linux__)
TEST_CASE("the settings folder is the app's own, under XDG_DATA_HOME on Linux, and exists") {
    auto home = fs::temp_directory_path() / ("prism-xdg-" + std::to_string(::getpid()));
    ::setenv("XDG_DATA_HOME", home.c_str(), 1);
    auto dir = prism::backends::settings_dir("prism-test", "notes");
    CHECK(fs::equivalent(dir, home / "prism-test" / "notes"));
    CHECK(fs::is_directory(dir));
    fs::remove_all(home);
}
#endif

TEST_CASE("two apps get two folders") {
    auto a = prism::backends::settings_dir("prism-test", "a");
    auto b = prism::backends::settings_dir("prism-test", "b");
    REQUIRE_FALSE(a.empty());
    CHECK(a != b);
    fs::remove_all(a.parent_path());
}
