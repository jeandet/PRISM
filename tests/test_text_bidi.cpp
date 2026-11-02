// Bidirectional text (UAX #9): visual runs, the caret and hit testing in mixed-direction lines.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/ui/text_bidi.hpp>
#include <prism/ui/text_metrics.hpp>

using namespace prism::ui;

namespace {
// Without a measurer every character is 0.6 em wide: 6 points at size 10.
constexpr float size = 10.f;
constexpr float ch = 6.f;
const std::string hebrew = "\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d";   // "shalom": 4 letters, 8 bytes
}

TEST_CASE("a left-to-right line is one run") {
    auto runs = bidi_runs("hello");
    REQUIRE(runs.size() == 1);
    CHECK(runs[0] == BidiRun{0, 5, false});
}

TEST_CASE("a Hebrew line is one right-to-left run") {
    auto runs = bidi_runs(hebrew);
    REQUIRE(runs.size() == 1);
    CHECK(runs[0] == BidiRun{0, 8, true});
}

TEST_CASE("a Hebrew word inside English text is its own run, in place") {
    std::string line = "ab " + hebrew + " cd";   // bytes: "ab " 0-3, Hebrew 3-11, " cd" 11-14
    auto runs = bidi_runs(line);
    REQUIRE(runs.size() == 3);
    CHECK(runs[0] == BidiRun{0, 3, false});
    CHECK(runs[1] == BidiRun{3, 11, true});
    CHECK(runs[2] == BidiRun{11, 14, false});
}

TEST_CASE("in a Hebrew line, an English word comes first on screen (runs are in visual order)") {
    std::string line = hebrew + " ab";   // a right-to-left paragraph: "ab" ends up on the left
    auto runs = bidi_runs(line);
    REQUIRE(runs.size() == 2);
    CHECK(runs[0] == BidiRun{9, 11, false});   // "ab", leftmost
    CHECK(runs[1] == BidiRun{0, 9, true});     // Hebrew and the space, on the right
}

TEST_CASE("an empty line has no runs") {
    CHECK(bidi_runs("").empty());
}

TEST_CASE("the caret in left-to-right text sits after the prefix, as before") {
    CHECK(caret_x_at(nullptr, "hello", 0, size) == 0.f);
    CHECK(caret_x_at(nullptr, "hello", 2, size) == doctest::Approx(2 * ch));
    CHECK(caret_x_at(nullptr, "hello", 5, size) == doctest::Approx(5 * ch));
}

TEST_CASE("the caret in Hebrew text moves right to left") {
    // Before the first letter: the right edge. After the last: the left edge.
    CHECK(caret_x_at(nullptr, hebrew, 0, size) == doctest::Approx(4 * ch));
    CHECK(caret_x_at(nullptr, hebrew, 2, size) == doctest::Approx(3 * ch));   // after one letter
    CHECK(caret_x_at(nullptr, hebrew, 8, size) == doctest::Approx(0.f));
}

TEST_CASE("the caret after a Hebrew letter typed into English text is on that letter's left") {
    std::string line = "ab " + hebrew.substr(0, 2);   // "ab " then one letter, typed in that order
    // The caret goes with the letter just typed, where the next one will appear: its left edge.
    CHECK(caret_x_at(nullptr, line, line.size(), size) == doctest::Approx(3 * ch));
}

TEST_CASE("a click on a Hebrew letter maps back to that letter's offset") {
    std::string line = "ab " + hebrew + " cd";   // the Hebrew run spans x 18..42 on screen
    // The first letter (bytes 3-5) is drawn rightmost, at x 36..42; its right edge is offset 3.
    CHECK(index_at(nullptr, line, size, 41.f) == 3);
    // The last letter (bytes 9-11) is drawn leftmost, at x 18..24; its left edge is offset 11.
    CHECK(index_at(nullptr, line, size, 19.f) == 11);
    CHECK(index_at(nullptr, line, size, 2.f) == 0);    // English still maps as before
    CHECK(index_at(nullptr, line, size, 83.f) == 14);  // past the end
}

TEST_CASE("caret_x_at and index_at agree on every offset of a mixed line") {
    std::string line = "ab " + hebrew + " cd";
    for (std::size_t b : {0u, 1u, 2u, 5u, 7u, 9u, 12u, 13u, 14u})
        CHECK(index_at(nullptr, line, size, caret_x_at(nullptr, line, b, size)) == b);
}

TEST_CASE("a selection across a direction change covers one span per run it touches") {
    std::string line = "ab " + hebrew + " cd";
    // Select "b " and the first Hebrew letter: x 6..18 in English, then that letter's cell, 36..42.
    auto spans = selection_spans(nullptr, line, 1, 5, size);
    REQUIRE(spans.size() == 2);
    CHECK(spans[0].first == doctest::Approx(6.f));
    CHECK(spans[0].second == doctest::Approx(18.f));
    CHECK(spans[1].first == doctest::Approx(36.f));
    CHECK(spans[1].second == doctest::Approx(42.f));
}

TEST_CASE("a left-to-right selection is a single span, as before") {
    auto spans = selection_spans(nullptr, "hello", 1, 3, size);
    REQUIRE(spans.size() == 1);
    CHECK(spans[0].first == doctest::Approx(ch));
    CHECK(spans[0].second == doctest::Approx(3 * ch));
}
