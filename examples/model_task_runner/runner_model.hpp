#pragma once

// What a build console has to get right, as plain functions: cutting a byte stream into lines,
// reading progress out of them, telling an error from a warning, filtering, and the simulated
// workloads that stand in for a compiler when there is no compiler to run.
//
// Nothing here knows about threads, I/O or the UI.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace prism::runner {

// --- text ---------------------------------------------------------------------------------

inline char lower(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

inline bool contains_icase(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) return true;
    if (needle.size() > haystack.size()) return false;
    for (std::size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
        std::size_t j = 0;
        while (j < needle.size() && lower(haystack[i + j]) == lower(needle[j])) ++j;
        if (j == needle.size()) return true;
    }
    return false;
}

// Drops the escape sequences a compiler colours its output with, plus the other control bytes
// (a bare '\r' from a redrawing progress bar among them): what a console should show.
inline std::string strip_ansi(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c != 0x1b) {
            if (c >= 0x20 || c == '\t') out.push_back(text[i]);
            continue;
        }
        if (++i >= text.size()) break;
        if (text[i] == '[') {                     // CSI: parameters, then one final byte
            for (++i; i < text.size(); ++i)
                if (text[i] >= '@' && text[i] <= '~') break;
        } else if (text[i] == ']') {              // OSC: up to BEL or ESC '\'
            // The terminator is consumed here, ESC '\' body included: leaving the '\' behind
            // would put it in the output.
            while (++i < text.size()) {
                if (text[i] == '\a') break;
                if (text[i] == 0x1b && i + 1 < text.size() && text[i + 1] == '\\') {
                    ++i;
                    break;
                }
            }
        }
    }
    return out;
}

// A byte stream cut into lines. Chunks are arbitrary: a line may be split across any number of
// them, and the last one may never be terminated at all.
class LineAssembler {
public:
    // Every line this chunk completes; the tail of a split line stays pending.
    std::vector<std::string> feed(std::string_view chunk) {
        std::vector<std::string> lines;
        std::size_t from = 0;
        for (auto nl = chunk.find('\n', from); nl != std::string_view::npos; nl = chunk.find('\n', from)) {
            pending_.append(chunk.substr(from, nl - from));
            lines.push_back(take_pending());
            from = nl + 1;
        }
        pending_.append(chunk.substr(from));
        return lines;
    }

    // What the writer left unterminated when the stream ended.
    std::vector<std::string> finish() {
        if (pending_.empty()) return {};
        return {take_pending()};
    }

private:
    std::string take_pending() {
        if (!pending_.empty() && pending_.back() == '\r') pending_.pop_back();
        std::string line = std::exchange(pending_, {});
        return line;
    }

    std::string pending_{};
};

// Bytes in, finished and cleaned lines out: the one call a run's reader needs.
inline void decode_chunk(LineAssembler& assembler, std::string_view chunk, std::vector<std::string>& out) {
    for (auto& line : assembler.feed(chunk))
        out.push_back(strip_ansi(line));
}

// --- progress -----------------------------------------------------------------------------

inline std::size_t skip_spaces(std::string_view text, std::size_t i) {
    while (i < text.size() && text[i] == ' ') ++i;
    return i;
}

// The number starting at `i`, and where it ends; -1 when there is none.
inline std::pair<double, std::size_t> read_number(std::string_view text, std::size_t i) {
    double value = 0.0;
    std::size_t digits = 0;
    for (; i < text.size() && text[i] >= '0' && text[i] <= '9'; ++i, ++digits)
        value = value * 10.0 + static_cast<double>(text[i] - '0');
    return {digits > 0 ? value : -1.0, i};
}

// ` 42%` or `  3/120` inside a bracket, as the fraction done.
inline std::optional<double> parse_progress_body(std::string_view inner) {
    auto i = skip_spaces(inner, 0);
    auto [first, after] = read_number(inner, i);
    if (first < 0.0) return std::nullopt;
    i = skip_spaces(inner, after);
    if (i < inner.size() && inner[i] == '%') {
        if (skip_spaces(inner, i + 1) != inner.size()) return std::nullopt;
        return std::clamp(first / 100.0, 0.0, 1.0);
    }
    if (i < inner.size() && inner[i] == '/') {
        auto [total, after_total] = read_number(inner, skip_spaces(inner, i + 1));
        if (total <= 0.0 || skip_spaces(inner, after_total) != inner.size()) return std::nullopt;
        return std::clamp(first / total, 0.0, 1.0);
    }
    return std::nullopt;
}

// The first `[...]` in the line that reads as progress: ninja, ctest and meson all tag their
// lines this way.
inline std::optional<double> parse_progress(std::string_view line) {
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] != '[') continue;
        const auto close = line.find(']', i + 1);
        if (close == std::string_view::npos) return std::nullopt;
        if (auto done = parse_progress_body(line.substr(i + 1, close - i - 1))) return done;
        i = close;
    }
    return std::nullopt;
}

// --- lines --------------------------------------------------------------------------------

enum class LineKind : std::uint8_t { Info, Command, Success, Warning, Error };

// First match wins, so an error that happens to mention a warning stays an error.
inline LineKind classify_line(std::string_view line) {
    if (contains_icase(line, "error") || contains_icase(line, "failed") || contains_icase(line, "failure"))
        return LineKind::Error;
    if (contains_icase(line, "warning") || contains_icase(line, "warn:")) return LineKind::Warning;
    if (contains_icase(line, "passed") || contains_icase(line, "ok ") || contains_icase(line, "finished")
        || contains_icase(line, "succeeded") || contains_icase(line, "done"))
        return LineKind::Success;
    if (!line.empty() && line.front() == '$') return LineKind::Command;
    return LineKind::Info;
}

struct ConsoleLine {
    std::string text{};
    LineKind kind = LineKind::Info;
    bool operator==(const ConsoleLine&) const = default;
};

// What the console shows of what a run said.
struct LineFilter {
    std::string needle{};        // empty: everything
    bool errors_only = false;    // errors and warnings only

    bool keeps(std::string_view text, LineKind kind) const {
        if (errors_only && kind != LineKind::Error && kind != LineKind::Warning) return false;
        return contains_icase(text, needle);
    }
    bool operator==(const LineFilter&) const = default;
};

// What the console shows of what a run said: the lines the filter keeps, in order. Runs on every
// filter edit, hence the reserve -- a 200 000-line console refilters in a few milliseconds.
inline std::vector<ConsoleLine> filtered_lines(const std::vector<ConsoleLine>& lines, const LineFilter& filter) {
    std::vector<ConsoleLine> kept;
    kept.reserve(lines.size());
    for (const auto& line : lines)
        if (filter.keeps(line.text, line.kind)) kept.push_back(line);
    return kept;
}

// --- the simulated workloads --------------------------------------------------------------
//
// A compiler's output as a pure function of (script, line index): same seed, same lines, same
// timings, so the interesting states -- a warning, a failing build, a console at full tilt --
// are reproducible and testable without running anything.

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

// --- the tasks ----------------------------------------------------------------------------

struct TaskSpec {
    std::string_view id{};
    std::string_view name{};
    std::string_view summary{};
    std::string_view command{};          // what the task runs, shown in the console header
    std::optional<SimKind> simulated{};  // empty: run `command` for real (desktop only)
};

// A browser has no shell: on the web only the simulated tasks and the custom box are offered
// (and a custom run explains what it cannot do there).
inline constexpr bool task_available(const TaskSpec& spec, bool native_processes) {
    return native_processes || spec.simulated.has_value() || spec.command.empty();
}

inline constexpr TaskSpec kTasks[] = {
    {.id = "build", .name = "Build", .summary = "A simulated 120-unit compile: progress, warnings, the odd error.",
     .command = "ninja -C builddir", .simulated = SimKind::Build},
    {.id = "tests", .name = "Unit tests", .summary = "A simulated test runner: suites, cases, one failure now and then.",
     .command = "ctest --output-on-failure", .simulated = SimKind::Tests},
    {.id = "flood", .name = "Flood", .summary = "200 000 lines as fast as the console takes them.",
     .command = "seq 200000", .simulated = SimKind::Flood},
    {.id = "ping", .name = "ping", .summary = "A real process: five ICMP echoes to loopback.",
     .command = "ping -c 5 127.0.0.1"},
    {.id = "ls", .name = "ls", .summary = "A real process: the contents of /usr/bin.",
     .command = "ls -la /usr/bin"},
    {.id = "uname", .name = "uname", .summary = "A real process: the kernel this runs on.",
     .command = "uname -a"},
    {.id = "custom", .name = "Custom", .summary = "Whatever the box beside the list holds, run by the shell.",
     .command = ""},
};

inline constexpr std::size_t kTaskCount = std::size(kTasks);
inline constexpr std::size_t kCustomTask = kTaskCount - 1;

// The two tasks the headless capture seeds its frame from, named so the app does not index the
// catalogue by number.
inline constexpr std::size_t kBuildTask = 0;
inline constexpr std::size_t kFloodTask = 2;
static_assert(kTasks[kBuildTask].simulated == SimKind::Build);
static_assert(kTasks[kFloodTask].simulated == SimKind::Flood);


} // namespace prism::runner
