// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <iosfwd>
namespace csol::client
{
void InitializeBossHealth();
void ResetBossHealth();
void BossHealthFrame();
// Consume original MPToCL3 offsets before the stock CL_AdjustAngles path.
void BossImpactCreateMove(int active);
void BossHealthVideoInit();
void DrawBossHealth(int intermission);
void ShutdownBossHealth();
void BossHealthDiagnostics(std::ostream&);
}
