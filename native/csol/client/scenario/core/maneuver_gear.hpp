// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <iosfwd>

namespace csol::client::maneuver
{
void Initialize();
void Reset();
void Frame();
void Diagnostics(std::ostream& out);
void WriteSnapshot(std::ostream& out);
}
