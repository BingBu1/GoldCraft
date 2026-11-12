// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <iosfwd>
struct client_data_s;

namespace csol::client
{
void InitializeParticleEffects();
void ResetParticleEffects();
void ShutdownParticleEffects();
void CacheParticleHudTime(float time);
void CacheParticleClientData(const client_data_s& data);
void UpdateParticleEmitters();
void DrawParticleEffects();
void ParticleEffectsDiagnostics(std::ostream&);
}
