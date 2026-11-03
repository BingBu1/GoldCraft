// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <iosfwd>

namespace csol::client
{
void InitializeBalrog11Flame();
void ResetBalrog11Flame();
void Balrog11FlameFrame();
void DrawBalrog11Flame();
void Balrog11FlameDiagnostics(std::ostream& out);
}
