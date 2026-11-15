// With block outlines on, each block placed with vb.block() is outlined around everything it
// laid out, and labelled with its type's name where reflection gives one.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/widget_tree.hpp>

#include <array>
#include <string>
#include <variant>
#include <vector>

using namespace prism;
using namespace prism::core;
using namespace prism::render;
using namespace prism::ui;
using prism::app::WidgetTree;

namespace outline_test {
struct Strip {
    Field<Label<>> name{{.value = "Drums"}};
    Field<Checkbox> mute{{.label = "Mute"}};
    void view(WidgetTree::ViewBuilder& vb) { vb.vstack(name, mute); }
};
struct Mixer {
    Field<Label<>> title{{.value = "Mixer"}};
    Strip strip;
    void view(WidgetTree::ViewBuilder& vb) {
        vb.widget(title);
        vb.block(strip);
    }
};
struct Tall {
    std::array<Field<Label<>>, 40> rows{};
    void view(WidgetTree::ViewBuilder& vb) {
        vb.vstack([&] { for (auto& r : rows) vb.widget(r); });
    }
};
struct Scrolled {
    Tall tall;
    void view(WidgetTree::ViewBuilder& vb) { vb.scroll([&] { vb.block(tall); }); }
};
} // namespace outline_test

namespace {
std::vector<RectOutline> outlines(const SceneSnapshot& snap) {
    std::vector<RectOutline> out;
    for (auto& cmd : snap.overlay.commands)
        if (auto* r = std::get_if<RectOutline>(&cmd)) out.push_back(*r);
    return out;
}
#if __cpp_impl_reflection   // only the reflection build knows the names
bool says(const SceneSnapshot& snap, std::string_view text) {
    for (auto& cmd : snap.overlay.commands)
        if (auto* t = std::get_if<TextCmd>(&cmd); t && t->text == text) return true;
    return false;
}
#endif
Rect rect_of(const SceneSnapshot& snap, WidgetId id) {
    for (auto& [wid, r] : snap.geometry)
        if (wid == id) return r;
    return {};
}
} // namespace

TEST_CASE("off by default: no outline") {
    outline_test::Mixer m;
    WidgetTree tree{m};
    auto snap = tree.build_snapshot(400, 300, 1);
    CHECK(outlines(*snap).empty());
}

TEST_CASE("on: one outline per block, around all of its widgets and nothing else") {
    outline_test::Mixer m;
    WidgetTree tree{m};
    tree.show_blocks(true);
    auto snap = tree.build_snapshot(400, 300, 1);
    const auto boxes = outlines(*snap);
    REQUIRE(boxes.size() == 1);   // the strip; the app itself was not placed with block()

    const auto ids = tree.leaf_ids();   // title, name, mute
    REQUIRE(ids.size() == 3);
    const Rect title = rect_of(*snap, ids[0]), name = rect_of(*snap, ids[1]), mute = rect_of(*snap, ids[2]);
    const Rect box = boxes[0].rect;
    CHECK(box.contains(name.origin));
    CHECK(box.contains(Point{mute.origin.x, mute.origin.y + DY{mute.extent.h.raw() - 1.f}}));
    CHECK_FALSE(box.contains(Point{title.origin.x + DX{1.f}, title.origin.y + DY{1.f}}));
#if __cpp_impl_reflection
    CHECK(says(*snap, "Strip"));
#endif
}

TEST_CASE("a block taller than its scroll view is outlined where it shows, not past it") {
    outline_test::Scrolled m;
    WidgetTree tree{m};
    tree.show_blocks(true);
    std::unique_ptr<SceneSnapshot> snap;
    for (uint64_t v = 1; v < 3; ++v) snap = tree.build_snapshot(300, 200, v);
    const auto boxes = outlines(*snap);
    REQUIRE(boxes.size() == 1);
    CHECK(boxes[0].rect.extent.h.raw() <= 200.f);
}
