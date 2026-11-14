// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <iosfwd>

namespace csol::client
{
bool InitializeCofMovementInput();
void ResetCofMovementInput();
void ShutdownCofMovementInput();
void CofMovementInputDiagnostics(std::ostream& out);
}
