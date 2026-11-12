// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <iosfwd>

namespace csol::client
{
void InitializeTeddySparks();
void ResetTeddySparks();
void TeddySparksHudTime(float time);
void TeddySparksUpdateClientData();
void DrawTeddySparks();
void TeddySparksDiagnostics(std::ostream&);
}
