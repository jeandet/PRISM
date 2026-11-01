#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/utf8.hpp>

#include <string>

namespace utf8 = prism::core::utf8;

// "a" (1 byte) "é" (C3 A9) "€" (E2 82 AC) "😀" (F0 9F 98 80) -> boundaries 0 1 3 6 10
static const std::string mixed = "a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80";

TEST_CASE("utf8::next steps over whole code points") {
    CHECK(utf8::next(mixed, 0) == 1);
    CHECK(utf8::next(mixed, 1) == 3);
    CHECK(utf8::next(mixed, 3) == 6);
    CHECK(utf8::next(mixed, 6) == 10);
    CHECK(utf8::next(mixed, 10) == 10);   // at end: stays
    CHECK(utf8::next(mixed, 99) == 10);   // past end: clamps
}

TEST_CASE("utf8::snap moves a mid-sequence index back to its code point start") {
    CHECK(utf8::snap(mixed, 0) == 0);
    CHECK(utf8::snap(mixed, 2) == 1);
    CHECK(utf8::snap(mixed, 5) == 3);
    CHECK(utf8::snap(mixed, 9) == 6);
    CHECK(utf8::snap(mixed, 10) == 10);
    CHECK(utf8::snap(mixed, 99) == 10);
}

TEST_CASE("utf8::prev steps back one code point") {
    CHECK(utf8::prev(mixed, 10) == 6);
    CHECK(utf8::prev(mixed, 3) == 1);
    CHECK(utf8::prev(mixed, 1) == 0);
    CHECK(utf8::prev(mixed, 0) == 0);
}

TEST_CASE("utf8::count_upto and utf8::offset are inverses at every boundary") {
    const size_t bounds[] = {0, 1, 3, 6, 10};
    for (size_t cp = 0; cp < 5; ++cp) {
        CHECK(utf8::count_upto(mixed, bounds[cp]) == cp);
        CHECK(utf8::offset(mixed, cp) == bounds[cp]);
    }
    CHECK(utf8::offset(mixed, 99) == mixed.size());
    CHECK(utf8::count(mixed) == 4);
}

TEST_CASE("utf8 helpers on empty and malformed input do not run past the end") {
    CHECK(utf8::next("", 0) == 0);
    CHECK(utf8::count("") == 0);
    const std::string truncated = "a\xC3";       // lead byte with no continuation
    CHECK(utf8::next(truncated, 1) == 2);
    CHECK(utf8::snap(truncated, 2) == 2);
    CHECK(utf8::count(truncated) == 2);
}
