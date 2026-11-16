// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "guest_cof_pushback.hpp"
#include "native/guest/cof/server/effects_dispatch.hpp"
#include "../scenario/core/game_rules_gate.h"
#include "../scenario/core/scenario_runtime.h"
#include "../scenario/core/scenario_session.h"
#include "../scenario/core/human_kick_damage.h"
#include "../scenario/monsters/scenario_monster_pool.h"
#include <cmath>

namespace csol::server::cof
{
namespace
{
using namespace guest::cof;
struct Call
{
    CBasePlayer& actor;
    std::shared_ptr<CCSOLScenarioRuntime> runtime;
    std::shared_ptr<CCSOLScenarioSession> session;
    const NativePushbackHost& host;
    EHANDLE player, cursor, target;
    CBaseEntity* cursorAddress{};
    CBaseEntity* targetAddress{};
    CCSOLMonsterPool* pool{};
    std::uint64_t round{}, lease{};
    std::size_t slot{};
    int spawn{}, iterations{};

    static bool Current(void* context) noexcept
    {
        auto& c = *static_cast<Call*>(context);
        return c.host.authority.current(c.host.authority.context) && c.runtime && !c.runtime->Closing() &&
            c.runtime->session == c.session && c.session && c.session->Map().EntityAccessAllowed() &&
            c.session->Map().RoundGeneration() == c.round && c.player.Get<CBasePlayer>() == &c.actor &&
            c.actor.m_iNumSpawns == c.spawn && !c.actor.has_disconnected &&
            c.actor.pev->deadflag == DEAD_NO && c.actor.pev->health > 0 &&
            c.host.authority.current(c.host.authority.context);
    }
    CBaseEntity* Cursor(PushbackTarget token) noexcept
    {
        if (!Current(this) || token != reinterpret_cast<PushbackTarget>(cursorAddress)) return nullptr;
        auto* entity = cursor.Get<CBaseEntity>();
        return Current(this) && entity == cursorAddress ? entity : nullptr;
    }
    CBaseEntity* Target(PushbackTarget token) noexcept
    {
        if (!Current(this) || token != reinterpret_cast<PushbackTarget>(targetAddress)) return nullptr;
        auto* entity = target.Get<CBaseEntity>();
        if (!Current(this) || entity != targetAddress) return nullptr;
        if (pool && (&session->Monsters() != pool || pool->At(slot) != entity ||
            pool->Lease(*static_cast<CCSOLScenarioMonster*>(entity)) != lease)) return nullptr;
        return entity;
    }
    static bool Read(void* context, float& amount) noexcept
    {
        auto& c = *static_cast<Call*>(context); bool infinite{};
        return Current(context) && c.host.stamina(c.host.authority.context, amount, infinite) && Current(context);
    }
    static bool Infinite(void* context, bool& infinite) noexcept
    {
        auto& c = *static_cast<Call*>(context); float amount{};
        return Current(context) && c.host.stamina(c.host.authority.context, amount, infinite) && Current(context);
    }
    static bool Set(void* context, float amount) noexcept
    {
        auto& c = *static_cast<Call*>(context);
        return Current(context) && c.host.setStamina(c.host.authority.context, amount) && Current(context);
    }
    static bool Sound(void* context, const PushbackSound& event) noexcept
    {
        auto& c = *static_cast<Call*>(context);
        auto* entity = event.emitter == reinterpret_cast<PushbackTarget>(&c.actor) ? &c.actor : c.Target(event.emitter);
        if (!entity || !Current(context)) return false;
        const auto index = static_cast<unsigned>(event.sample);
        if (index >= c.host.samples.size()) return false;
        const float volume = c.host.quietAudio ? static_cast<float>(double(event.volume) * .25) : event.volume;
        EMIT_SOUND_DYN2(entity->edict(), event.channel, c.host.samples[index], volume,
            event.attenuation, event.flags, event.pitch);
        return Current(context);
    }
    static bool Vectors(void* context) noexcept
    {
        auto& c = *static_cast<Call*>(context);
        if (!Current(context)) return false;
        UTIL_MakeVectors(c.actor.pev->angles); // Original100E8B90: pev+50.
        return Current(context);
    }
    static bool Position(void* context, PushbackVector& output) noexcept
    {
        auto& c = *static_cast<Call*>(context);
        if (!Current(context)) return false;
        const auto value = c.actor.GetGunPosition();
        if (!Current(context)) return false;
        output = {value.x, value.y, value.z}; return true;
    }
    static bool Recoil(void* context, const PushbackVector& value) noexcept
    {
        auto& c = *static_cast<Call*>(context);
        if (!Current(context)) return false;
        const auto result = CooperativeViewRecoil(c.actor, Vector(value[0], value[1], value[2]),
            c.host.recoilEvent, {context, Current});
        return result == WeaponEffectStatus::Issued && Current(context);
    }
    static bool Next(void* context, PushbackTarget previous, const PushbackVector& center,
        float radius, PushbackTarget& output) noexcept
    {
        auto& c = *static_cast<Call*>(context);
        CBaseEntity* prior = previous ? c.Cursor(previous) : nullptr;
        if ((previous && !prior) || !Current(context) || ++c.iterations > gpGlobals->maxEntities + 1) return false;
        auto* entity = UTIL_FindEntityInSphere(prior, Vector(center[0], center[1], center[2]), radius);
        if (!Current(context)) return false;
        c.cursor = entity; c.cursorAddress = entity;
        output = reinterpret_cast<PushbackTarget>(entity);
        return !entity || c.Cursor(output) == entity;
    }
    static bool Monster(void* context, PushbackTarget token, PushbackTarget& output) noexcept
    {
        auto& c = *static_cast<Call*>(context);
        auto* entity = c.Cursor(token);
        if (!entity) return false;
        c.target = nullptr; c.targetAddress = nullptr; c.pool = nullptr; c.lease = 0;
        if (auto* monster = dynamic_cast<CCSOLScenarioMonster*>(entity))
        {
            auto& pool = c.session->Monsters();
            if (monster->Pool() != &pool || !pool.Borrowed(*monster)) { output = 0; return Current(context); }
            c.pool = &pool; c.slot = monster->Slot(); c.lease = pool.Lease(*monster);
        }
        else if (!dynamic_cast<CBaseMonster*>(entity)) { output = 0; return Current(context); }
        // CSOL's pooled monsters derive CBaseAnimating; the native CS class
        // hierarchy substitutes for CoF MyMonsterPointer without installing AI.
        c.target = entity; c.targetAddress = entity; output = token;
        return c.Target(token) == entity;
    }
    static bool Alive(void* context, PushbackTarget token, bool& output) noexcept
    {
        auto& c = *static_cast<Call*>(context); auto* entity = c.Target(token);
        if (!entity) return false;
        output = entity->IsAlive() != FALSE;
        return c.Target(token) == entity;
    }
    static bool Damage(void* context, PushbackTarget token, const PushbackDamage& request) noexcept
    {
        auto& c = *static_cast<Call*>(context); auto* entity = c.Target(token);
        if (!entity || request.amount != 3.F || request.originalBits != 0x05001000U) return false;
        if (c.pool)
        {
            csol::scenario::ConfigError error;
            const auto result = CSOL_GuestCofPushbackMonsterDamage(*static_cast<CCSOLScenarioMonster*>(entity),
                c.actor, c.runtime, c.host.damage, error, [](const void* p) { return Current(const_cast<void*>(p)); }, &c);
            return Current(context) && (result == CSOLMonsterActionDamageResult::Applied ||
                result == CSOLMonsterActionDamageResult::Killed || result == CSOLMonsterActionDamageResult::Ignored);
        }
        entity->TakeDamage(c.actor.pev, c.actor.pev, c.host.damage, DMG_CLUB);
        return Current(context); // Original hit sound follows FALSE as well.
    }
};
}
guest::cof::PushbackResult NativeLongGunPushback(CBasePlayer& actor,
    const std::shared_ptr<CCSOLScenarioRuntime>& runtime, const NativePushbackHost& host) noexcept
{
    using namespace guest::cof;
    if (!actor.pev || !runtime || !runtime->session || !host.authority.current || !host.stamina ||
        !host.setStamina || !host.recoilEvent || !host.samples[0] || !*host.samples[0] ||
        !host.samples[1] || !*host.samples[1] || !std::isfinite(host.damage) || !(host.damage > 0) ||
        !gpGlobals || gpGlobals->maxEntities < 1 || !g_engfuncs.pfnFindEntityInSphere ||
        !g_engfuncs.pfnMakeVectors || !g_engfuncs.pfnEmitSound || !g_engfuncs.pfnPlaybackEvent)
        return PushbackResult::MissingBackend;
    try
    {
        Call call{actor, runtime, runtime->session, host};
        call.player = &actor; call.round = call.session->Map().RoundGeneration(); call.spawn = actor.m_iNumSpawns;
        return LongGunPushback({&call, reinterpret_cast<PushbackTarget>(&actor), Call::Current,
            Call::Read, Call::Infinite, Call::Set, Call::Sound, Call::Vectors, Call::Position,
            Call::Recoil, Call::Next, Call::Monster, Call::Alive, Call::Damage});
    }
    catch (...) { return PushbackResult::MissingBackend; }
}
}
