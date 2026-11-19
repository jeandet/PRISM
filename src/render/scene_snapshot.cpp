#include <prism/render/scene_snapshot.hpp>

#include <cstdio>
#include <cstdlib>

namespace prism::render {

// See the declaration's comment in scene_snapshot.hpp: the invariant resolve_clips relies on
// when it writes a ClipPush, reported and aborted on, always on (not only under assert).
[[noreturn]] void clip_sharing_violation() {
    std::fputs("prism: resolve_clips would write a ClipPush into a DrawList shared with another "
               "snapshot (use_count > 1); a list reached with an outer clip active must be owned "
               "solely by the snapshot being resolved\n", stderr);
    std::abort();
}

} // namespace prism::render
