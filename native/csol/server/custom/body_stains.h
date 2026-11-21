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
class CCSOLScenarioMessages;
class Vector;

void CSOL_RegisterBodyStainDelta(bool player, delta_s* schema) noexcept;
bool CSOL_BodyStainDeltaReady() noexcept;
bool CSOL_BodyStainsActive() noexcept;
void CSOL_ResetBodyStains() noexcept;
void CSOL_SetBodyStainPolicy(bool enabled, float splashRadius) noexcept;
void CSOL_ClearBodyStain(CBaseEntity& actor) noexcept;
void CSOL_CopyDroppedBodyStain(CWeaponBox& box, CBasePlayerItem& weapon) noexcept;
float CSOL_PackBodyStain(const edict_s& actor, unsigned index, bool player, bool local,
    const edict_s* host = nullptr) noexcept;
void CSOL_FlushMonsterStains(const CCSOLScenarioMessages& messages) noexcept;

void CSOL_RecordBodyStains(CCSOLScenarioMonster& monster, entvars_s* attacker,
    const TraceResult* trace, const Vector& direction) noexcept;
bool CSOL_IsMeleeStainTrace(const CCSOLScenarioMonster& monster, const TraceResult* trace) noexcept;
bool CSOL_IsMeleeWeaponHeadTrace(const CCSOLScenarioMonster& monster, const TraceResult* trace,
    const entvars_s* attacker) noexcept;
void CSOL_ClearBodyStainSplash() noexcept;
void CSOL_ApplyBodyStainSplash(CBaseEntity& victim, entvars_s* inflictor, entvars_s* attacker) noexcept;

// One scope per shot or swing. Only explicitly bound contacts may stain
// the attacker's arms and the item which produced the contact.
class CSOLBodyStainShot
{
public:
    enum class Contact { Unarmed };
    CSOLBodyStainShot(CBaseEntity* shooter, const Vector& source, const CBasePlayerItem* melee = nullptr) noexcept;
    CSOLBodyStainShot(CBasePlayer& attacker, const Vector& source, Contact) noexcept;
    ~CSOLBodyStainShot();
    CSOLBodyStainShot(const CSOLBodyStainShot&) = delete;
    CSOLBodyStainShot& operator=(const CSOLBodyStainShot&) = delete;
    void TraceAttack(CBaseEntity* target, entvars_s* attacker, float damage,
        const Vector& direction, TraceResult* trace, int damageType) const;
    bool PrepareContact(CCSOLScenarioMonster& target, const TraceResult* contact, TraceResult& trace) const noexcept;
    void CommitContact(CCSOLScenarioMonster& target, const TraceResult& hit) const noexcept;

private:
    friend void CSOL_RecordBodyStains(CCSOLScenarioMonster&, entvars_s*, const TraceResult*, const Vector&) noexcept;
    friend void CSOL_ApplyBodyStainSplash(CBaseEntity&, entvars_s*, entvars_s*) noexcept;
    friend void CSOL_ClearBodyStain(CBaseEntity&) noexcept;
    bool CaptureOwner(CBasePlayer& player, const Vector& source) noexcept;
    void Splash(const Vector& hit) const noexcept;
    const edict_s* meleeAttacker_{};
    const edict_s* owner_{};
    const edict_s* weapon_{};
    int ownerSerial_{}, weaponSerial_{}, spawn_{}, view_{}, held_{};
    float source_[3]{};
    unsigned generation_{};
    bool melee_{};
    mutable bool splashed_{};
};
