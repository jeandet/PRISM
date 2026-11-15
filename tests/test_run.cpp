// app::run's settings: the struct the app names in `.settings` is saved on every change and
// loaded at start, with the window size when the struct has one.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/run.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

using namespace prism;
namespace fs = std::filesystem;

// Named, not anonymous: Glaze's reflection under clang needs types with linkage.
namespace run_test {
struct Plain {
    core::Field<int> volume{3};
};
struct WithWindow {
    core::Field<int> volume{3};
    app::WindowGeometry window;
};
} // namespace run_test

namespace {
std::string read_file(const fs::path& p) {
    std::ifstream in(p);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}
} // namespace

TEST_CASE("the settings folder is named after the title unless told otherwise") {
    CHECK(app::run_detail::settings_name({.title = "PRISM Synth Rack"}) == "prism-synth-rack");
    CHECK(app::run_detail::settings_name({.title = "Lab  Bench & DAQ"}) == "lab-bench-daq");
    CHECK(app::run_detail::settings_name({.title = "X", .name = "custom"}) == "custom");
}

TEST_CASE("a settings struct with a window size hands it over; one without has none") {
    run_test::Plain plain;
    run_test::WithWindow with;
    CHECK(app::SavedSettings{&plain}.window == nullptr);
    CHECK(app::SavedSettings{&with}.window == &with.window);
    CHECK_FALSE(app::SavedSettings{}.open);   // no settings: nothing saved
}

TEST_CASE("opened settings load what the file had, and save each change") {
    const fs::path file = fs::temp_directory_path() / "prism_test_run" / "settings.toml";
    fs::remove_all(file.parent_path());
    fs::create_directories(file.parent_path());
    std::ofstream(file) << "volume = 7\n";

    run_test::Plain plain;
    {
        const auto keep = app::SavedSettings{&plain}.open(file);
        CHECK(plain.volume.get() == 7);
        plain.volume.set(9);
    }   // closing writes what is pending
    CHECK(read_file(file).find("volume = 9") != std::string::npos);
}
