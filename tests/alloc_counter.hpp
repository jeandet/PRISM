// Counts heap allocations made through operator new in a test executable. Include it in exactly
// one translation unit per test executable: it replaces the global operator new and delete.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <new>

namespace prism::test {

inline std::atomic<std::size_t> g_allocations{0};

inline std::size_t allocations() { return g_allocations.load(std::memory_order_relaxed); }

} // namespace prism::test

void* operator new(std::size_t n)
{
    prism::test::g_allocations.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(n == 0 ? 1 : n)) return p;
    throw std::bad_alloc{};
}
void* operator new(std::size_t n, std::align_val_t al)
{
    prism::test::g_allocations.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::aligned_alloc(static_cast<std::size_t>(al),
                                     (n + static_cast<std::size_t>(al) - 1) & ~(static_cast<std::size_t>(al) - 1)))
        return p;
    throw std::bad_alloc{};
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }
