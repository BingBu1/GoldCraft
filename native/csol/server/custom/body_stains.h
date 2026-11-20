// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

struct delta_s;
struct edict_s;
struct entvars_s;
struct TraceResult;
class CBaseEntity;
class CCSOLScenarioMonster;
class CBasePlayer;
class CBasePlayerItem;
class CWeaponBox;
class Vector;

void CSOL_RegisterBodyStainDelta(bool player, delta_s* schema) noexcept;
bool CSOL_BodyStainDeltaReady() noexcept;
bool CSOL_BodyStainsActive() noexcept;
void CSOL_ResetBodyStains() noexcept;
void CSOL_SetBodyStainPolicy(bool enabled, float splashRadius) noexcept;
void CSOL_ClearBodyStain(CBaseEntity& actor) noexcept;
void CSOL_CopyDroppedBodyStain(CWeaponBox& box, CBasePlayerItem& weapon) noexcept;
float CSOL_PackBodyStain(const edict_s& actor, unsigned index, bool player, bool local) noexcept;

void CSOL_RecordBodyStains(CCSOLScenarioMonster& monster, entvars_s* attacker,
    const TraceResult* trace, const Vector& hit) noexcept;
void CSOL_ClearBodyStainSplash() noexcept;
void CSOL_ApplyBodyStainSplash(CBaseEntity& victim, entvars_s* inflictor, entvars_s* attacker) noexcept;

// Built only by firearm trace producers. Keep the first ray origin across
// penetrations and associate a TraceAttack with this exact shot and target.
class CSOLBodyStainShot
{
public:
    CSOLBodyStainShot(CBaseEntity* shooter, const Vector& source) noexcept;
    ~CSOLBodyStainShot();
    CSOLBodyStainShot(const CSOLBodyStainShot&) = delete;
    CSOLBodyStainShot& operator=(const CSOLBodyStainShot&) = delete;
    void TraceAttack(CBaseEntity* target, entvars_s* attacker, float damage,
        const Vector& direction, TraceResult* trace, int damageType) const;

private:
    friend void CSOL_RecordBodyStains(CCSOLScenarioMonster&, entvars_s*, const TraceResult*, const Vector&) noexcept;
    friend void CSOL_ApplyBodyStainSplash(CBaseEntity&, entvars_s*, entvars_s*) noexcept;
    friend void CSOL_ClearBodyStain(CBaseEntity&) noexcept;
    const edict_s* owner_{};
    const edict_s* weapon_{};
    int ownerSerial_{}, weaponSerial_{}, spawn_{}, view_{}, held_{};
    float source_[3]{};
    unsigned generation_{};
    mutable bool splashed_{};
};
