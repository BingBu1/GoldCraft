// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <iosfwd>

namespace csol::client {
void InitializeMonsterImpact();
// CsoMonFX owns dispatch. No weapon event or stock client function is hooked.
// Recognized malformed hit packets are consumed without drawing.
bool ConsumeMonsterImpact(int size, const void* data);
void MonsterImpactDiagnostics(std::ostream& out);
}
