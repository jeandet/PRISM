// A link (ui::Link): text that opens its URL when clicked, through the backend's opener.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/event_routing.hpp>
#include <prism/app/widget_tree.hpp>
#include <prism/ui/url_opener.hpp>

#include <string>
#include <vector>

using namespace prism;
using namespace prism::core;
using namespace prism::input;
using namespace prism::render;
using namespace prism::ui;
using prism::app::WidgetTree;
using prism::app::widget_detail::route_event;

namespace {
struct FakeOpener final : UrlOpener {
    mutable std::vector<std::string> opened;
    void open(std::string url) const override { opened.push_back(std::move(url)); }
};

struct About {
    Field<Link> docs{{.text = "Read the docs", .url = "https://example.org/docs"}};
    Field<Link> more{{.text = "More…"}};   // no URL: the app handles the click
    void view(WidgetTree::ViewBuilder& vb) { vb.vstack(docs, more); }
};

struct Fixture {
    About m;
    FakeOpener opener;
    WidgetTree tree{m};
    std::unique_ptr<SceneSnapshot> snap;
    Fixture() {
        tree.set_url_opener(&opener);
        snap = tree.build_snapshot(300, 200, 1);
    }
    void click(WidgetId id) {
        Point c{};
        for (auto& [gid, r] : snap->geometry)
            if (gid == id) c = r.center();
        route_event(tree, snap.get(), MouseButton{c, buttons::left, true});
        route_event(tree, snap.get(), MouseButton{c, buttons::left, false});
    }
};
}

TEST_CASE("clicking a link opens its URL and counts the click") {
    Fixture f;
    f.click(f.tree.leaf_ids()[0]);
    CHECK(f.opener.opened == std::vector<std::string>{"https://example.org/docs"});
    CHECK(f.m.docs.get().click_count == 1);
}

TEST_CASE("a link without a URL only counts, for the app to act on") {
    Fixture f;
    f.click(f.tree.leaf_ids()[1]);
    CHECK(f.opener.opened.empty());
    CHECK(f.m.more.get().click_count == 1);
}

TEST_CASE("Enter on a focused link opens it too") {
    Fixture f;
    f.tree.set_focused(f.tree.leaf_ids()[0]);
    route_event(f.tree, f.snap.get(), KeyPress{keys::enter, 0});
    CHECK(f.opener.opened.size() == 1);
}

TEST_CASE("a link is underlined text, with a pointing hand over it") {
    Fixture f;
    bool text = false, underline = false;
    for (auto& c : f.snap->draw_lists.front()->commands) {
        if (auto* t = std::get_if<TextCmd>(&c)) text = t->text == "Read the docs";
        if (std::holds_alternative<Line>(c)) underline = true;
    }
    CHECK(text);
    CHECK(underline);
    f.tree.update_hover(f.tree.leaf_ids()[0]);
    CHECK(f.tree.desired_cursor() == CursorShape::Pointer);
}
