// app::run's settings: the struct the app names in `.settings` is saved on every change and
// loaded at start, with the window size when the struct has one.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/run.hpp>
#include <prism/app/settings.hpp>

#include <chrono>
#include <atomic>
#include <cstdlib>
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
struct Tiny {
    core::Field<int> count{0};
    void view(app::ViewBuilder& vb) { vb.widget(count); }
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

TEST_CASE("app::run with quit_after closes itself and returns 0, well under 10 s") {
    run_test::Tiny model;
    app::AppConfig cfg{.title = "prism test quit_after"};
    cfg.quit_after = std::chrono::milliseconds(200);

    // If run() never returns (quit_after not acted on yet), fail this binary after 20 s instead
    // of hanging the whole suite; with the quit working, run() returns after ~200 ms.
    const auto returned = std::make_shared<std::atomic<bool>>(false);
    std::thread([returned] {
        std::this_thread::sleep_for(std::chrono::seconds(20));
        if (!returned->load(std::memory_order_acquire)) std::_Exit(1);
    }).detach();

    const auto start = std::chrono::steady_clock::now();
    const int rc = app::run(model, cfg);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    returned->store(true, std::memory_order_release);

    CHECK(rc == 0);
    CHECK(elapsed < std::chrono::seconds(10));
}

TEST_CASE("run_detail::quit_after_from_env parses PRISM_QUIT_AFTER_MS, ignores what does not parse") {
    using namespace std::chrono;
    CHECK_FALSE(app::run_detail::quit_after_from_env(nullptr).has_value());
    CHECK_FALSE(app::run_detail::quit_after_from_env("").has_value());
    CHECK_FALSE(app::run_detail::quit_after_from_env("abc").has_value());
    CHECK_FALSE(app::run_detail::quit_after_from_env("-5").has_value());
    CHECK_FALSE(app::run_detail::quit_after_from_env("250ms").has_value());
    const auto parsed = app::run_detail::quit_after_from_env("250");
    REQUIRE(parsed.has_value());
    CHECK(*parsed == milliseconds(250));
}
