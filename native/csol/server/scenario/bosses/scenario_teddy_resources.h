// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include "../../../cso_tent.hpp"

// MP10996D40 consumes the first model; MP109984F0 precaches the next
// model/sprite into 11EF7B48/11EF7B50. ef_tentacle is already in the shared
// CTentacle precache. Keep concrete_gibs distinct from concretegibs.
inline constexpr std::array CSOLTeddyGroundModels{
    "models/concrete_gibs.mdl", "models/ef_bear_attack.mdl", "sprites/fire3.spr"};
// HW3785032B -> 3AC0B0BC, consumed by TE3's sound selector 2.
inline constexpr const char* CSOLTeddyGroundSound = csol::cso_tent::MooncakeSoundPath;
// MP10997FD0 event0 ->11493800 world emitters, in this exact order.
inline constexpr std::array CSOLTeddySpawnSprites{"sprites/spark2.spr", "sprites/spark3.spr"};
// MP10936D30 Push Enter ->11493570: three entity-owned Sliding emissions.
inline constexpr const char* CSOLTeddySlidingSprite = "sprites/ef_bat.spr";
// MP10997A90 event1 ->114931C0: reliable MPToCL/88 ring smoke.
inline constexpr const char* CSOLTeddyBlackholeSmokeSprite = "sprites/smoke_ia.spr";
// MP10995D20 ->10996880 ->11493000: both Meteor hand attachments.
inline constexpr const char* CSOLTeddyMeteorSprite = "sprites/burn5.spr";
