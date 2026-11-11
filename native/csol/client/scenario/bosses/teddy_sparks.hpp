// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <iosfwd>

namespace csol::client
{
void InitializeTeddySparks();
void ResetTeddySparks();
void TeddySparksFrame();
void DrawTeddySparks();
void TeddySparksDiagnostics(std::ostream&);
}
