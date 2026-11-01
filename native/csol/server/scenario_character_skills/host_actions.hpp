// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../../scenario/config.hpp"
#include <cstdint>
#include <map>
#include <string_view>
#include <string>

namespace csol::scenario::skills
{
struct CharacterAction5
{
    int modelId{}, sequence{}, weaponAnimation{}, viewModel{};
    float frameRate{};
    std::string sourceName;
};
using CharacterActionTable = std::map<int, CharacterAction5>;
bool ReadCharacterActions(std::string_view csv, CharacterActionTable& output, ConfigError& error);

// Original 0x618 direction mask and strict, unexpired constraint predicate.
// A skill's new constraint is added only after this start gate has succeeded.
bool ActionButtonsAllowed(std::uint32_t buttons, bool hasUnexpiredConstraint);
bool ActionFinished(float now, float deadline, bool sequenceFinished);
struct ActionConstraint
{
    bool present{};
    float expires{};
    bool Active(float now) const { return present && expires > now; }
    bool Expired(float now) const { return present && now > expires; }
};
}
