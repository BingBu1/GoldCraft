// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "model_source.hpp"
#include "sequence_safety.hpp"
#include <span>

namespace csol
{
// CSO appends stem1.seq, stem2.seq, ... until the first missing file.
std::vector<std::string> SequenceFiles(const std::string& model, const ResourceExists& exists);

// Returns an owned v10 model with the original geometry/textures intact.
// Normal gameplay uses bone units below final index198 and fixed units above,
// regardless of MDL/SEQ origin; requantize both into shared stock bone scales.
// Original sequence numbers are preserved; malformed input throws atomically.
// Already packed models are validated and returned unchanged, even if their
// original adjacent SEQ files remain present. Invalid packed data is rejected.
// Explicit AllowUnsupportedTracks preserves incompatible external RLE blocks
// as unsampleable metadata. Only consumers guarding actual selection may use it.
std::vector<std::byte> AppendSequences(std::span<const std::byte> model,
    const std::vector<std::vector<std::byte>>& sequences,
    SequenceReadPolicy policy=SequenceReadPolicy::StrictAllTracks);
}
