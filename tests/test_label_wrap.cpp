// A label that wraps to its width (Label::wrap): as tall as its lines, from the first frame on.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/widget_tree.hpp>

using namespace prism;
using namespace prism::core;
using namespace prism::render;
using namespace prism::ui;
using prism::app::WidgetTree;

namespace {
struct Page {
    Field<Label<>> help{{.value = "This setting decides how often the instrument is sampled; "
                                  "a higher rate gives finer detail but larger files.",
                         .wrap = true}};
    Field<Button> ok{{"OK"}};
    void view(WidgetTree::ViewBuilder& vb) { vb.vstack(help, ok); }
};

Rect rect_of(const SceneSnapshot& s, WidgetId id) {
    for (auto& [gid, r] : s.geometry)
        if (gid == id) return r;
    FAIL("no geometry for widget");
    return {};
}

int text_lines(const SceneSnapshot& s, WidgetId id) {
    for (std::size_t i = 0; i < s.geometry.size(); ++i)
        if (s.geometry[i].first == id) {
            int n = 0;
            for (auto& c : s.draw_lists[i]->commands) n += std::holds_alternative<TextCmd>(c);
            return n;
        }
    return 0;
}
}

TEST_CASE("a wrapped label is as tall as its lines, and what follows starts below it") {
    Page p;
    WidgetTree tree{p};
    auto snap = tree.build_snapshot(200, 400, 1);   // one frame: no lag to converge
    auto ids = tree.leaf_ids();
    Rect help = rect_of(*snap, ids[0]);
    CHECK(text_lines(*snap, ids[0]) >= 3);
    CHECK(help.extent.w.raw() <= 200.f);
    CHECK(rect_of(*snap, ids[1]).origin.y.raw() >= help.origin.y.raw() + help.extent.h.raw() - 0.5f);

    auto wide = tree.build_snapshot(900, 400, 2);   // wider: fewer lines, the button moves up
    CHECK(text_lines(*wide, ids[0]) < text_lines(*snap, ids[0]));
    CHECK(rect_of(*wide, ids[1]).origin.y.raw() < rect_of(*snap, ids[1]).origin.y.raw());
}

TEST_CASE("a label that does not wrap stays on one line") {
    Field<Label<>> plain{{.value = "A rather long label that will not fit in a narrow window at all"}};
    struct One {
        Field<Label<>>& l;
        void view(WidgetTree::ViewBuilder& vb) { vb.widget(l); }
    } m{plain};
    WidgetTree tree{m};
    auto snap = tree.build_snapshot(150, 300, 1);
    CHECK(text_lines(*snap, tree.leaf_ids()[0]) == 1);
}
