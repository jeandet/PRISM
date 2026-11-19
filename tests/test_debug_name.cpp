#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

// Debug-tools test: meson.build defines PRISM_DEBUG_TOOLS_ENABLED exactly when the build has
// the debug tools on (buildtype != 'release' unless -Dprism_debug_tools overrides), and this
// test cannot know which side it was compiled in. So it only asserts the positive direction;
// with the tools off (a release build) there is nothing to check and the debug-tools cases
// below are compiled out.
TEST_CASE("debug tools: PRISM_DEBUG_TOOLS_ENABLED is defined when the build enables them") {
#ifdef PRISM_DEBUG_TOOLS_ENABLED
    CHECK(true);
#else
    MESSAGE("PRISM_DEBUG_TOOLS_ENABLED is not defined: this build has the debug tools off "
            "(release buildtype); the debug-tools test cases are compiled out");
#endif
}

#if defined(PRISM_DEBUG_TOOLS_ENABLED) && __cpp_impl_reflection
#include <prism/app/widget_tree.hpp>
#include <prism/core/field.hpp>

namespace {
struct ReflectedOnlyModel {
    prism::core::Field<int> volume{0};
};
}

TEST_CASE("reflection-only model captures the real field name as debug_name") {
    ReflectedOnlyModel model;
    prism::app::WidgetTree tree(model);
    REQUIRE(!tree.root().children.empty());
    CHECK(tree.root().children[0].debug_name == "volume");
}
#endif
