#pragma once

// A compiler's output as a pure function of (script, line index): same seed, same lines, same
// timings, so the interesting states -- a warning, a failing build, a console at full tilt --
// are reproducible and testable without running anything.
//
// This is what makes the example work in a browser, where there is no shell to run a build with.

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace prism::runner {

enum class SimKind : std::uint8_t { Build, Tests, Flood };

struct SimRun {
    SimKind kind = SimKind::Build;
    unsigned seed = 0;
    std::size_t line_count = 0;
    double duration_s = 0.0;   // 0: as fast as the console takes them
    int exit_code = 0;
    bool operator==(const SimRun&) const = default;
};

inline std::string padded(std::size_t value, std::size_t width) {
    std::string digits = std::to_string(value);
    return std::string(width > digits.size() ? width - digits.size() : 0, ' ') + digits;
}

inline constexpr std::string_view kBuildDirs[] = {"core", "render", "ui", "delegates",
                                                  "app", "widgets", "backends"};
inline constexpr std::string_view kBuildFiles[] = {"field", "draw_list", "layout", "button",
                                                   "widget_tree", "table", "sdl_backend", "shared"};
inline constexpr std::string_view kTestSuites[] = {"Storage", "Index", "Parser", "Network",
                                                   "Scheduler", "Geometry", "Renderer", "Catalog"};
inline constexpr std::string_view kTestCases[] = {"keeps insertion order", "rolls back a partial write",
                                                  "reopens after a truncation", "rejects a short header",
                                                  "splits a large batch", "keeps ties stable"};

inline constexpr std::size_t kBuildUnits = 120;
inline constexpr double kBuildSeconds = 10.0;
inline constexpr std::size_t kTestsLines = 97;      // 8 suites of 12, then the summary
inline constexpr std::size_t kTestsPerSuite = 12;
inline constexpr std::size_t kFloodLines = 200'000;

// 120 compile units in ten seconds; every third seed trips an error two thirds of the way in.
inline SimRun simulated_build(unsigned seed) {
    const bool fails = seed % 3 == 2;
    const std::size_t units = fails ? 97 : kBuildUnits;
    const auto per_line = kBuildSeconds / static_cast<double>(kBuildUnits);
    return SimRun{.kind = SimKind::Build,
                  .seed = seed,
                  .line_count = units + 1,
                  .duration_s = per_line * static_cast<double>(units + 1),
                  .exit_code = fails ? 1 : 0};
}

// A test runner's log; every fourth seed has one case fail.
inline SimRun simulated_tests(unsigned seed) {
    const bool fails = seed % 4 == 3;
    return SimRun{.kind = SimKind::Tests,
                  .seed = seed,
                  .line_count = kTestsLines,
                  .duration_s = 8.1,
                  .exit_code = fails ? 1 : 0};
}

// 200 000 lines as fast as they come: the console under pressure.
inline SimRun simulated_flood(std::size_t lines = kFloodLines) {
    return SimRun{.kind = SimKind::Flood, .seed = 0, .line_count = lines, .duration_s = 0.0, .exit_code = 0};
}

// The script a simulated task runs; the seed is which launch it is, so the failures come round.
inline SimRun simulated_run(SimKind kind, unsigned seed) {
    switch (kind) {
    case SimKind::Build: return simulated_build(seed);
    case SimKind::Tests: return simulated_tests(seed);
    case SimKind::Flood: break;
    }
    return simulated_flood();
}

inline std::string build_line(std::size_t index) {
    const std::string file = std::string(kBuildDirs[index % std::size(kBuildDirs)]) + "/"
        + std::string(kBuildFiles[index % std::size(kBuildFiles)]);
    if (index % 23 == 11)
        return "src/" + file + ".cpp:42:13: warning: unused parameter 'count' [-Wunused-parameter]";
    return "[" + padded(index + 1, 3) + "/" + std::to_string(kBuildUnits) + "] Compiling src/" + file + ".cpp";
}

inline std::string build_failure_line() {
    return "src/net/socket.cpp:88:9: error: no matching function for call to 'connect'";
}

inline std::string build_summary_line(const SimRun& run) {
    if (run.exit_code != 0) return "ninja: build stopped: subcommand failed.";
    return "Build finished: " + std::to_string(kBuildUnits) + "/" + std::to_string(kBuildUnits) + " in 10.0 s";
}

inline std::string tests_line(const SimRun& run, std::size_t index) {
    const auto suite = kTestSuites[(index / kTestsPerSuite) % std::size(kTestSuites)];
    if (index % kTestsPerSuite == 0) {
        const auto percent = index * 100 / kTestsLines;
        return "[" + padded(percent, 3) + "%] Running " + std::string(suite) + "Test (48 tests)";
    }
    const auto name = kTestCases[index % std::size(kTestCases)];
    if (run.exit_code != 0 && index == 61) return "  FAILED " + std::string(suite) + " / " + std::string(name);
    return "  ok " + std::string(suite) + " / " + std::string(name);
}

inline std::string tests_summary_line(const SimRun& run) {
    if (run.exit_code != 0) return "[100%] 510 passed, 1 failed in 8.1 s";
    return "[100%] 511 passed in 8.1 s";
}

inline std::string flood_line(std::size_t index, std::size_t total) {
    return "[" + padded(index + 1, 7) + "/" + std::to_string(total) + "] stage "
        + std::to_string(index % 9) + ": compacting batch " + std::to_string(index / 500) + " of "
        + std::to_string(total / 500) + ", 1200 records/s";
}

// Line `index` of a script. The last one is the summary for Build and Tests.
inline std::string simulated_line(const SimRun& run, std::size_t index) {
    switch (run.kind) {
    case SimKind::Build: {
        const std::size_t units = run.line_count - 1;   // the unit lines, before the summary
        if (index >= units) return build_summary_line(run);
        if (run.exit_code != 0 && index + 1 == units) return build_failure_line();
        return build_line(index);
    }
    case SimKind::Tests:
        return index + 1 == run.line_count ? tests_summary_line(run) : tests_line(run, index);
    case SimKind::Flood:
        return flood_line(index, run.line_count);
    }
    return {};
}

// When line `index` is due, counted from the run's start. 0 duration means no pacing at all.
inline double simulated_time(const SimRun& run, std::size_t index) {
    if (run.duration_s <= 0.0 || run.line_count == 0) return 0.0;
    return run.duration_s * static_cast<double>(index + 1) / static_cast<double>(run.line_count);
}

// A whole script at once: what the headless capture fills a console with.
inline std::vector<std::string> simulated_lines(const SimRun& run, std::size_t count) {
    std::vector<std::string> texts;
    texts.reserve(count);
    for (std::size_t i = 0; i < count; ++i) texts.push_back(simulated_line(run, i));
    return texts;
}

} // namespace prism::runner
