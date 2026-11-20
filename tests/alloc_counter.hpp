// Counts heap allocations made through operator new in a test executable. Include it in exactly
// one translation unit per test executable: it replaces the global operator new and delete.
// It counts libprism's allocations only where a program's operator new replaces the shared
// library's (ELF platforms, static builds), not inside a Windows DLL. Over-aligned new is not
// replaced (std::aligned_alloc is missing on Windows' UCRT): nothing on the frame path uses it.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <new>

namespace prism::test {

inline std::atomic<std::size_t> g_allocations{0};

inline std::size_t allocations() { return g_allocations.load(std::memory_order_relaxed); }

} // namespace prism::test

// noinline: inlined into a caller, delete's free() faces operator new's pointer and GCC flags the
// pair as mismatched (-Wmismatched-new-delete), though both come from malloc here.
[[gnu::noinline]] void* operator new(std::size_t n)
{
    prism::test::g_allocations.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(n == 0 ? 1 : n)) return p;
    throw std::bad_alloc{};
}
[[gnu::noinline]] void operator delete(void* p) noexcept { std::free(p); }
[[gnu::noinline]] void operator delete(void* p, std::size_t) noexcept { std::free(p); }
