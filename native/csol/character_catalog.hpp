// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace csol {
struct Character {
    std::uint16_t id{};
    std::uint8_t team{}, skin{};
    bool male{}, allowPlayerStyle{};
    int handStyle{};
    // Original model IDs and ZBS_class keys are separate namespaces.
    // -1 means this catalog version has no verified scenario mapping.
    int scenarioClass{-1};
    std::string name, model, hand, portrait;
    std::string ModelPath() const { return "models/player/"+model+"/"+model+".mdl"; }
};
std::vector<Character> ParseCharacters(std::string_view json);
const Character* FindCharacter(const std::vector<Character>& catalog, unsigned id);
bool CharacterTeamAllowed(const Character& character, int team, bool zombieHuman);
}
