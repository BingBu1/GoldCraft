// SPDX-License-Identifier: GPL-3.0-or-later
#include "allocation_probe.hpp"
#include <cstdlib>
#include <new>

namespace
{
void* recorded[32768]{};
bool recording{}, overflow{};
std::size_t used{}, live{};
void Record(void* pointer)
{
    if (!recording) return;
    for (std::size_t index = 0; index < used; ++index)
        if (!recorded[index]) { recorded[index] = pointer; ++live; return; }
    if (used == sizeof(recorded) / sizeof(recorded[0])) { overflow = true; return; }
    recorded[used++] = pointer;
    ++live;
}
void Forget(void* pointer)
{
    if (!pointer || !live) return;
    for (std::size_t index = 0; index < used; ++index)
        if (recorded[index] == pointer) { recorded[index] = nullptr; --live; return; }
}
}

void* operator new(std::size_t bytes)
{
    void* pointer = std::malloc(bytes ? bytes : 1);
    if (!pointer) throw std::bad_alloc();
    Record(pointer);
    return pointer;
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* pointer) noexcept { Forget(pointer); std::free(pointer); }
void operator delete[](void* pointer) noexcept { ::operator delete(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { ::operator delete(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { ::operator delete(pointer); }

namespace ordinary_test::allocation_probe
{
void Begin()
{
    if (recording || Outstanding()) std::abort();
    used = 0;
    overflow = false;
    recording = true;
}
void End() { recording = false; }
std::size_t Outstanding(const void* except)
{
    std::size_t count{};
    for (std::size_t index = 0; index < used; ++index)
        if (recorded[index] && recorded[index] != except) ++count;
    return count;
}
bool Overflowed() { return overflow; }
}
