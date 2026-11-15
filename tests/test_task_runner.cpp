#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "../examples/model_task_runner/runner_model.hpp"

#include <string>
#include <vector>

using namespace prism::runner;

TEST_CASE("chunks split inside a line keep the tail") {
    LineAssembler assembler;
    CHECK(assembler.feed("alpha\nbe") == std::vector<std::string>{"alpha"});
    CHECK(assembler.feed("ta\ngamma\n") == std::vector<std::string>{"beta", "gamma"});
    CHECK(assembler.feed("delta").empty());
    CHECK(assembler.finish() == std::vector<std::string>{"delta"});
}

TEST_CASE("carriage returns and blank lines") {
    LineAssembler assembler;
    CHECK(assembler.feed("a\r\n\r\nb\n") == std::vector<std::string>{"a", "", "b"});
    CHECK(assembler.finish().empty());
}

TEST_CASE("an escape sequence may be split across chunks") {
    LineAssembler assembler;
    std::vector<std::string> lines;
    decode_chunk(assembler, "\x1b[3", lines);
    decode_chunk(assembler, "2mgreen\x1b[0m\n", lines);
    CHECK(lines == std::vector<std::string>{"green"});
}

TEST_CASE("a line the writer never terminated still comes out") {
    LineAssembler assembler;
    CHECK(assembler.feed("one\ntwo\r").size() == 1);
    CHECK(assembler.finish() == std::vector<std::string>{"two"});
}

TEST_CASE("ansi escapes and other control bytes are dropped") {
    CHECK(strip_ansi("\x1b[1;31mred\x1b[0m") == "red");
    CHECK(strip_ansi("\x1b]0;window title\ared") == "red");
    CHECK(strip_ansi("\x1b]0;window title\x1b\\red") == "red");
    CHECK(strip_ansi("a\rb") == "ab");
    CHECK(strip_ansi("keep\tthe tab") == "keep\tthe tab");
    CHECK(strip_ansi("\x1b[38;5;") == "");
}

TEST_CASE("progress reads both shapes of tag") {
    CHECK(parse_progress("[ 42%] Compiling") == doctest::Approx(0.42));
    CHECK(parse_progress("[120/120] Compiling") == doctest::Approx(1.0));
    CHECK(parse_progress("[  3/120] Compiling") == doctest::Approx(3.0 / 120.0));
    CHECK(parse_progress("  [100%] 511 passed") == doctest::Approx(1.0));
    CHECK(parse_progress("[3/120]") == doctest::Approx(3.0 / 120.0));
    CHECK(parse_progress("[ 42 %]") == doctest::Approx(0.42));
}

TEST_CASE("what is not progress stays nothing") {
    CHECK_FALSE(parse_progress("no brackets here").has_value());
    CHECK_FALSE(parse_progress("[ 42 ]").has_value());
    CHECK_FALSE(parse_progress("[]").has_value());
    CHECK_FALSE(parse_progress("[0/0]").has_value());
    CHECK_FALSE(parse_progress("[ 42%").has_value());
    CHECK_FALSE(parse_progress("[ok Storage / keeps order]").has_value());
}

TEST_CASE("a line is an error, a warning, a success, a command or just output") {
    CHECK(classify_line("src/a.cpp:12:3: error: expected ';'") == LineKind::Error);
    CHECK(classify_line("  FAILED Storage / large file") == LineKind::Error);
    CHECK(classify_line("src/a.cpp:12:3: warning: unused variable") == LineKind::Warning);
    CHECK(classify_line("  ok Storage / keeps order") == LineKind::Success);
    CHECK(classify_line("[100%] 511 passed in 8.1 s") == LineKind::Success);
    CHECK(classify_line("$ ninja -C builddir") == LineKind::Command);
    CHECK(classify_line("Compiling src/core/field.cpp") == LineKind::Info);
}

TEST_CASE("the filter keeps what it should") {
    const LineFilter all{};
    CHECK(all.keeps("anything at all", LineKind::Info));

    const LineFilter link{.needle = "LINK"};
    CHECK(link.keeps("Linking build/libprism.so", LineKind::Info));
    CHECK_FALSE(link.keeps("Compiling src/core/field.cpp", LineKind::Info));

    const LineFilter broken{.errors_only = true};
    CHECK(broken.keeps("error: nope", LineKind::Error));
    CHECK(broken.keeps("warning: hmm", LineKind::Warning));
    CHECK_FALSE(broken.keeps("  ok Storage", LineKind::Success));

    const LineFilter both{.needle = "socket", .errors_only = true};
    CHECK(both.keeps("error: no such socket", LineKind::Error));
    CHECK_FALSE(both.keeps("socket closed", LineKind::Info));
}

TEST_CASE("the console shows the lines the filter lets through") {
    const std::vector<ConsoleLine> lines{
        {"$ ninja", LineKind::Command},
        {"Compiling a.cpp", LineKind::Info},
        {"a.cpp: error: nope", LineKind::Error},
        {"b.cpp: warning: hmm", LineKind::Warning},
    };
    CHECK(filtered_lines(lines, LineFilter{}).size() == lines.size());

    const auto broken = filtered_lines(lines, LineFilter{.errors_only = true});
    REQUIRE(broken.size() == 2);
    CHECK(broken[0].text == "a.cpp: error: nope");
    CHECK(broken[1].kind == LineKind::Warning);

    const auto compiled = filtered_lines(lines, LineFilter{.needle = "compil"});
    REQUIRE(compiled.size() == 1);
    CHECK(compiled[0].text == "Compiling a.cpp");
}

TEST_CASE("a build that passes walks every unit and says so") {
    const auto run = simulated_build(0);
    REQUIRE(run.line_count == kBuildUnits + 1);
    CHECK(run.exit_code == 0);
    CHECK(run.duration_s == doctest::Approx(kBuildSeconds + kBuildSeconds / kBuildUnits));

    CHECK(classify_line(simulated_line(run, 0)) == LineKind::Info);
    CHECK(parse_progress(simulated_line(run, 0)) == doctest::Approx(1.0 / kBuildUnits));
    CHECK(classify_line(simulated_line(run, kBuildUnits)) == LineKind::Success);
}

TEST_CASE("a build can fail two thirds of the way in") {
    const auto run = simulated_build(2);
    REQUIRE(run.exit_code == 1);
    REQUIRE(run.line_count < kBuildUnits);
    CHECK(classify_line(simulated_line(run, run.line_count - 2)) == LineKind::Error);
    CHECK(classify_line(simulated_line(run, run.line_count - 1)) == LineKind::Error);
}

TEST_CASE("progress only ever climbs, warnings and all") {
    const auto run = simulated_build(1);
    double last = 0.0;
    for (std::size_t i = 0; i < run.line_count; ++i) {
        const auto line = simulated_line(run, i);
        if (const auto done = parse_progress(line)) {
            CHECK(*done >= last);
            last = *done;
        }
    }
    CHECK(last >= 1.0);

    bool saw_warning = false;
    for (std::size_t i = 0; i < run.line_count; ++i)
        saw_warning = saw_warning || classify_line(simulated_line(run, i)) == LineKind::Warning;
    CHECK(saw_warning);
}

TEST_CASE("the same seed gives the same run") {
    for (unsigned seed : {0u, 1u, 2u, 7u}) {
        const auto run = simulated_build(seed);
        const auto again = simulated_build(seed);
        REQUIRE(run == again);
        for (std::size_t i = 0; i < run.line_count; ++i)
            REQUIRE(simulated_line(run, i) == simulated_line(again, i));
    }
}

TEST_CASE("a test run reports its cases and its summary") {
    const auto passing = simulated_tests(0);
    CHECK(passing.exit_code == 0);
    CHECK(classify_line(simulated_line(passing, passing.line_count - 1)) == LineKind::Success);

    const auto failing = simulated_tests(3);
    REQUIRE(failing.exit_code == 1);
    CHECK(classify_line(simulated_line(failing, failing.line_count - 1)) == LineKind::Error);
    bool saw_failed_case = false;
    for (std::size_t i = 0; i < failing.line_count; ++i)
        saw_failed_case = saw_failed_case || simulated_line(failing, i).starts_with("  FAILED");
    CHECK(saw_failed_case);
}

TEST_CASE("the flood is not paced and its lines carry progress") {
    const auto run = simulated_flood();
    CHECK(run.line_count == 200'000);
    CHECK(simulated_time(run, 0) == 0.0);
    CHECK(simulated_time(run, 199'999) == 0.0);
    CHECK(parse_progress(simulated_line(run, 0)) == doctest::Approx(1.0 / 200'000.0));
    CHECK(parse_progress(simulated_line(run, 199'999)) == doctest::Approx(1.0));
    CHECK(simulated_line(run, 0) != simulated_line(run, 1));
}

TEST_CASE("a paced run starts now and ends at its duration") {
    const auto run = simulated_build(0);
    CHECK(simulated_time(run, 0) < simulated_time(run, run.line_count - 1));
    CHECK(simulated_time(run, run.line_count - 1) == doctest::Approx(run.duration_s));
}

TEST_CASE("the catalogue offers the simulated tasks everywhere and the rest on a desktop") {
    CHECK(kTaskCount == 7);
    CHECK(kTasks[kCustomTask].id == "custom");
    for (const auto& task : kTasks) {
        CHECK_FALSE(task.name.empty());
        CHECK_FALSE(task.summary.empty());
    }
    CHECK(task_available(kTasks[0], /*native_processes=*/false));    // Build
    CHECK(task_available(kTasks[3], /*native_processes=*/true));     // ping
    CHECK_FALSE(task_available(kTasks[3], /*native_processes=*/false));
    CHECK(task_available(kTasks[kCustomTask], /*native_processes=*/false));
}
