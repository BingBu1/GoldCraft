// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <iosfwd>

namespace csol::client
{
void InitializeStandaloneMovementInput();
bool StandaloneSprintAvailable() noexcept;
void ResetStandaloneMovementInput();
void ShutdownStandaloneMovementInput();
void StandaloneMovementInputDiagnostics(std::ostream& out);
}
