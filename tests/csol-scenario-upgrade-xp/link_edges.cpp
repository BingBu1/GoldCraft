// SPDX-License-Identifier: GPL-3.0-or-later
// This fixture supplies in-memory tables. Resource parsing and unrelated
// damage-number messages are outside its scope; accidental use must fail.
#include "precompiled.h"
#include "../../native/csol/server/damage_numbers.h"
#include "../../native/csol/scenario/upgrade_tables.hpp"
#include <cstdlib>
#include <iostream>
namespace
{
[[noreturn]] void Unrelated(const char* edge)
{ std::cerr << "UNEXPECTED XP TEST EDGE " << edge << '\n'; std::abort(); }
}
bool CSOLWriteDamageNumber(int, edict_t*, CSOLDamageNumber)
{ Unrelated("damage-number message"); }
namespace csol::scenario
{
bool ReadCsv(std::string_view, CsvDocument&, ConfigError&)
{ Unrelated("CSV parsing"); }
bool ReadUpgradeLevelResource(std::span<const std::byte>, UpgradeLevels&, ConfigError&)
{ Unrelated("level resource"); }
bool ReadUpgradeClassResource(std::span<const std::byte>, UpgradeClasses&, ConfigError&)
{ Unrelated("class resource"); }
}
