#pragma once

// The PRISM_PROFILE_* marks (doc/design/profiling.md): blocks, instants, counters and frame
// boundaries on the global recorder's timeline. With PRISM_PROFILING (meson option
// `profiling`, which puts the define on prism_headers_dep so PRISM and every consumer compile
// the marks the same way) each macro forwards to an inline function; without it each expands
// to nothing and never evaluates its arguments, so marks can stay in release code.
#include <cstdint>
#include <string_view>

#if defined(PRISM_PROFILING)
#include <prism/profile/recorder.hpp>
#endif

namespace prism::profile {

// Names the calling thread on the timeline (copied into its slot, cut at 16 characters).
#if defined(PRISM_PROFILING)
inline void name_thread(std::string_view name) { global().name_thread(name); }
#else
inline void name_thread(std::string_view) {}
#endif

#if defined(PRISM_PROFILING)

inline void instant(const char* name) { global().instant(name); }
inline void counter(const char* name, std::int32_t value) { global().counter(name, value); }
inline void frame() { global().frame(); }

// The RAII half of PRISM_PROFILE_SCOPE: begin in the constructor, end in the destructor.
struct Scope {
    const char* name;
    explicit Scope(const char* n) : name(n) { global().begin(n); }
    ~Scope() { global().end(name); }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};

#endif // PRISM_PROFILING

} // namespace prism::profile

#if defined(PRISM_PROFILING)
#define PRISM_PROFILE_DETAIL_CAT_(a, b) a##b
#define PRISM_PROFILE_DETAIL_CAT(a, b) PRISM_PROFILE_DETAIL_CAT_(a, b)
#define PRISM_PROFILE_SCOPE(name) \
    ::prism::profile::Scope PRISM_PROFILE_DETAIL_CAT(prism_profile_scope_, __LINE__){name}
#define PRISM_PROFILE_INSTANT(name) ::prism::profile::instant(name)
#define PRISM_PROFILE_COUNTER(name, value) ::prism::profile::counter(name, value)
#define PRISM_PROFILE_FRAME() ::prism::profile::frame()
#else
#define PRISM_PROFILE_SCOPE(name) ((void)0)
#define PRISM_PROFILE_INSTANT(name) ((void)0)
// sizeof does not evaluate its operand, so a variable used only in a mark is still "used"
// under -Wunused-variable, and the argument is never evaluated.
#define PRISM_PROFILE_COUNTER(name, value) ((void)sizeof((value)))
#define PRISM_PROFILE_FRAME() ((void)0)
#endif
