// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../../scenario/core/config.hpp"
#include <array>
#include <cstdint>
#include <map>
#include <string_view>
#include <string>

namespace csol::scenario::skills
{
// One action of the HW 379F8E60 row, already resolved the way MP menuselect
// case 15 (1140EFA5) and action5 read it: wa/seq by slot, rate by seq value.
struct CharacterEmotionAction
{
    int sequence{}, weaponAnimation{};
    float frameRate{};
};
struct CharacterAction5
{
    int modelId{}, sequence{}, weaponAnimation{}, viewModel{};
    float frameRate{};
    std::string sourceName;
    // J emotion menu: radio_text titles key and 1-based slots 1..count.
    int emotionCount{};
    std::string emotionMenu;
    std::array<CharacterEmotionAction, 11> emotions{};
};
using CharacterActionTable = std::map<int, CharacterAction5>;
bool ReadCharacterActions(std::string_view csv, CharacterActionTable& output, ConfigError& error);
// Original cheer 114106E8: (int)(pow(2.0, count) - 1.0 + 512.0).
int EmotionMenuKeys(int count);
// Body of a titles.txt client message ("NAME", "{", lines, "}").
bool ReadTitleMessage(std::string_view titles, std::string_view name, std::string& body);

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
