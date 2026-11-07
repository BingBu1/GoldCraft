// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../../engine/crc32_ieee.h"

namespace csol::client
{
using HostCrcFunction = void (__cdecl *)(std::uint32_t*, void*, int);

inline void ProcessHostCrc(std::uint32_t* state, void* bytes, int length, HostCrcFunction original)
{
    // HL10210 uses an unsigned length comparison. Unlike ReHLDS, a negative
    // input is not an empty buffer: preserve that unsupported path verbatim.
    if (length < 0) { original(state, bytes, length); return; }
    *state = goldcraft::crc32_ieee::Update(*state, bytes, length);
}

void InstallModelCrc();
}
