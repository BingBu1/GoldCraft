// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdlib>
#include <new>
namespace allocation_probe { bool active{};unsigned calls{}; }
void* operator new(std::size_t bytes)
{
    if(allocation_probe::active)++allocation_probe::calls;
    if(auto* memory=std::malloc(bytes?bytes:1))return memory;
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes){return ::operator new(bytes);}
void operator delete(void* memory)noexcept{std::free(memory);}
void operator delete[](void* memory)noexcept{std::free(memory);}
void operator delete(void* memory,std::size_t)noexcept{std::free(memory);}
void operator delete[](void* memory,std::size_t)noexcept{std::free(memory);}
