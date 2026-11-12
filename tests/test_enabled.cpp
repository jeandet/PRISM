// Disabled widgets: greyed out, deaf to input, skipped by focus; following a bool field.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/event_routing.hpp>
#include <prism/app/widget_tree.hpp>

using namespace prism;
using namespace prism::core;
using namespace prism::input;
using namespace prism::render;
using namespace prism::ui;
using prism::app::WidgetTree;
using prism::app::widget_detail::route_event;

namespace {
struct Form {
    Field<bool> can_save{false};
    Field<bool> editable{true};
    Field<TextField<>> name{{}};
    Field<Button> save{{"Save"}};
    Field<TextField<>> notes{{}};
    Field<Button> clear{{"Clear"}};
    void view(WidgetTree::ViewBuilder& vb) {
        vb.vstack([&] {
            vb.widget(name);
            vb.widget(save).enabled(can_save);
            vb.enabled_if(editable, [&] {
                vb.widget(notes);
                vb.hstack([&] { vb.widget(clear); });
            });
        });
    }
};

struct Fixture {
    Form model;
    WidgetTree tree{model};
    std::unique_ptr<SceneSnapshot> snap = tree.build_snapshot(500, 400, 1);
    uint64_t v = 1;
    // In placement order: name, save, notes, clear.
    std::vector<WidgetId> ids = tree.leaf_ids();
    WidgetId name() const { return ids[0]; }
    WidgetId save() const { return ids[1]; }
    WidgetId notes() const { return ids[2]; }
    WidgetId clear() const { return ids[3]; }

    void frame() { snap = tree.build_snapshot(500, 400, ++v); }
    Point centre(WidgetId id) const {
        auto r = find_widget_rect(*snap, id);
        REQUIRE(r);
        return Point{r->origin.x + DX{r->extent.w.raw() / 2}, r->origin.y + DY{r->extent.h.raw() / 2}};
    }
    void click(WidgetId id) {
        auto p = centre(id);
        route_event(tree, snap.get(), MouseButton{p, buttons::left, true});
        route_event(tree, snap.get(), MouseButton{p, buttons::left, false});
    }
    void key(int32_t k) { route_event(tree, snap.get(), KeyPress{k, 0}); }
    void type(std::string s) { route_event(tree, snap.get(), TextInput{std::move(s)}); }
    // The highest alpha among the widget's draw commands that carry a color.
    int max_alpha(WidgetId id) const {
        int best = -1;
        for (std::size_t i = 0; i < snap->geometry.size(); ++i) {
            if (snap->geometry[i].first != id) continue;
            for (const auto& cmd : snap->draw_lists[i]->commands)
                std::visit([&](const auto& c) {
                    if constexpr (requires { c.color; }) best = std::max(best, int(c.color.a));
                }, cmd);
        }
        return best;
    }
};
}

TEST_CASE("a disabled button ignores clicks; enabling it brings them back") {
    Fixture f;
    f.click(f.save());
    CHECK(f.model.save.get().click_count == 0);
    f.model.can_save.set(true);
    f.frame();
    f.click(f.save());
    CHECK(f.model.save.get().click_count == 1);
}

TEST_CASE("a disabled widget is drawn faded, and redrawn when its flag changes") {
    Fixture f;
    int faded = f.max_alpha(f.save());
    f.model.can_save.set(true);
    f.frame();
    int normal = f.max_alpha(f.save());
    CHECK(normal == 255);
    CHECK(faded > 0);
    CHECK(faded < normal / 2);
}

TEST_CASE("Tab skips a disabled widget") {
    Fixture f;
    f.click(f.name());
    REQUIRE(f.tree.focused_id() == f.name());
    f.key(keys::tab);
    CHECK(f.tree.focused_id() == f.notes());
}

TEST_CASE("a click on a disabled field does not focus it, so typing goes nowhere") {
    Fixture f;
    f.model.editable.set(false);
    f.frame();
    f.click(f.notes());
    CHECK(f.tree.focused_id() != f.notes());
    f.type("x");
    CHECK(f.model.notes.get().value.empty());
}

TEST_CASE("disabling the focused widget takes its focus away") {
    Fixture f;
    f.click(f.notes());
    REQUIRE(f.tree.focused_id() == f.notes());
    f.model.editable.set(false);
    CHECK(f.tree.focused_id() == 0);
    f.frame();
    f.type("x");
    CHECK(f.model.notes.get().value.empty());
}

TEST_CASE("enabled_if reaches widgets inside nested containers") {
    Fixture f;
    f.model.editable.set(false);
    f.frame();
    f.click(f.clear());
    CHECK(f.model.clear.get().click_count == 0);
    CHECK(f.max_alpha(f.clear()) < 128);
    f.model.editable.set(true);
    f.frame();
    f.click(f.clear());
    CHECK(f.model.clear.get().click_count == 1);
}

TEST_CASE("a disabled widget shows no hover and no special cursor") {
    Fixture f;
    f.model.editable.set(false);
    f.frame();
    route_event(f.tree, f.snap.get(), MouseMove{f.centre(f.notes())});
    CHECK(f.tree.desired_cursor() == CursorShape::Default);   // no text I-beam
    f.frame();
    CHECK(f.max_alpha(f.notes()) < 128);
    route_event(f.tree, f.snap.get(), MouseMove{f.centre(f.save())});
    route_event(f.tree, f.snap.get(), MouseMove{f.centre(f.name())});
    CHECK(f.tree.desired_cursor() == CursorShape::Text);   // an enabled field still has one
}

TEST_CASE("fading scales every color's alpha, images included") {
    DrawList dl;
    dl.filled_rect(Rect{Point{X{0}, Y{0}}, Size{Width{4}, Height{4}}}, Color::rgba(10, 20, 30, 200));
    dl.text("a", Point{X{0}, Y{0}}, 12.f, Color::rgba(1, 2, 3));
    auto img = Image::from_rgba(1, 1, {0, 0, 0, 255});
    REQUIRE(img);
    dl.image(*img, Rect{Point{X{0}, Y{0}}, Size{Width{4}, Height{4}}});
    fade(dl, 0.5f);
    CHECK(std::get<FilledRect>(dl.commands[0]).color.a == 100);
    CHECK(std::get<TextCmd>(dl.commands[1]).color.a == 128);
    CHECK(std::get<FilledRect>(dl.commands[0]).color.r == 10);   // only alpha changes
    CHECK(std::get<ImageCmd>(dl.commands[2]).alpha == 128);
}

namespace {
struct Rows {
    Field<bool> on{false};
    List<std::string> rows;
    std::string clicked = "none";
    Rows() { for (auto r : {"r0", "r1", "r2"}) rows.push_back(r); }
    void view(WidgetTree::ViewBuilder& vb) {
        vb.enabled_if(on, [&] {
            vb.list(rows, std::function<void(size_t, const std::string&)>{
                [this](size_t, const std::string& row) { clicked = row; }});
        });
    }
};

std::optional<Rect> text_rect(const SceneSnapshot& snap, std::string_view text) {
    for (std::size_t i = 0; i < snap.draw_lists.size(); ++i)
        for (auto& c : snap.draw_lists[i]->commands)
            if (auto* t = std::get_if<TextCmd>(&c); t && t->text == text) return snap.geometry[i].second;
    return std::nullopt;
}

int text_alpha(const SceneSnapshot& snap, std::string_view text) {
    for (auto& dl : snap.draw_lists)
        for (auto& c : dl->commands)
            if (auto* t = std::get_if<TextCmd>(&c); t && t->text == text) return t->color.a;
    return -1;
}
}

TEST_CASE("rows a list builds at run time follow the group's flag") {
    Rows model;
    WidgetTree tree{model};
    auto snap = tree.build_snapshot(400, 300, 1);
    auto r = text_rect(*snap, "r1");
    REQUIRE(r);
    Point p{r->origin.x + DX{5}, r->origin.y + DY{r->extent.h.raw() / 2}};
    route_event(tree, snap.get(), MouseButton{p, buttons::left, true});
    route_event(tree, snap.get(), MouseButton{p, buttons::left, false});
    CHECK(model.clicked == "none");
    CHECK(text_alpha(*snap, "r1") < 128);

    model.on.set(true);
    snap = tree.build_snapshot(400, 300, 2);
    CHECK(text_alpha(*snap, "r1") > 128);
    route_event(tree, snap.get(), MouseButton{p, buttons::left, true});
    route_event(tree, snap.get(), MouseButton{p, buttons::left, false});
    CHECK(model.clicked == "r1");
}

namespace {
struct Advanced {
    Field<Checkbox> advanced{{.checked = false, .label = "Advanced"}};
    Field<Button> tune{{"Tune"}};
    void view(WidgetTree::ViewBuilder& vb) {
        vb.vstack([&] {
            vb.widget(advanced);
            vb.enabled_if(advanced, [&] { vb.widget(tune); });
        });
    }
};
}

TEST_CASE("a checkbox can be the flag: its mark enables the group") {
    Advanced model;
    WidgetTree tree{model};
    auto snap = tree.build_snapshot(400, 300, 1);
    auto click = [&](WidgetId id) {
        auto r = find_widget_rect(*snap, id);
        REQUIRE(r);
        Point p{r->origin.x + DX{5}, r->origin.y + DY{5}};
        route_event(tree, snap.get(), MouseButton{p, buttons::left, true});
        route_event(tree, snap.get(), MouseButton{p, buttons::left, false});
    };
    auto ids = tree.leaf_ids();
    click(ids[1]);
    CHECK(model.tune.get().click_count == 0);
    click(ids[0]);   // tick "Advanced"
    REQUIRE(model.advanced.get().checked);
    snap = tree.build_snapshot(400, 300, 2);
    click(ids[1]);
    CHECK(model.tune.get().click_count == 1);
}
