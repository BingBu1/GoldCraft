#pragma once

// Standalone test executable only. Covers every replaceable C++ allocation form
// used by the matched Windows toolchain; no production allocator is changed.
#include <cstddef>
#include <cstdlib>
#include <malloc.h>
#include <new>

namespace allocations {
inline std::size_t count = 0;
inline std::size_t remaining = 0;
inline bool fail = false;
inline bool injected = false;
inline void attempt() {
    ++count;
    if (fail) {
        if (!remaining) { injected = true; throw std::bad_alloc(); }
        --remaining;
    }
}
class Fault {
public:
    explicit Fault(std::size_t successful) {
        if (fail) std::abort();
        remaining = successful; injected = false; fail = true;
    }
    ~Fault() { fail = false; }
    Fault(const Fault&) = delete;
    Fault& operator=(const Fault&) = delete;
};
}

void* operator new(std::size_t size) {
    allocations::attempt();
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    try { return ::operator new(size); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    try { return ::operator new[](size); } catch (...) { return nullptr; }
}
void* operator new(std::size_t size, std::align_val_t alignment) {
    allocations::attempt();
    if (void* p = _aligned_malloc(size ? size : 1, static_cast<std::size_t>(alignment))) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size, std::align_val_t alignment) { return ::operator new(size, alignment); }
void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    try { return ::operator new(size, alignment); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    try { return ::operator new[](size, alignment); } catch (...) { return nullptr; }
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete(void* p, std::align_val_t) noexcept { _aligned_free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { _aligned_free(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { _aligned_free(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { _aligned_free(p); }
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept { _aligned_free(p); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept { _aligned_free(p); }
