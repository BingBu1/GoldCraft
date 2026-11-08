// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Count real validator entries without replacing its implementation.
#define ReadSequenceSafety UncountedReadSequenceSafety
#include "../../native/csol/sequence_safety.hpp"
#undef ReadSequenceSafety
namespace csol {
inline unsigned validationReads{};
inline SequenceSafetyStatus ReadSequenceSafety(std::span<const std::byte> bytes,SequenceSafetyView& output,
    SequenceReadPolicy policy=SequenceReadPolicy::StrictAllTracks)
{++validationReads;return UncountedReadSequenceSafety(bytes,output,policy);}
}
