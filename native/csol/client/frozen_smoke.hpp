// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../frozen_smoke.hpp"
#include <iosfwd>

namespace csol::client
{
bool PlayFrozenSmoke(const frozen_smoke::Event&);
// Only the private CsoFrSmoke message. Unknown versions/lengths return false.
bool ConsumeFrozenSmoke(int size, const void* data);
void ResetFrozenSmoke();
void FrozenSmokeFrame();
void DrawFrozenSmoke();
void FrozenSmokeDiagnostics(std::ostream&);
}
