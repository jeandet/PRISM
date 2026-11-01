#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/backends/font_lookup.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;
using prism::backends::find_font;

namespace {
struct TempTree {   // <tmp>/lib64 holds the library; fonts go under <tmp>/share/prism/fonts
    fs::path root = fs::temp_directory_path() / ("prism-font-lookup-" + std::to_string(::getpid()));
    TempTree() { fs::create_directories(root / "lib64"); }
    ~TempTree() { fs::remove_all(root); }
    fs::path touch(const fs::path& rel) {
        fs::create_directories((root / rel).parent_path());
        std::ofstream(root / rel) << "x";
        return root / rel;
    }
};
}

TEST_CASE("an installed font is found relative to the library, not the executable") {
    TempTree t;
    auto font = t.touch("share/prism/fonts/Ui.ttf");
    CHECK(fs::path(find_font(nullptr, nullptr, t.root / "lib64", "share/prism/fonts/Ui.ttf")) == fs::canonical(font));
}

TEST_CASE("an explicit path wins, then the build-tree path") {
    TempTree t;
    auto installed = t.touch("share/prism/fonts/Ui.ttf");
    auto build = t.touch("src/Ui.ttf");
    CHECK(find_font("/explicit.ttf", build.c_str(), t.root / "lib64", "share/prism/fonts/Ui.ttf") == "/explicit.ttf");
    CHECK(find_font(nullptr, build.c_str(), t.root / "lib64", "share/prism/fonts/Ui.ttf") == build.string());
    CHECK(fs::path(find_font(nullptr, "/no/such/build.ttf", t.root / "lib64", "share/prism/fonts/Ui.ttf")) == fs::canonical(installed));
}

TEST_CASE("nothing found gives an empty path") {
    TempTree t;
    CHECK(find_font(nullptr, nullptr, t.root / "lib64", "share/prism/fonts/Ui.ttf").empty());
    CHECK(find_font(nullptr, nullptr, {}, "share/prism/fonts/Ui.ttf").empty());
}

TEST_CASE("the library directory of this process's PRISM backend is known") {
    CHECK(fs::is_directory(prism::backends::backend_library_dir()));
}

TEST_CASE("a multiarch library directory (lib/<triplet>) is two levels below the prefix") {
    TempTree t;
    fs::create_directories(t.root / "lib" / "x86_64-linux-gnu");
    auto font = t.touch("share/prism/fonts/Ui.ttf");
    CHECK(fs::path(find_font(nullptr, nullptr, t.root / "lib" / "x86_64-linux-gnu", "share/prism/fonts/Ui.ttf"))
          == fs::canonical(font));
}
