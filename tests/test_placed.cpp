// The "not placed by view()" check catches a forgotten widget. It must not cry wolf over what
// the view reads without showing (a canvas's inputs), what a tab page shows, what a block
// borrows from its parent, or the plumbing between threads.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/widget_tree.hpp>

#include <algorithm>
#include <cstdio>
#include <unistd.h>
#include <string>
#include <string_view>
#include <vector>

using namespace prism;
using namespace prism::core;
using namespace prism::ui;
using prism::app::WidgetTree;

namespace {

struct Meter {
    Field<int> level{3};
    Derived<int> doubled{[this] { return level.get() * 2; }, level};
    bool counted = false;

    void view(WidgetTree::ViewBuilder& vb) {
        vb.canvas(*this).depends_on(level, doubled);
        counted = vb.placed().contains(&doubled);
    }
    void canvas(render::DrawList&, const Rect&, const WidgetNode&) {}
};

struct Pages {
    Field<Label<>> name{{.value = "a"}};
    Field<TabBar<>> tabs{};
    bool counted = false;

    void view(WidgetTree::ViewBuilder& vb) {
        vb.tabs(tabs, [&] {
            vb.tab("Only", [&](WidgetTree::ViewBuilder& page) { page.widget(name); });
        });
        counted = vb.placed().contains(&name);
    }
};

} // namespace

TEST_CASE("a value a canvas depends on counts as placed, whatever kind it is") {
    Meter m;
    WidgetTree tree{m};
    CHECK(m.counted);
}

TEST_CASE("a widget on a tab page counts as placed before the page is ever shown") {
    Pages m;
    WidgetTree tree{m};
    CHECK(m.counted);
}

#if __cpp_impl_reflection
namespace {
struct Borrower {
    Field<int>& level;              // the parent's: the parent answers for it
    Shared<int> from_worker{};      // plumbing between threads, never a widget
    Field<Label<>> forgotten{};     // a widget nobody placed: this is what the check is for
};
} // namespace

// A parent owns the value, a block draws it: the parent must not be told it forgot it.
struct Gauge {
    Field<int>& level;
    void view(WidgetTree::ViewBuilder& vb) { vb.canvas(*this).depends_on(level); }
    void canvas(render::DrawList&, const Rect&, const WidgetNode&) {}
};
struct Panel {
    Field<int> level{2};
    Gauge gauge{level};
    void view(WidgetTree::ViewBuilder& vb) { vb.block(gauge); }
};

std::string warnings_while(auto&& build) {
    std::FILE* sink = std::tmpfile();
    const int saved = dup(fileno(stderr));
    std::fflush(stderr);
    dup2(fileno(sink), fileno(stderr));
    build();
    std::fflush(stderr);
    dup2(saved, fileno(stderr));
    close(saved);
    std::rewind(sink);
    std::string out;
    for (int c; (c = std::fgetc(sink)) != EOF;) out.push_back(static_cast<char>(c));
    std::fclose(sink);
    return out;
}

TEST_CASE("a value a block shows counts as placed for the parent that owns it") {
    Panel m;
    const std::string said = warnings_while([&] { WidgetTree tree{m}; });
    CHECK(said.find("not placed") == std::string::npos);
}

TEST_CASE("the check names a forgotten widget, not a borrowed value nor thread plumbing") {
    Field<int> level{1};
    Borrower b{level};
    const std::vector<std::string_view> names = app::widget_detail::unplaced_fields(b, {});
    CHECK(names == std::vector<std::string_view>{"forgotten"});
}

namespace {
// A selection index beside the widget it selects: a value the block reads, never a widget.
struct Picker {
    [[=core::skip]] Field<int> selected{0};
    Field<Label<>> forgotten{};   // a widget nobody placed: the check is still for this
    void view(WidgetTree::ViewBuilder& vb) { vb.canvas(*this); }
    void canvas(render::DrawList&, const Rect&, const WidgetNode&) {}
};
} // namespace

TEST_CASE("a Field marked skip is a value the view does not have to place") {
    Picker m;
    const std::vector<std::string_view> names = app::widget_detail::unplaced_fields(m, {});
    CHECK(names == std::vector<std::string_view>{"forgotten"});
}

// The warning text is a debug diagnostic: check_unplaced_fields() only prints it under
// #ifndef NDEBUG (include/prism/app/widget_tree_reflect.hpp:69), and a release build both
// defines NDEBUG (b_ndebug=if-release) and leaves the debug tools off (meson.build:62-70,
// PRISM_DEBUG_TOOLS_ENABLED), so in a release build no message is printed at all and there is
// nothing to match. The direct unplaced_fields() checks above run in every build.
#if defined(PRISM_DEBUG_TOOLS_ENABLED) && !defined(NDEBUG)
TEST_CASE("the check says nothing about a skipped value, and still names a forgotten widget") {
    Picker m;
    const std::string said = warnings_while([&] { WidgetTree tree{m}; });
    CHECK(said.find("selected") == std::string::npos);
    CHECK(said.find("forgotten") != std::string::npos);
}
#endif

namespace {
// A model with no view(): its window is built from its members, and skip hides one everywhere.
struct Shown {
    Field<int> count{1};
};
struct WithHidden {
    Field<int> count{1};
    [[=core::skip]] Field<int> secret{2};
    [[=core::skip]] Derived<int> twice{[this] { return count.get() * 2; }};
    [[=core::skip]] Shared<int> feed{0};
    [[=core::skip]] Channel<int> events;
    [[=core::skip]] Shown nested;
};
} // namespace

TEST_CASE("a member marked skip gets no widget in a window built from the struct") {
    Shown plain;
    WithHidden m;
    WidgetTree plain_tree{plain};
    WidgetTree tree{m};
    CHECK(tree.leaf_count() == plain_tree.leaf_count());
}

TEST_CASE("a skipped Shared or Channel is still delivered: hidden, not cut off") {
    WithHidden m;
    WidgetTree tree{m};
    int seen = 0;
    m.feed.observe([&](const int& v) { seen = v; });
    int heard = 0;
    m.events.observe([&](const int& v) { heard = v; });
    m.feed.set(5);
    m.events.send(7);
    tree.drain_shared();
    CHECK(seen == 5);
    CHECK(heard == 7);
}
#endif
