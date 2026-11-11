// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "weapon_melee.hpp"
#include <array>
#include <cmath>

namespace guest::cof
{
namespace
{
constexpr std::array<const char*, 8> Sounds{
    "player/police/Swing/Swing1.wav", "player/police/Swing/Swing2.wav",
    "player/police/Swing/Swing3.wav", "player/police/Swing/Swing4.wav",
    "player/police/Swing/Swing5.wav", "player/police/Swing/Swing6.wav",
    "weapons/mobile/punch_swing.wav", "weapons/mobile/punch_smack.wav"
};
constexpr int DamageFlags = 0x04001000;
bool Finite(const Vector& v) noexcept
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
struct Owner
{
    EntityHandle<CBasePlayer> handle;
    const CBasePlayer* expected;
};

bool Current(const MeleeCall& call, const Owner& owner) noexcept
{
    return call.current(call.context) && owner.handle.Get<CBasePlayer>() == owner.expected;
}
class DamageRegister
{
public:
    DamageRegister() noexcept : previous_(gMultiDamage) {}
    ~DamageRegister() { gMultiDamage = previous_; }
private:
    MULTIDAMAGE previous_;
};
void Sound(edict_t* owner, int channel, int index, bool quiet) noexcept
{
    // 100FC9F0 converts through double*.25 for these ordinary WAV paths.
    const float volume = quiet ? static_cast<float>(1.0 * 0.25) : 1.0f;
    EMIT_SOUND_DYN2(owner, channel, Sounds[index], volume, 0.8f, 0, 100);
}
float OriginalLength(const Vector& v) noexcept
{
    const float y2 = v.y * v.y;
    const float x2 = v.x * v.x;
    const float z2 = v.z * v.z;
    return static_cast<float>(std::sqrt(static_cast<double>((y2 + x2) + z2)));
}
bool HullIntersection(const Vector& source, TraceResult& trace, edict_t* skip,
    const MeleeCall& call, const Owner& owner) noexcept
{
    // 100DF820: doubled hull endpoint, then the eight +/-16,+/-16,+/-18
    // corners. The actual CS knife helper has different bounds/trace flags.
    const Vector delta = trace.vecEndPos - source;
    const Vector hullEnd{source.x + (delta.x + delta.x),
        source.y + (delta.y + delta.y), source.z + (delta.z + delta.z)};
    TraceResult candidate{};
    UTIL_TraceLine(source, hullEnd, dont_ignore_monsters, skip, &candidate);
    if (!Current(call, owner)) return false;
    if (candidate.flFraction < 1.0f) { trace = candidate; return true; }
    constexpr float bounds[2][3]{{-16,-16,-18},{16,16,18}};
    float nearest = 1000000.0f;
    for (int i=0;i<2;++i)
        for (int j=0;j<2;++j)
            for (int k=0;k<2;++k)
            {
                const Vector end{hullEnd.x + bounds[i][0], hullEnd.y + bounds[j][1], hullEnd.z + bounds[k][2]};
                UTIL_TraceLine(source, end, dont_ignore_monsters, skip, &candidate);
                if (!Current(call, owner)) return false;
                if (candidate.flFraction < 1.0f)
                {
                    const float distance = OriginalLength(candidate.vecEndPos - source);
                    if (distance < nearest) { nearest = distance; trace = candidate; }
                }
            }
    return true;
}
bool Shake(CBasePlayer& player, const PistolMeleeEffects& effects, bool shortDuration,
    const MeleeCall& call, const Owner& owner) noexcept
{
    // 10119DE0: original grounded recipient/radius test, without adding the
    // host UTIL_ScreenShake's separate dormant-player policy.
    for (int i=1;i<=gpGlobals->maxClients;++i)
    {
        auto* edict = INDEXENT(i);
        if (!Current(call, owner)) return false;
        if (!edict || edict->free) continue;
        auto* recipient = GET_PRIVATE<CBaseEntity>(edict);
        if (!recipient || !(recipient->pev->flags & FL_ONGROUND)) continue;
        const EntityHandle<CBaseEntity> recipientHandle(recipient);
        if (!(OriginalLength(player.pev->origin - recipient->pev->origin) < 2.0f)) continue;
        if (!Current(call, owner) || recipientHandle.Get<CBaseEntity>() != recipient) return false;
        MESSAGE_BEGIN(MSG_ONE, effects.shakeMessage, nullptr, edict);
        WRITE_SHORT(20480); // 5*4096
        WRITE_SHORT(shortDuration ? 409 : 614); // trunc(.1/.15*4096)
        WRITE_SHORT(64); // .25*256
        MESSAGE_END();
        if (!Current(call, owner)) return false;
    }
    return true;
}
}

bool PrecachePistolMeleeSounds() noexcept
{
    bool ready = true;
    for (const char* sound : Sounds)
        if (PRECACHE_SOUND(const_cast<char*>(sound)) <= 0) ready = false;
    return ready;
}
MeleeStatus CheckPistolWeaponMelee(CBasePlayer& player, const PistolMeleeRequest& request,
    const PistolMeleeEffects& effects, const MeleeCall& call) noexcept
{
    if (request.world != MeleeWorld::Multiplayer) return MeleeStatus::UnsupportedWorld;
    if (!player.pev || !gpGlobals || gpGlobals->maxClients < 1 || gpGlobals->maxClients > MAX_CLIENTS ||
        !call.current || request.damage < 0 || !Finite(player.pev->v_angle) || !Finite(player.pev->origin))
        return MeleeStatus::InvalidInput;
    if (!effects.soundsReady || !effects.recoil || effects.shakeMessage < 64 || effects.shakeMessage > 255)
        return MeleeStatus::MissingEffects;
    if (!call.current(call.context)) return MeleeStatus::Cancelled;
    return player.m_pActiveItem ? MeleeStatus::Ready : MeleeStatus::NoActiveWeapon;
}
MeleeStatus PistolWeaponMelee(CBasePlayer& player, const PistolMeleeRequest& request,
    const PistolMeleeEffects& effects, const MeleeCall& call) noexcept
{
    const auto ready = CheckPistolWeaponMelee(player, request, effects, call);
    if (ready != MeleeStatus::Ready) return ready;
    const Owner owner{EntityHandle<CBasePlayer>(&player), &player};
    if (!Current(call, owner)) return MeleeStatus::Cancelled;
    auto* const ownerEdict = player.edict();
    DamageRegister transaction;
    const int breath = RANDOM_LONG(0,5);
    if (!Current(call, owner)) return MeleeStatus::Cancelled;
    if (breath < 0 || breath > 5) return MeleeStatus::InvalidInput;
    Sound(ownerEdict, CHAN_VOICE, breath, request.quietAudio);
    if (!Current(call, owner)) return MeleeStatus::Cancelled;
    Sound(ownerEdict, CHAN_AUTO, 6, request.quietAudio);
    if (!Current(call, owner)) return MeleeStatus::Cancelled;
    effects.recoil(effects.context, player, {1,1,8});
    if (!Current(call, owner)) return MeleeStatus::Cancelled;
    UTIL_MakeVectors(player.pev->v_angle);
    if (!Current(call, owner)) return MeleeStatus::Cancelled;
    const Vector source = player.GetGunPosition();
    if (!Current(call, owner)) return MeleeStatus::Cancelled;
    if (!Finite(source) || !Finite(gpGlobals->v_forward)) return MeleeStatus::InvalidInput;
    const Vector end{source.x + gpGlobals->v_forward.x * 32.0f,
        source.y + gpGlobals->v_forward.y * 32.0f, source.z + gpGlobals->v_forward.z * 32.0f};
    TraceResult trace{};
    UTIL_TraceLine(source, end, dont_ignore_monsters, ownerEdict, &trace);
    if (!Current(call, owner)) return MeleeStatus::Cancelled;
    if (trace.flFraction >= 1.0f)
    {
        UTIL_TraceHull(source, end, dont_ignore_monsters, 3, ownerEdict, &trace);
        if (!Current(call, owner)) return MeleeStatus::Cancelled;
        if (!(trace.flFraction < 1.0f)) return MeleeStatus::Miss;
        auto* target = CBaseEntity::Instance(trace.pHit);
        if (!Current(call, owner)) return MeleeStatus::Cancelled;
        const EntityHandle<CBaseEntity> targetHandle(target);
        bool bsp = !target;
        if (target)
        {
            bsp = target->IsBSPModel() != FALSE;
            if (!Current(call, owner) || targetHandle.Get<CBaseEntity>() != target) return MeleeStatus::Cancelled;
        }
        if (bsp && !HullIntersection(source, trace, ownerEdict, call, owner)) return MeleeStatus::Cancelled;
    }
    if (!(trace.flFraction < 1.0f)) return MeleeStatus::Miss;
    auto* target = CBaseEntity::Instance(trace.pHit);
    if (!Current(call, owner)) return MeleeStatus::Cancelled;
    const EntityHandle<CBaseEntity> targetHandle(target);
    bool shortDuration = false;
    if (target)
    {
        ClearMultiDamage();
        if (!Current(call, owner) || targetHandle.Get<CBaseEntity>() != target) return MeleeStatus::Cancelled;
        target->TraceAttack(player.pev, static_cast<float>(request.damage), gpGlobals->v_forward, &trace, DamageFlags);
        if (!Current(call, owner) || targetHandle.Get<CBaseEntity>() != target) return MeleeStatus::Cancelled;
        ApplyMultiDamage(player.pev, player.pev);
        if (!Current(call, owner) || targetHandle.Get<CBaseEntity>() != target) return MeleeStatus::Cancelled;
        const int first = target->Classify();
        if (!Current(call, owner) || targetHandle.Get<CBaseEntity>() != target) return MeleeStatus::Cancelled;
        if (first != 0)
        {
            const int second = target->Classify();
            if (!Current(call, owner) || targetHandle.Get<CBaseEntity>() != target) return MeleeStatus::Cancelled;
            shortDuration = second != 1;
        }
    }
    if (shortDuration)
    {
        Sound(ownerEdict, CHAN_AUTO, 7, request.quietAudio);
        if (!Current(call, owner)) return MeleeStatus::Cancelled;
        if (!Shake(player, effects, true, call, owner)) return MeleeStatus::Cancelled;
    }
    else
    {
        if (!Shake(player, effects, false, call, owner)) return MeleeStatus::Cancelled;
        Sound(ownerEdict, CHAN_AUTO, 7, request.quietAudio);
        if (!Current(call, owner)) return MeleeStatus::Cancelled;
    }
    return MeleeStatus::Hit;
}
}
