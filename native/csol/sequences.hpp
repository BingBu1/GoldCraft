// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "model_source.hpp"
#include <span>

namespace csol
{
// CSO appends stem1.seq, stem2.seq, ... until the first missing file.
std::vector<std::string> SequenceFiles(const std::string& model, const ResourceExists& exists);

// Returns an owned v10 model with the original geometry/textures intact.
// Requantizes native and fixed-unit CSO SEQ tracks to shared bone scales.
// Original sequence numbers are preserved; malformed input throws atomically.
std::vector<std::byte> AppendSequences(std::span<const std::byte> model,
    const std::vector<std::vector<std::byte>>& sequences);
}
