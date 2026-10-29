// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>

// Single-threaded fixture-only observer. Entries use static storage, so the
// observer neither allocates recursively nor changes STL allocator types.
namespace ordinary_test::allocation_probe
{
void Begin();
void End();
std::size_t Outstanding(const void* except = nullptr);
bool Overflowed();
}
