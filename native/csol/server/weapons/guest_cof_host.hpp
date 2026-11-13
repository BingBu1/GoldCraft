// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
class CBasePlayer;
class CBasePlayerWeapon;
class CBasePlayerItem;
struct edict_s;

// Optional host registration. Missing author configuration leaves the guest
// weapons disabled without changing ordinary CS/CSO map admission.
void CSOL_PrecacheGuestCof();
void CSOL_EndGuestCofMap() noexcept;
void CSOL_ShutdownGuestCof() noexcept;
void CSOL_GuestCofEntityRetiring(edict_s*) noexcept;
void CSOL_GuestCofSpawnStarted(CBasePlayer&) noexcept;
void CSOL_GuestCofSpawned(CBasePlayer&) noexcept;
void CSOL_GuestCofModelChanging(CBasePlayer&) noexcept;
void CSOL_GuestCofModelChanged(CBasePlayer&) noexcept;
void CSOL_GuestCofUserInfoChanged(CBasePlayer&) noexcept;
void CSOL_GuestCofDied(CBasePlayer&) noexcept;
void CSOL_GuestCofDisconnected(CBasePlayer&) noexcept;
void CSOL_GuestCofPreThink(CBasePlayer&) noexcept;
void CSOL_GuestCofInput(CBasePlayer&) noexcept;
void CSOL_GuestCofAcceptedJump(CBasePlayer&) noexcept;
void CSOL_GuestCofAfterMovement(CBasePlayer&) noexcept;
void CSOL_GuestCofPostThink(CBasePlayer&) noexcept;
void CSOL_GuestCofClientSnapshot(CBasePlayer&) noexcept;
bool CSOL_GuestCofCanUse(CBasePlayer&, int id) noexcept;
bool CSOL_DeliverGuestCof(CBasePlayer&,int id);
bool CSOL_IsGuestCof(const CBasePlayerWeapon&) noexcept;
const char* CSOL_GuestCofWorldModel(const CBasePlayerItem&) noexcept;
