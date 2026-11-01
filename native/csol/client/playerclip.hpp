// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
struct playermove_s;
namespace csol::client
{
void InitializePlayerClips();
void ResetPlayerClips();
void ShutdownPlayerClips();
bool InjectPlayerClips(playermove_s& move);
}
