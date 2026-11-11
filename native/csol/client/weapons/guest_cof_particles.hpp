// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "../../../guest/cof/client/aurora.hpp"

struct GoldCraftConnectionAPI;
struct ref_params_s;

namespace csol::client
{
struct GuestCofParticlePlan
{
    std::string_view script;
    // Actual already-loaded sprites/guest/cof/fog2.spr handle. Only the
    // resource-admission owner supplies it after identity verification.
    int fogSprite{};
};
void InitializeGuestCofParticles();
void InitializeGuestCofParticles(const GoldCraftConnectionAPI*);
guest::cof::client::AuroraResult PrepareGuestCofParticles(const GuestCofParticlePlan&) noexcept;
bool GuestCofParticlesReady() noexcept;
guest::cof::client::AuroraResult GuestCofParticleStatus() noexcept;
void GuestCofParticlesVideoInit() noexcept;
// Capture nextView before the native CalcRefdef can consume it. No view
// modification occurs here; the original early branch suppresses particles.
void GuestCofParticlesViewStage(const ref_params_s&) noexcept;
void DrawGuestCofParticles(bool resourceReadsAllowed) noexcept;
void ShutdownGuestCofParticles() noexcept;
}
