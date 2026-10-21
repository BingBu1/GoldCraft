// CSOL format implementation carried from this project's HLAM research.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <span>

namespace csol
{
// CSOL IDST 20/21 use the v10 layout with encoded vertices and embedded images.
// Decodes owned memory only; throws before changing malformed model data.
bool DecodeStudioModel(std::span<std::byte> data);

// Original engine's 1024-byte chunk rule, including its unaligned final chunk.
void DecodeStudioPayload(std::span<std::byte> data, int version);
}
