// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Windows.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#define EXT_FUNC
#include <interface.h>

// Explicit engine boundaries, not binary-layout substitutes. The test runs
// the real PF body with its globals, network sink and client population here.
inline constexpr std::size_t MAX_LIGHTSTYLE_SIZE = 64;
inline constexpr int MAX_LIGHTSTYLES = 64;
enum server_state_t { ss_dead, ss_loading, ss_active };
struct sizebuf_t { unsigned size{}; std::array<unsigned char, 256> bytes{}; };
struct client_t { bool active{}, spawned{}, fakeclient{}; struct { sizebuf_t message; } netchan; };
struct ServerBoundary { server_state_t state{}; const char* lightstyles[MAX_LIGHTSTYLES]{}; };
struct StaticBoundary { int spawncount{}, maxclients{}; client_t* clients{}; };
struct RehldsBoundary { char lightstyleBuffers[MAX_LIGHTSTYLES][MAX_LIGHTSTYLE_SIZE]{}; };
extern ServerBoundary g_psv;
extern StaticBoundary g_psvs;
extern RehldsBoundary g_rehlds_sv;
inline constexpr int svc_lightstyle = 12;
template<std::size_t N> void Q_strlcpy(char (&to)[N], const char* from)
{
    unsigned n = 0;
    while (n + 1 < N && from[n]) ++n;
    std::memmove(to, from, n); to[n] = 0;
}
inline void MSG_WriteChar(sizebuf_t* sink, int value)
{
    if (sink->size >= sink->bytes.size()) std::abort();
    sink->bytes[sink->size++] = static_cast<unsigned char>(value);
}
inline void MSG_WriteString(sizebuf_t* sink, const char* value)
{ do { MSG_WriteChar(sink, *value); } while (*value++); }
void PF_lightstyle_I(int style, const char* val);
