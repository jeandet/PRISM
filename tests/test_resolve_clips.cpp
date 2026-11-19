#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/render/scene_snapshot.hpp>

#include <memory>
#include <variant>

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <csignal>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

namespace {
prism::Rect R(float x, float y, float w, float h) {
    return {prism::Point{prism::X{x}, prism::Y{y}}, prism::Size{prism::Width{w}, prism::Height{h}}};
}

// A snapshot whose z_order is: list 0 pushes `outer`, list 1 pushes `inner` (a nested clip, the
// one resolve_clips must intersect), list 2 pops. The nesting comes from ANOTHER list's push:
// list 1's own ClipPush is its first command, so nothing inside list 1 nests it.
// `extra_owner`, when given, keeps a second shared_ptr to the nested-clip list — the way an
// older snapshot (layout_flatten's per-widget cache) still holding it would.
struct NestedClip {
    prism::SceneSnapshot snap;
    std::shared_ptr<prism::DrawList> extra_owner;
};

NestedClip nested_clip_snapshot(bool share_inner) {
    NestedClip out;
    auto outer = std::make_shared<prism::DrawList>();
    outer->commands.push_back(prism::ClipPush{R(0, 0, 100, 100)});
    auto inner = std::make_shared<prism::DrawList>();
    inner->commands.push_back(prism::ClipPush{R(50, 50, 100, 100)});
    inner->commands.push_back(prism::FilledRect{R(60, 60, 10, 10), prism::Color::rgba(255, 0, 0)});
    auto pop = std::make_shared<prism::DrawList>();
    pop->commands.push_back(prism::ClipPop{});
    if (share_inner) out.extra_owner = inner;   // second owner: use_count 2 once published
    out.snap.draw_lists.push_back(std::move(outer));
    out.snap.draw_lists.push_back(std::move(inner));
    out.snap.draw_lists.push_back(std::move(pop));
    out.snap.z_order = {0u, 1u, 2u};
    return out;
}
} // namespace

// resolve_nested_clips runs the clip-stack intersection within ONE list (the stack starts
// empty): a push nested in another push of the same list is intersected with it, while a
// sibling push — the stack empty again after the pops — is left alone.
TEST_CASE("resolve_nested_clips intersects a nested push inside one list and leaves a sibling push alone") {
    prism::DrawList dl;
    dl.commands.push_back(prism::ClipPush{R(0, 0, 100, 100)});    // leading push
    dl.commands.push_back(prism::ClipPush{R(50, 50, 100, 100)});   // nested in the one above
    dl.commands.push_back(prism::FilledRect{R(60, 60, 10, 10), prism::Color::rgba(255, 0, 0)});
    dl.commands.push_back(prism::ClipPop{});
    dl.commands.push_back(prism::ClipPop{});
    dl.commands.push_back(prism::ClipPush{R(200, 200, 50, 50)});   // sibling: stack empty again
    dl.commands.push_back(prism::ClipPop{});

    prism::resolve_nested_clips(dl);

    // The leading push is unchanged (nothing above it) ...
    CHECK(std::get<prism::ClipPush>(dl.commands[0]).rect == R(0, 0, 100, 100));
    // ... the nested one is intersected with it ([50,150)^2 ∩ [0,100)^2 = [50,100)^2) ...
    CHECK(std::get<prism::ClipPush>(dl.commands[1]).rect == R(50, 50, 50, 50));
    // ... and the sibling push, back at stack depth 0, is left alone.
    CHECK(std::get<prism::ClipPush>(dl.commands[5]).rect == R(200, 200, 50, 50));
}

// A list whose own nested clips were resolved at build time (resolve_nested_clips, as
// layout_flatten does before caching a list), held by a second shared_ptr the way the cache
// holds it, at the top level of a snapshot (no outer clip active): resolve_clips changes
// nothing in it — and does not abort (if it did, the test binary would die here, no fork
// needed).
TEST_CASE("resolve_clips leaves a shared, already-resolved top-level list alone (no abort, no write)") {
    auto list = std::make_shared<prism::DrawList>();
    list->commands.push_back(prism::ClipPush{R(0, 0, 100, 100)});
    list->commands.push_back(prism::ClipPush{R(50, 50, 100, 100)});   // nested in the list's own push
    list->commands.push_back(prism::FilledRect{R(60, 60, 10, 10), prism::Color::rgba(255, 0, 0)});
    list->commands.push_back(prism::ClipPop{});
    list->commands.push_back(prism::ClipPop{});
    prism::resolve_nested_clips(*list);   // as flatten_node_draws does before caching the list
    REQUIRE(std::get<prism::ClipPush>(list->commands[1]).rect == R(50, 50, 50, 50));

    std::shared_ptr<const prism::DrawList> second_owner = list;   // the cache's copy: use_count 2
    prism::SceneSnapshot snap;
    snap.draw_lists.push_back(std::move(list));
    snap.z_order = {0u};
    REQUIRE(snap.draw_lists[0].use_count() == 2);

    prism::resolve_clips(snap);   // must not abort, and must not write into the shared list

    CHECK(snap.draw_lists[0].use_count() == 2);
    CHECK(std::get<prism::ClipPush>(snap.draw_lists[0]->commands[0]).rect == R(0, 0, 100, 100));
    CHECK(std::get<prism::ClipPush>(snap.draw_lists[0]->commands[1]).rect == R(50, 50, 50, 50));
}

// Pin today's behaviour: a nested ClipPush in a list owned only by the snapshot is intersected
// with the clip active on the stack ([50,150)^2 ∩ [0,100)^2 = [50,100)^2).
TEST_CASE("resolve_clips intersects a nested ClipPush with the active clip (snapshot-owned list)") {
    auto nc = nested_clip_snapshot(false);
    REQUIRE(nc.snap.draw_lists[1].use_count() == 1);

    prism::resolve_clips(nc.snap);

    // The outer push is written by nothing (the stack was empty when it was read) ...
    CHECK(std::get<prism::ClipPush>(nc.snap.draw_lists[0]->commands[0]).rect == R(0, 0, 100, 100));
    // ... and the nested one is intersected with it.
    CHECK(std::get<prism::ClipPush>(nc.snap.draw_lists[1]->commands[0]).rect == R(50, 50, 50, 50));
}

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
// POSIX only: a forked child can be checked for SIGABRT with waitpid. The shared list's push
// is nested under the OUTER clip of the earlier list 0 (not within its own list), so the
// resolved rect ([50,100)^2) differs from the stored one ([50,150)^2): resolve_clips must
// write it, the list is shared (use_count 2), so it must report the broken invariant and
// abort — whether or not NDEBUG is defined.
TEST_CASE("resolve_clips aborts when a nested-clip list is shared (use_count > 1), with or without NDEBUG") {
    auto nc = nested_clip_snapshot(true);
    REQUIRE(nc.snap.draw_lists[1].use_count() == 2);

    const pid_t pid = fork();
    REQUIRE(pid >= 0);
    if (pid == 0) {
        prism::resolve_clips(nc.snap);   // must report the broken invariant and abort
        _exit(0);                        // reached only when the invariant is not enforced
    }
    int status = 0;
    REQUIRE(waitpid(pid, &status, 0) == pid);
    CHECK(WIFSIGNALED(status));
    CHECK(WTERMSIG(status) == SIGABRT);
}
#endif
