#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "../examples/model_notes/notes_logic.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <random>

namespace fs = std::filesystem;

namespace {
struct TempDir {
    fs::path root = fs::temp_directory_path() / ("prism-notes-" + std::to_string(std::random_device{}()));
    TempDir() { fs::create_directories(root); }
    ~TempDir() { fs::remove_all(root); }
};
}

TEST_CASE("save then load round-trips the text") {
    TempDir d;
    auto file = d.root / "a.txt";
    REQUIRE(notes::save_text(file, "caf\xC3\xA9\nline 2\n"));
    auto text = notes::load_text(file);
    REQUIRE(text);
    CHECK(*text == "caf\xC3\xA9\nline 2\n");
}

TEST_CASE("binary and oversized files are refused with a reason") {
    TempDir d;
    {
        std::ofstream f(d.root / "bin", std::ios::binary);
        f.write("a\0b", 3);
    }
    auto bin = notes::load_text(d.root / "bin");
    REQUIRE_FALSE(bin);
    CHECK(bin.error().find("binary") != std::string::npos);
    {
        std::ofstream f(d.root / "big");
        f << std::string(100, 'x');
    }
    CHECK_FALSE(notes::load_text(d.root / "big", 10));
    CHECK_FALSE(notes::load_text(d.root / "missing"));
}

TEST_CASE("stats count lines and characters, not bytes") {
    auto s = notes::stats("caf\xC3\xA9\nab");
    CHECK(s.lines == 2);
    CHECK(s.characters == 7);
    CHECK(notes::stats("").lines == 0);
    CHECK(notes::stats("a\n").lines == 1);
}

TEST_CASE("find_all returns every match position") {
    CHECK(notes::find_all("abcabc", "bc") == std::vector<std::size_t>{1, 4});
    CHECK(notes::find_all("aaaa", "aa") == std::vector<std::size_t>{0, 2});   // non-overlapping
    CHECK(notes::find_all("abc", "").empty());
}

TEST_CASE("the status line says what is open, whether it changed, and the search result") {
    CHECK(notes::status_line("a.txt", {2, 7}, false, std::nullopt) == "a.txt  |  2 lines, 7 characters");
    CHECK(notes::status_line("a.txt", {2, 7}, true, 3) == "a.txt (modified)  |  2 lines, 7 characters  |  3 matches");
    CHECK(notes::status_line("", {0, 0}, false, std::nullopt) == "No file open");
}
