// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../csol-scenario-player-upgrades/fixture.hpp"
#include "../../native/csol/server/scenario_equipment.h"
namespace equipment_test
{
using namespace upgrade_test;
inline constexpr const char* BuffPath="resource/buff/BuffWeapon.cso";
inline constexpr const char* SettingsPath="maps/ZBS.json";
std::shared_ptr<const EquipmentTables> RealEquipment();
std::shared_ptr<EquipmentTables> EffectFixture();
void ResourceCases();void SelectionCases();void EffectCases();void ServiceCases();
}
