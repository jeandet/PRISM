// Settings: a struct of fields to and from a TOML file, saved after each change.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/settings.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace prism;
using namespace prism::core;
using namespace prism::ui;
namespace fs = std::filesystem;

// Named, not anonymous: Glaze's reflection under clang needs types with linkage.
namespace settings_test {
enum class Look { System, Light, Dark };

struct Editor {
    Field<int> font_size{12};
    Field<std::string> font{"JetBrains Mono"};
};

struct Prefs {
    Field<Slider<>> volume{{.value = 0.6, .min = 0.0, .max = 2.0}};
    Field<Checkbox> notify{{.checked = true, .label = "Send notifications"}};
    Field<Look> theme{Look::Dark};
    Field<TextField<>> name{{.value = "Ada", .placeholder = "Your name"}};
    Editor editor;
};
}  // namespace settings_test

using namespace settings_test;

namespace {

std::string read_file(const fs::path& p) {
    std::ifstream in(p);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

struct TempDir {
    fs::path path = fs::temp_directory_path() / ("prism-settings-" + std::to_string(::getpid()) + "-" +
                                                 std::to_string(reinterpret_cast<std::uintptr_t>(this)));
    ~TempDir() { fs::remove_all(path); }
};
}

TEST_CASE("a settings struct writes as TOML: widgets as their value, enums by name, structs as tables") {
    Prefs p;
    auto toml = app::to_toml(p);
    CHECK(toml.find("volume = 0.6") != std::string::npos);
    CHECK(toml.find("notify = true") != std::string::npos);
    CHECK(toml.find("theme = \"Dark\"") != std::string::npos);
    CHECK(toml.find("name = \"Ada\"") != std::string::npos);
    CHECK(toml.find("[editor]") != std::string::npos);
    CHECK(toml.find("font_size = 12") != std::string::npos);
    CHECK(toml.find("min") == std::string::npos);           // only the value is a setting
    CHECK(toml.find("placeholder") == std::string::npos);
}

TEST_CASE("reading sets the fields, so the widgets and observers follow") {
    Prefs p;
    int notified = 0;
    auto c = p.volume.on_change().connect([&](const Slider<>&) { ++notified; });
    auto r = app::from_toml(p, "volume = 1.5\ntheme = \"Light\"\nnotify = false\n[editor]\nfont_size = 15\n");
    REQUIRE(r);
    CHECK(p.volume.get().value == 1.5);
    CHECK(p.volume.get().max == 2.0);   // the rest of the widget stays
    CHECK(p.theme.get() == Look::Light);
    CHECK_FALSE(p.notify.get().checked);
    CHECK(p.notify.get().label == "Send notifications");
    CHECK(p.editor.font_size.get() == 15);
    CHECK(p.editor.font.get() == "JetBrains Mono");   // missing: unchanged
    CHECK(notified == 1);
}

TEST_CASE("unknown keys and unknown enum names are skipped, not errors") {
    Prefs p;
    auto r = app::from_toml(p, "removed_in_v2 = 3\ntheme = \"Sepia\"\nvolume = 0.25\n");
    REQUIRE(r);
    CHECK(p.theme.get() == Look::Dark);
    CHECK(p.volume.get().value == 0.25);
}

TEST_CASE("a broken file is reported") {
    Prefs p;
    auto r = app::from_toml(p, "volume = = =\n");
    REQUIRE_FALSE(r);
    CHECK_FALSE(r.error().empty());
}

TEST_CASE("save then load round-trips, creating the folder; a missing file loads as nothing") {
    TempDir dir;
    auto file = dir.path / "app" / "settings.toml";
    Prefs fresh;
    REQUIRE(app::load_settings(fresh, file));   // first run: no file yet
    CHECK(fresh.volume.get().value == 0.6);

    Prefs a;
    a.volume.set({.value = 1.25, .min = 0.0, .max = 2.0});
    a.editor.font.set("Fira Code");
    REQUIRE(app::save_settings(a, file));
    Prefs b;
    REQUIRE(app::load_settings(b, file));
    CHECK(b.volume.get().value == 1.25);
    CHECK(b.editor.font.get() == "Fira Code");
}

TEST_CASE("a settings file loads at start and saves what changed, at the latest when it closes") {
    TempDir dir;
    auto file = dir.path / "settings.toml";
    {
        Prefs p;
        p.editor.font_size.set(20);
        app::save_settings(p, file).value();
    }
    {
        Prefs p;
        app::SettingsFile settings{p, file};
        CHECK(p.editor.font_size.get() == 20);   // loaded
        p.theme.set(Look::System);
    }   // closing writes the last change
    CHECK(read_file(file).find("theme = \"System\"") != std::string::npos);
}
