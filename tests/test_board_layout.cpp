#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/ui/layout.hpp>
namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

namespace {
using namespace prism;

Rect R(float x, float y, float w, float h) {
    return {Point{X{x}, Y{y}}, Size{Width{w}, Height{h}}};
}
Point P(float x, float y) { return {X{x}, Y{y}}; }

const Color background = Color::rgba(10, 10, 10);
const Color under = Color::rgba(20, 20, 20);
const Color card_color = Color::rgba(30, 30, 30);
const Color over = Color::rgba(40, 40, 40);

LayoutNode leaf(WidgetId id, float w, float h, Color c = card_color) {
    LayoutNode n;
    n.kind = LayoutNode::Kind::Leaf;
    n.id = id;
    n.draws.filled_rect(R(0, 0, w, h), c);
    return n;
}

LayoutNode column(WidgetId id, std::vector<LayoutNode> children) {
    LayoutNode n;
    n.kind = LayoutNode::Kind::Column;
    n.id = id;
    n.children = std::move(children);
    return n;
}

LayoutNode board(std::vector<Point> places, std::vector<LayoutNode> cards) {
    LayoutNode n;
    n.kind = LayoutNode::Kind::Board;
    n.id = 1;
    n.board_points = std::move(places);
    n.children.push_back(leaf(2, 10, 10, background));
    for (auto& c : cards) n.children.push_back(std::move(c));
    return n;
}

const Rect board_rect = R(100, 100, 400, 300);

void lay_out(LayoutNode& b) {
    layout_measure(b, LayoutAxis::Vertical);
    layout_arrange(b, board_rect);
}

std::optional<Rect> geometry_of(const SceneSnapshot& snap, WidgetId id) {
    for (auto& [wid, r] : snap.geometry) if (wid == id) return r;
    return std::nullopt;
}

// The z position of the first draw list holding a FilledRect of colour `c`.
std::optional<std::size_t> z_of(const SceneSnapshot& snap, Color c) {
    for (std::size_t z = 0; z < snap.z_order.size(); ++z)
        for (auto& cmd : snap.draw_lists[snap.z_order[z]]->commands)
            if (auto* f = std::get_if<FilledRect>(&cmd); f && f->color == c) return z;
    return std::nullopt;
}

std::optional<std::size_t> z_of_clip(const SceneSnapshot& snap, Rect r) {
    for (std::size_t z = 0; z < snap.z_order.size(); ++z)
        for (auto& cmd : snap.draw_lists[snap.z_order[z]]->commands)
            if (auto* c = std::get_if<ClipPush>(&cmd); c && c->rect == r) return z;
    return std::nullopt;
}

int clip_depth_at(const SceneSnapshot& snap, std::size_t until_z) {
    int depth = 0;
    for (std::size_t z = 0; z < until_z; ++z)
        for (auto& cmd : snap.draw_lists[snap.z_order[z]]->commands) {
            if (std::holds_alternative<ClipPush>(cmd)) ++depth;
            if (std::holds_alternative<ClipPop>(cmd)) --depth;
        }
    return depth;
}
}

TEST_CASE("a card's natural size is its measured width across and height down a column") {
    SizeHint h{.preferred = 50, .cross = 80, .expand = true};
    CHECK(layout_detail::natural_size(h) == Size{Width{80}, Height{50}});
}

TEST_CASE("a board measures as expand") {
    auto b = board({}, {});
    layout_measure(b, LayoutAxis::Horizontal);
    CHECK(b.hint.expand);
}

TEST_CASE("a board's cards keep their natural size at their points") {
    auto b = board({P(20, 40), P(200, 10)},
                   {column(5, {leaf(6, 80, 30), leaf(7, 50, 20)}), leaf(8, 60, 25)});
    lay_out(b);
    CHECK(b.children[0].allocated == board_rect);
    CHECK(b.children[1].allocated == R(120, 140, 80, 50));
    CHECK(b.children[2].allocated == R(300, 110, 60, 25));
}

TEST_CASE("a card at a negative point or past the edge is placed there, clipped") {
    auto b = board({P(-30, -10), P(380, 290)}, {leaf(5, 60, 25), leaf(6, 60, 25)});
    lay_out(b);
    CHECK(b.children[1].allocated == R(70, 90, 60, 25));
    CHECK(b.children[2].allocated == R(480, 390, 60, 25));

    SceneSnapshot snap;
    layout_flatten(b, snap);
    auto clip = z_of_clip(snap, board_rect);
    auto card = z_of(snap, card_color);
    REQUIRE(clip);
    REQUIRE(card);
    CHECK(*clip < *card);
    CHECK(clip_depth_at(snap, *card) >= 1);
    CHECK(clip_depth_at(snap, snap.z_order.size()) == 0);
}

TEST_CASE("board_under is drawn above the background and below the cards; board_over above all") {
    auto b = board({P(20, 40)}, {leaf(5, 60, 25)});
    b.board_under = [](DrawList& dl, const BoardRectOf&) { dl.filled_rect(R(0, 0, 5, 5), under); };
    b.board_over = [](DrawList& dl, const BoardRectOf&) { dl.filled_rect(R(0, 0, 5, 5), over); };
    lay_out(b);
    SceneSnapshot snap;
    layout_flatten(b, snap);
    auto zb = z_of(snap, background), zu = z_of(snap, under), zc = z_of(snap, card_color), zo = z_of(snap, over);
    REQUIRE((zb && zu && zc && zo));
    CHECK(*zb < *zu);
    CHECK(*zu < *zc);
    CHECK(*zc < *zo);
    CHECK(clip_depth_at(snap, *zo) >= 1);
}

TEST_CASE("rect_of gives a card's descendant's rect in board coordinates") {
    auto b = board({P(20, 40)}, {column(5, {leaf(6, 80, 30), leaf(7, 50, 20)})});
    std::optional<Rect> seen, missing;
    b.board_over = [&](DrawList& dl, const BoardRectOf& rect_of) {
        seen = rect_of(7);
        missing = rect_of(99);
        dl.filled_rect(*seen, over);
    };
    lay_out(b);
    SceneSnapshot snap;
    layout_flatten(b, snap);
    CHECK(seen == R(20, 70, 80, 20));
    CHECK_FALSE(missing);
    auto zo = z_of(snap, over);
    REQUIRE(zo);
    auto& cmd = snap.draw_lists[snap.z_order[*zo]]->commands;
    auto it = std::ranges::find_if(cmd, [](auto& c) { return std::holds_alternative<FilledRect>(c); });
    CHECK(std::get<FilledRect>(*it).rect == R(120, 170, 80, 20));
    CHECK(geometry_of(snap, 7) == R(120, 170, 50, 20));
}

TEST_CASE("a card wholly off the board contributes no geometry") {
    auto b = board({P(-500, -500), P(20, 40)}, {leaf(5, 60, 25), leaf(6, 60, 25)});
    lay_out(b);
    SceneSnapshot snap;
    layout_flatten(b, snap);
    CHECK_FALSE(geometry_of(snap, 5));
    CHECK(geometry_of(snap, 6));
}
