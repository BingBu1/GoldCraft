// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>

// Engine-independent CTentacle resource lists, shared by the server session's
// required-resource check (../core/scenario_session.cpp), the entity
// (scenario_teddy_tentacle.cpp) and the read-only map resource audit.

// Indices used by the entity: sign, candy (type 3), ground effect, debris
// (concretegibs is the global 11551E20 precache), generic tentacle (type 0).
inline constexpr std::array CSOLTeddyTentacleModels{
    "models/ef_tentacle_sign.mdl", "models/bear_candy.mdl",
    "models/ef_tentacle.mdl", "models/concretegibs.mdl", "models/tentacle.mdl"};
inline constexpr char CSOLTeddyTentacleSound[] = "zombi/bearboss/candy_attack.wav";
inline constexpr char CSOLTentaclePierceSound[] = "zombi/dione/zbs_tentacle_pierce.wav";
// CTentacle::Precache 113E7C10, in its original order (all variants).
inline constexpr std::array CSOLTentaclePrecacheModels{
    "models/tentacle.mdl", "models/ef_tentacle_sign.mdl", "models/ef_tentacle.mdl",
    "models/tentacle2.mdl", "models/tentacle3.mdl", "models/bear_candy.mdl",
    "models/rex_tentacle.mdl", "models/ZombieGiant/ef_dione_tentacle.mdl",
    "models/ZombieGiant/dione_tentacle.mdl"};
inline constexpr std::array CSOLTentacleSounds{CSOLTeddyTentacleSound, CSOLTentaclePierceSound};
