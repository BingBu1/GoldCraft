#pragma once
#include <cstdint>
#include <iosfwd>
struct metahook_api_s;
namespace goldcraft::host_text {
struct Context {std::uint64_t world=0,menu=0;};
using Query=Context(*)();
using Commit=void(*)(std::uint32_t,unsigned);
bool install(metahook_api_s*,Query,Commit);
void shutdown();
bool block_key(int glfw);
unsigned modifiers(unsigned fallback);
void diagnostics(std::ostream&);
}
