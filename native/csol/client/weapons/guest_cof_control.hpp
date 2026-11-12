// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
struct GoldCraftConnectionAPI;
namespace csol::client
{
bool GuestCofControlReady() noexcept;
enum class GuestCofControlResult : unsigned char
{
    Inactive, InvalidPacket, Cancelled, BodyApplied, NoViewModel, MissingParticleBackend,
    ShotStateApplied, UnsupportedCamera, MissingCameraBackend,
    // Original 1055959C has no host producer/consumer yet. This is deliberately
    // not a claim that the complete original WpnDeploy was consumed.
    LocalParticlesKilledForwardDisplacementMissing,
    DeployApplied,MissingForwardDisplacementBackend
};
// Explicit host capability; does not name the unclosed original 1018D27C gate.
enum class GuestCofControlCamera : unsigned char { OrdinaryHost, Unsupported };
void SetGuestCofControlCamera(GuestCofControlCamera) noexcept;
void InitializeGuestCofControl();
void InitializeGuestCofControl(const GoldCraftConnectionAPI*);
void ShutdownGuestCofControl() noexcept;
GuestCofControlResult GuestCofLastControl() noexcept;
}
