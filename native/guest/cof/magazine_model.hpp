// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "magazine_actions.hpp"
#include <cstddef>
#include <span>

namespace guest::cof
{
enum class MagazineModelResult : unsigned char
{
    Prepared, InvalidInput, InvalidModel, MissingSequence, InvalidTiming
};

// Cold preparation from the complete, validated resource bytes. The resource
// owner still authenticates the model/content generation before using these
// times. requiredCount is a prefix: P345 requires34, Glock43. It is not the
// number of descriptors to invent when a model has fewer sequences.
// Keeps the previous output on failure; never retains pointers into the model.
MagazineModelResult PrepareMagazineSequenceTimes(std::span<const std::byte> model,
    unsigned requiredCount, MagazineSequenceTimes& output) noexcept;
}
