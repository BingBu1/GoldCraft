// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Rename only the real reader definition, then count calls to it. The exact
// production validator still executes, including every RLE and metadata check.
#define ReadSequenceSafety UncountedReadSequenceSafety
#include "sequence_safety.hpp"
#undef ReadSequenceSafety
namespace safety_counter { inline unsigned reads{}; }
namespace csol
{
inline SequenceSafetyStatus ReadSequenceSafety(std::span<const std::byte> bytes,
    SequenceSafetyView& output,SequenceReadPolicy policy=SequenceReadPolicy::StrictAllTracks)
{
    ++safety_counter::reads;
    return UncountedReadSequenceSafety(bytes,output,policy);
}
}
