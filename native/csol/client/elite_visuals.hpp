// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <iosfwd>
namespace csol::client
{
void InitializeEliteVisuals();
void ResetEliteVisuals();
void ShutdownEliteVisuals();
void EliteVisualsFrame();
void DrawEliteVisuals();
void EliteVisualsDiagnostics(std::ostream&);
}
