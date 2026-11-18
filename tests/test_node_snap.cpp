#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/nodes/snap.hpp>

#include <algorithm>
#include <vector>

using namespace prism::core;
using namespace prism::nodes;

namespace {

constexpr float reach = 6.f;

Rect rect(float x, float y, float w, float h) { return Rect{Point{X{x}, Y{y}}, Size{Width{w}, Height{h}}}; }
Point pt(float x, float y) { return Point{X{x}, Y{y}}; }
Size size(float w, float h) { return Size{Width{w}, Height{h}}; }

bool has_guide(const Snapped& s, Guide g) { return std::ranges::find(s.guides, g) != s.guides.end(); }

} // namespace

TEST_CASE("with no neighbour the card stays where the pointer puts it") {
    const auto s = snap(pt(100, 50), size(160, 90), {}, {}, reach);
    CHECK(s.at == pt(100, 50));
    CHECK(s.guides.empty());
}

TEST_CASE("left edges 4 apart snap together, and the guide spans both cards vertically") {
    const std::vector<Rect> others{rect(104, 400, 160, 90)};
    const auto s = snap(pt(100, 50), size(160, 90), others, {}, reach);

    CHECK(s.at == pt(104, 50));
    REQUIRE(s.guides.size() == 1);
    CHECK(s.guides.front() == Guide{pt(104, 50), pt(104, 490)});
}

TEST_CASE("edges 7 apart do not snap, with a reach of 6") {
    const std::vector<Rect> others{rect(107, 400, 160, 90)};
    const auto s = snap(pt(100, 50), size(160, 90), others, {}, reach);

    CHECK(s.at == pt(100, 50));
    CHECK(s.guides.empty());
}

TEST_CASE("a centre lines up with another card's centre") {
    const std::vector<Rect> others{rect(40, 300, 280, 90)};
    const auto s = snap(pt(100, 50), size(160, 90), others, {}, reach);

    CHECK(s.at == pt(100, 50));
    REQUIRE(s.guides.size() == 1);
    CHECK(s.guides.front() == Guide{pt(180, 50), pt(180, 390)});
}

TEST_CASE("two bottoms line up even when the cards differ in height") {
    const std::vector<Rect> others{rect(400, 0, 160, 135)};
    const auto s = snap(pt(100, 50), size(160, 90), others, {}, reach);

    CHECK(s.at == pt(100, 45));
    REQUIRE(s.guides.size() == 1);
    CHECK(s.guides.front() == Guide{pt(100, 135), pt(560, 135)});
}

TEST_CASE("x and y snap on their own, one guide each") {
    const std::vector<Rect> others{rect(104, 400, 160, 90), rect(600, 53, 160, 90)};
    const auto s = snap(pt(100, 50), size(160, 90), others, {}, reach);

    CHECK(s.at == pt(104, 53));
    REQUIRE(s.guides.size() == 2);
    CHECK(has_guide(s, Guide{pt(104, 53), pt(104, 490)}));
    CHECK(has_guide(s, Guide{pt(104, 53), pt(760, 53)}));
}

TEST_CASE("a wire straightening beats an alignment at the same distance") {
    const std::vector<Rect> others{rect(400, 53, 160, 90)};
    const std::vector<LevelWith> wires{LevelWith{DY{30}, Y{83}, X{500}}};
    const auto s = snap(pt(100, 50), size(160, 90), others, wires, reach);

    CHECK(s.at == pt(100, 53));
    REQUIRE(s.guides.size() == 1);
    CHECK(s.guides.front() == Guide{pt(100, 83), pt(500, 83)});
}

TEST_CASE("a wire straightens a card with no other card in sight") {
    const std::vector<LevelWith> wires{LevelWith{DY{30}, Y{83}, X{40}}};
    const auto s = snap(pt(100, 50), size(160, 90), {}, wires, reach);

    CHECK(s.at == pt(100, 53));
    REQUIRE(s.guides.size() == 1);
    CHECK(s.guides.front() == Guide{pt(40, 83), pt(260, 83)});
}

TEST_CASE("an alignment nearer than a wire wins") {
    const std::vector<Rect> others{rect(400, 52, 160, 90)};
    const std::vector<LevelWith> wires{LevelWith{DY{30}, Y{85}, X{500}}};
    const auto s = snap(pt(100, 50), size(160, 90), others, wires, reach);

    // the wire would move it 5, the other card's top 2
    CHECK(s.at == pt(100, 52));
    REQUIRE(s.guides.size() == 1);
    CHECK(s.guides.front() == Guide{pt(100, 52), pt(560, 52)});
}

TEST_CASE("a candidate exactly at the reach snaps") {
    const std::vector<Rect> others{rect(106, 400, 160, 90)};
    const auto s = snap(pt(100, 50), size(160, 90), others, {}, reach);

    CHECK(s.at == pt(106, 50));
    REQUIRE(s.guides.size() == 1);
    CHECK(s.guides.front() == Guide{pt(106, 50), pt(106, 490)});
}

TEST_CASE("both axes can snap to the same card") {
    const std::vector<Rect> others{rect(104, 53, 300, 200)};
    const auto s = snap(pt(100, 50), size(160, 90), others, {}, reach);

    CHECK(s.at == pt(104, 53));
    REQUIRE(s.guides.size() == 2);
    CHECK(has_guide(s, Guide{pt(104, 53), pt(104, 253)}));
    CHECK(has_guide(s, Guide{pt(104, 53), pt(404, 53)}));
}

TEST_CASE("the nearest of two candidates wins") {
    const std::vector<Rect> others{rect(103, 400, 160, 90), rect(105, 700, 160, 90)};
    const auto s = snap(pt(100, 50), size(160, 90), others, {}, reach);

    CHECK(s.at == pt(103, 50));
    REQUIRE(s.guides.size() == 1);
    CHECK(s.guides.front() == Guide{pt(103, 50), pt(103, 490)});
}

TEST_CASE("snapping depends only on where the pointer is, not on what snapped before") {
    const std::vector<Rect> others{rect(104, 400, 160, 90)};

    const auto first = snap(pt(100, 50), size(160, 90), others, {}, reach);
    CHECK(first.at == pt(104, 50));

    const auto dragged_past = snap(pt(114, 50), size(160, 90), others, {}, reach);
    CHECK(dragged_past.at == pt(114, 50));
    CHECK(dragged_past.guides.empty());

    const auto back_again = snap(pt(100, 50), size(160, 90), others, {}, reach);
    CHECK(back_again == first);
}
