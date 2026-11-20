#pragma once

// The PRISM_PROFILE_* marks (doc/design/profiling.md): blocks, instants, counters and frame
// boundaries on the global recorder's timeline. With PRISM_PROFILING (meson option
// `profiling`, which puts the define on prism_headers_dep so PRISM and every consumer compile
// the marks the same way) each macro first builds its call site -- a static constexpr Site
// (name, function, file, line, from std::source_location at the macro's expansion) -- and then
// forwards the mark, whose event points at that site; without PRISM_PROFILING each macro
// expands to nothing and never evaluates its arguments, so marks can stay in release code.
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

inline void instant(const Site& site) { global().instant(site); }
inline void counter(const Site& site, std::int32_t value) { global().counter(site, value); }
void instant(const Site&&) = delete; // a temporary site would dangle in the ring
void counter(const Site&&, std::int32_t) = delete;
inline void frame() { global().frame(); }

// The RAII half of PRISM_PROFILE_SCOPE: begin in the constructor, end in the destructor.
struct Scope {
    const Site* site;
    explicit Scope(const Site& s) : site(&s) { global().begin(s); }
    explicit Scope(const Site&&) = delete; // a temporary site would dangle in the ring
    ~Scope() { global().end(*site); }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};

#endif // PRISM_PROFILING

} // namespace prism::profile

#if defined(PRISM_PROFILING)
#define PRISM_PROFILE_DETAIL_CAT_(a, b) a##b
#define PRISM_PROFILE_DETAIL_CAT(a, b) PRISM_PROFILE_DETAIL_CAT_(a, b)
// Each mark builds its call site first: a static constexpr Site at the macro's expansion.
// make_site is consteval and takes std::source_location::current() by default, so the site
// carries this line's function, file and line -- and a name that is not a constant expression
// does not compile.
// __COUNTER__, not __LINE__: two marks on one line (or one macro expanding to two) get
// distinct names.
#define PRISM_PROFILE_SCOPE(name) PRISM_PROFILE_DETAIL_SCOPE(name, __COUNTER__)
#define PRISM_PROFILE_DETAIL_SCOPE(name, id) \
    static constexpr ::prism::profile::Site PRISM_PROFILE_DETAIL_CAT(prism_site_, id) = \
        ::prism::profile::make_site(name); \
    ::prism::profile::Scope PRISM_PROFILE_DETAIL_CAT(prism_profile_scope_, id){ \
        PRISM_PROFILE_DETAIL_CAT(prism_site_, id)}
// do/while: a statement of its own, so `if (x) PRISM_PROFILE_INSTANT("i");` compiles on and off.
#define PRISM_PROFILE_INSTANT(name) \
    do { \
        static constexpr ::prism::profile::Site prism_site = ::prism::profile::make_site(name); \
        ::prism::profile::instant(prism_site); \
    } while (0)
#define PRISM_PROFILE_COUNTER(name, value) \
    do { \
        static constexpr ::prism::profile::Site prism_site = ::prism::profile::make_site(name); \
        ::prism::profile::counter(prism_site, (value)); \
    } while (0)
#define PRISM_PROFILE_FRAME() ::prism::profile::frame()
#else
#define PRISM_PROFILE_SCOPE(name) ((void)0)
#define PRISM_PROFILE_INSTANT(name) ((void)0)
// sizeof does not evaluate its operand, so a variable used only in a mark is still "used"
// under -Wunused-variable, and the argument is never evaluated.
#define PRISM_PROFILE_COUNTER(name, value) ((void)sizeof((value)))
#define PRISM_PROFILE_FRAME() ((void)0)
#endif
