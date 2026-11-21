#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <embedded/widgets_app.hpp>

#include <prism/app/widget_tree.hpp>
#include <prism/raster/fonts.hpp>

#include <string>

namespace {
void press_increment(prism_examples::WidgetsModel &model)
{
    auto button = model.increment.get();
    ++button.click_count;
    model.increment.set(button);
}
} // namespace

TEST_CASE("widgets app: the default title is PRISM")
{
    CHECK(std::string(prism_examples::WidgetsModel{}.title.get().value) == "PRISM");
}

TEST_CASE("widgets app: +1 counts the clicks")
{
    prism_examples::WidgetsModel model;
    press_increment(model);
    press_increment(model);
    CHECK(model.counter.get() == 2);
}

TEST_CASE("widgets app: the view builds a snapshot")
{
    prism_examples::WidgetsModel model;
    prism::app::WidgetTree tree{model};
    prism::raster::Fonts fonts;
    tree.set_text_measurer(&fonts);
    auto snap = tree.build_snapshot(480, 272, 1);
    REQUIRE(snap != nullptr);
    CHECK_FALSE(snap->draw_lists.empty());
}
