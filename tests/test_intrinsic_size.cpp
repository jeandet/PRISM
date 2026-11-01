#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/widget_tree.hpp>
#include <prism/core/field.hpp>

#include <map>
namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

namespace {
struct TenPerChar final : prism::TextMeasurer {
    float width(std::string_view s, float, prism::FontRole) const override { return 10.f * static_cast<float>(s.size()); }
};

struct Toolbar {
    prism::Field<prism::TextField<>> find{{.placeholder = "Find"}};
    prism::Field<prism::Button> ok{{"OK"}};
    prism::Field<prism::Button> cancel{{"Cancel"}};
    void view(prism::WidgetTree::ViewBuilder& vb) { vb.hstack(find, ok, cancel); }
};

std::map<prism::WidgetId, float> widths(prism::WidgetTree& tree, float w) {
    auto snap = tree.build_snapshot(w, 100.f, 1);
    std::map<prism::WidgetId, float> out;
    for (auto& [id, rect] : snap->geometry) out[id] = rect.extent.w.raw();
    return out;
}
}

TEST_CASE("buttons take their text width; the text field takes the rest") {
    Toolbar model;
    prism::WidgetTree tree(model);
    TenPerChar m;
    tree.set_text_measurer(&m);
    auto ids = tree.leaf_ids();
    auto w = widths(tree, 600.f);
    float ok = w[ids[1]], cancel = w[ids[2]], find = w[ids[0]];
    CHECK(cancel == doctest::Approx(16.f + 60.f));     // padding + "Cancel"
    CHECK(ok == doctest::Approx(48.f));                // "OK" is short: minimum button width
    CHECK(find > 400.f);                               // the field expands into the rest
    CHECK(find + ok + cancel == doctest::Approx(600.f).epsilon(0.02));
}

TEST_CASE("without a measurer the estimate is used, with the same rules") {
    Toolbar model;
    prism::WidgetTree tree(model);
    auto ids = tree.leaf_ids();
    auto w = widths(tree, 600.f);
    CHECK(w[ids[2]] == doctest::Approx(16.f + 6.f * 0.6f * 14.f));
    CHECK(w[ids[0]] > w[ids[2]]);
}
