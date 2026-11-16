// core::Wiring (include/prism/core/wiring.hpp): connections that live as long as the block that
// made them, whichever side dies first.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/field.hpp>
#include <prism/core/wiring.hpp>

#include <memory>
#include <optional>
#include <string>

using namespace prism::core;

namespace {
// Another block's fields, watched from outside.
struct Settings {
    Field<int> volume{5};
    Field<std::string> name{"a"};
    Field<bool> mute{false};
};

struct Watcher {
    int last_volume = -1;
    int updates = 0;
    Wiring wire;

    explicit Watcher(Settings& s) {
        wire.on(s.volume, [this](const int& v) { last_volume = v; });
        wire.on_any([this] { ++updates; }, s.volume, s.name, s.mute);
    }
};
}

TEST_CASE("on() passes the new value; on_any() runs one reaction for any of its sources") {
    Settings settings;
    Watcher watcher{settings};
    settings.volume.set(7);
    CHECK(watcher.last_volume == 7);
    CHECK(watcher.updates == 1);
    settings.name.set("b");
    settings.mute.set(true);
    CHECK(watcher.updates == 3);
}

TEST_CASE("a watcher that dies first takes its connections with it") {
    Settings settings;
    auto watcher = std::make_unique<Watcher>(settings);
    watcher.reset();
    settings.volume.set(9);   // would call into the destroyed watcher if still connected
    CHECK(settings.volume.get() == 9);
}

TEST_CASE("fields that die first leave the watcher's connections inert") {
    std::optional<Settings> settings{std::in_place};
    Watcher watcher{*settings};
    settings.reset();   // the watcher's Wiring now outlives every hub it connected to
    watcher.wire.clear();
    CHECK(watcher.wire.size() == 0);
}

TEST_CASE("keep() holds a connection made elsewhere; clear() drops everything now") {
    Field<int> f{0};
    int calls = 0;
    Wiring wire;
    wire.keep(f.on_change().connect([&](const int&) { ++calls; }));
    f.set(1);
    CHECK(calls == 1);
    wire.clear();
    f.set(2);
    CHECK(calls == 1);
}

TEST_CASE("link() copies the source's value now and after each change") {
    Field<float> level{0.5f};
    Field<float> gain{0.f};
    Wiring wire;
    wire.link(level, gain);
    CHECK(gain.get() == 0.5f);
    level.set(0.8f);
    CHECK(gain.get() == 0.8f);
    wire.clear();
    level.set(0.1f);
    CHECK(gain.get() == 0.8f);
}

TEST_CASE("link() with a function converts on the way, even between types") {
    Field<int> percent{50};
    Field<std::string> label{""};
    Wiring wire;
    wire.link(percent, label, [](int p) { return std::to_string(p) + " %"; });
    CHECK(label.get() == "50 %");
    percent.set(75);
    CHECK(label.get() == "75 %");
}

TEST_CASE("two links in opposite directions settle: an unchanged value stops the echo") {
    Field<int> a{1};
    Field<int> b{2};
    int b_changes = 0;
    b.observe([&](const int&) { ++b_changes; });
    Wiring wire;
    wire.link(a, b);
    wire.link(b, a);
    a.set(7);
    CHECK(b.get() == 7);
    CHECK(b_changes == 2);   // 2 -> 1 at the first link, then 1 -> 7
}
