// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <iosfwd>
namespace csol::client
{
void InitializePickupHistory();
void AddWeaponPickup(int id);
void ResetPickupHistory();
void PickupHistoryVideoInit();
void DrawPickupHistory(int hidden,int intermission);
void PickupHistoryDiagnostics(std::ostream& out);
}
