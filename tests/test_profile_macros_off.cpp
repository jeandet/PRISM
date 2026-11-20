// The marks with PRISM_PROFILING off: tests/meson.build builds this file with
// -UPRISM_PROFILING (its own cpp_args, appended after the dependency flags), so even a
// -Dprofiling=enabled build compiles the macros away here.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/profile/profile.hpp>

TEST_CASE("without PRISM_PROFILING the marks compile to nothing and never evaluate their arguments")
{
    int n = 0;
    PRISM_PROFILE_COUNTER("c", ++n);
    PRISM_PROFILE_SCOPE("s");
    PRISM_PROFILE_INSTANT("i");
    PRISM_PROFILE_FRAME();
    prism::profile::name_thread("main");
    CHECK(n == 0);
}
