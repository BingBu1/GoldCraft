// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace csol::client {
void InitializeSharedRandom();
void ShutdownSharedRandom();
float SharedRandomFloat(unsigned seed, float low, float high);
inline float SharedSpread(unsigned seed) { return SharedRandomFloat(seed, -.5f, .5f); }
}
