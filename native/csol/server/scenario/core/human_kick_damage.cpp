// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "human_kick_damage.h"
#include "scenario_runtime.h"
#include "scenario_session.h"
#include "scenario_player_runtime.h"
#include "game_rules_gate.h"
#include "../monsters/scenario_monster_damage.h"
#include "../monsters/scenario_monster_ordinary.h"

#include <algorithm>
#include <cmath>

#pragma clang fp contract(off)

namespace
{
bool Finite(const Vector& value)
{ return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }
float Length(const Vector& value)
{
    const float square = (value.y * value.y + value.x * value.x) + value.z * value.z;
    return static_cast<float>(std::sqrt(static_cast<double>(square)));
}

struct Hit;
thread_local Hit* activeHit{};
thread_local Hit* enteredHit{};
struct Hit
{
    Hit* previous{activeHit};
    std::shared_ptr<CCSOLScenarioRuntime> runtime;
    std::shared_ptr<CCSOLScenarioSession> session;
    std::shared_ptr<CCSOLScenarioPlayerRuntime> players;
    std::shared_ptr<const CSOLScenarioEntityAccess> access;
    EHANDLE victim, actor;
    const entvars_t* actorVars{};
    CCSOLScenarioMonster* identity{};
    const CCSOLMonsterBehavior* behavior{};
    std::uint64_t lease{};
    CSOLMonsterActionCredit credit;
    bool (*ownerCurrent)(const void*){};
    const void* ownerContext{};
    bool entered{}, consumed{}, healthCommitted{}, finished{}, killed{};

    Hit(CCSOLScenarioMonster& target, CBasePlayer& player,
        const std::shared_ptr<CCSOLScenarioRuntime>& owner,
        bool (*valid)(const void*), const void* context)
        : runtime(owner), session(owner->session), players(owner->players),
          access(session->Map().EntityAccess()), victim(&target), actor(&player), actorVars(player.pev), identity(&target), behavior(target.Behavior()),
          lease(session->Monsters().Lease(target)), ownerCurrent(valid), ownerContext(context)
    {
        credit.kind = CSOLMonsterActionKind::HumanKick;
        credit.runtime = owner;
        credit.session = session;
        credit.players = players;
        credit.access = access;
        credit.actorState = players->Find(actor);
        credit.runtimeGeneration = owner->Generation();
        credit.roundGeneration = session->Map().RoundGeneration();
        if (credit.actorState)
        {
            credit.actorGeneration = credit.actorState->generation;
            credit.actorLife = credit.actorState->skillLifeSerial;
            credit.actorSpawn = credit.actorState->nativeSpawnSerial;
        }
        activeHit = this;
    }
    ~Hit() { activeHit = previous; }
    bool OwnerCurrent() const { return ownerCurrent && ownerCurrent(ownerContext); }
    CCSOLScenarioMonster* Current() const
    {
        // Check map authority before touching a retained edict or pool slot.
        if (runtime->Closing() || runtime->Generation() != credit.runtimeGeneration ||
            runtime->session != session || runtime->players != players || !players->Active() ||
            !access || !access->Allowed() || session->Map().EntityAccess() != access ||
            session->Map().RoundGeneration() != credit.roundGeneration) return nullptr;
        auto* target = dynamic_cast<CCSOLScenarioMonster*>(victim.Get<CBaseEntity>());
        return target && target == identity && target->Behavior() == behavior && target->Pool() == &session->Monsters() &&
            session->Monsters().Borrowed(*target) && session->Monsters().Lease(*target) == lease &&
            !(target->pev->flags & FL_KILLME) ? target : nullptr;
    }
    static bool DamageCurrent(const void* pointer)
    {
        const auto& hit = *static_cast<const Hit*>(pointer);
        return hit.Current() && (hit.healthCommitted ||
            (hit.OwnerCurrent() && CSOL_HumanKickCreditCurrent(hit.credit, hit.actor.Get<CBasePlayer>())));
    }
};
}

CSOLHumanKickDamageEntry::CSOLHumanKickDamageEntry(CCSOLScenarioMonster& target,
    const entvars_t* inflictor, const entvars_t* attacker) : m_previous(enteredHit)
{
    enteredHit = nullptr;
    auto* hit = activeHit;
    // Raw identities only: this must precede the first engine lookup. Even a
    // nested hit with identical attacker/victim cannot steal this entry.
    if (!hit || hit->entered || hit->identity != &target ||
        inflictor != hit->actorVars || attacker != hit->actorVars) return;
    hit->entered = true;
    enteredHit = hit;
}
CSOLHumanKickDamageEntry::~CSOLHumanKickDamageEntry()
{ enteredHit = static_cast<Hit*>(m_previous); }

bool CSOL_HumanKickCreditCurrent(const CSOLMonsterActionCredit& credit, const CBasePlayer* actor)
{
    if (credit.kind != CSOLMonsterActionKind::HumanKick) return false;
    const auto runtime = credit.runtime.lock();
    if (!runtime || runtime->Closing() || runtime->Generation() != credit.runtimeGeneration ||
        !runtime->session || !runtime->players || !runtime->players->Active()) return false;
    const auto session = runtime->session;
    const auto players = runtime->players;
    const auto access = credit.access.lock();
    if (credit.session.lock() != session || credit.players.lock() != players ||
        !access || !access->Allowed() || session->Map().EntityAccess() != access ||
        !session->Map().EntityAccessAllowed() ||
        session->Map().RoundGeneration() != credit.roundGeneration ||
        !actor || !credit.actorState || players->Find(EHANDLE(actor)) != credit.actorState ||
        credit.actorState->generation != credit.actorGeneration ||
        credit.actorState->skillLifeSerial != credit.actorLife ||
        credit.actorState->nativeSpawnSerial != credit.actorSpawn) return false;
    return !actor->has_disconnected && !(actor->pev->flags & (FL_KILLME | FL_DORMANT));
}

bool CSOL_CaptureHumanKickDamage(CSOLMonsterDamageCall& call)
{
    auto* hit = enteredHit;
    if (!hit || hit->consumed || call.Spawn().Current() != hit->identity ||
        call.AttackerHandle().Get() != hit->actor.Get() ||
        call.AttackerHandle().GetSerialNumber() != hit->actor.GetSerialNumber() ||
        call.InflictorHandle().Get() != hit->actor.Get() ||
        call.InflictorHandle().GetSerialNumber() != hit->actor.GetSerialNumber()) return false;
    hit->consumed = true;
    call.m_action = hit->credit;
    call.m_actionCurrent = &Hit::DamageCurrent;
    call.m_actionContext = hit;
    return true;
}

void CSOL_CommitHumanKickHealth(CSOLMonsterDamageCall& call)
{
    if (call.Action() == CSOLMonsterActionKind::HumanKick)
        static_cast<Hit*>(const_cast<void*>(call.m_actionContext))->healthCommitted = true;
}
void CSOL_FinishHumanKickDamage(CSOLMonsterDamageCall& call, bool killed)
{
    if (call.Action() != CSOLMonsterActionKind::HumanKick) return;
    auto& hit = *static_cast<Hit*>(const_cast<void*>(call.m_actionContext));
    hit.finished = true;
    hit.killed = killed;
}

bool CSOL_HumanKickMonsterStaggered(CCSOLScenarioMonster& target, float now)
{
    const auto& state = target.HostState();
    return target.Pool() && target.Pool()->Borrowed(target) &&
        target.Pool()->Lease(target) == state.humanKickLease && now < state.humanKickUntil;
}

CSOLHumanKickDamageResult CSOL_HumanKickMonsterDamage(CCSOLScenarioMonster& target,
    CBasePlayer& actor, const std::shared_ptr<CCSOLScenarioRuntime>& runtime,
    float baseDamage, const Vector& direction, const Vector& actorVelocity,
    csol::scenario::ConfigError& error,
    bool (*ownerCurrent)(const void*), const void* ownerContext)
{
    using Result = CSOLHumanKickDamageResult;
    if (!ownerCurrent || !ownerCurrent(ownerContext) ||
        !runtime || runtime->Closing() || !runtime->session || !runtime->players ||
        !runtime->players->Active()) return Result::Cancelled;
    const auto session = runtime->session;
    if (!session->Map().EntityAccessAllowed() || session->State() != CSOLScenarioSessionState::Active ||
        target.Pool() != &session->Monsters() || !session->Monsters().Borrowed(target)) return Result::Cancelled;
    if (!gpGlobals || !std::isfinite(gpGlobals->time) || !std::isfinite(baseDamage) ||
        !(baseDamage > 0) || !Finite(direction) || !Finite(actorVelocity))
    { error.message = "human kick damage or impulse input is invalid"; return Result::Failed; }
    if (!target.Type() || target.pev->deadflag != DEAD_NO || target.pev->health <= 0 ||
        target.pev->takedamage == DAMAGE_NO || (target.pev->flags & FL_KILLME) ||
        (target.pev->effects & EF_NODRAW) || actor.pev->deadflag != DEAD_NO ||
        actor.has_disconnected || (actor.pev->flags & (FL_KILLME | FL_DORMANT))) return Result::Ignored;
    const float length = Length(direction), speed = Length(actorVelocity);
    // MP11486EEB..11486F4E: float length, double speed*.4+base,
    // then float scale. Normalize before multiplying, as in the original.
    const float force = static_cast<float>(500.0 + 0.4 * static_cast<double>(speed));
    const Vector unit = length > 0 ? direction / length : Vector(0, 0, 1);
    Vector impulse = unit * force;
    impulse.z += 300.0f;
    const float until = gpGlobals->time + 1.0f;
    if (!std::isfinite(length) || !std::isfinite(speed) || !Finite(impulse) || !std::isfinite(until))
    { error.message = "human kick impulse is not finite"; return Result::Failed; }
    Hit hit(target, actor, runtime, ownerCurrent, ownerContext);
    if (!hit.OwnerCurrent() || !hit.Current() || !CSOL_HumanKickCreditCurrent(hit.credit, hit.actor.Get<CBasePlayer>())) return Result::Cancelled;
    // Actual entity/behavior/armor/death hooks execute, retaining actor credit.
    target.TakeDamage(actor.pev, actor.pev, baseDamage, DMG_CLUB);
    if (hit.finished && hit.killed) return Result::Killed;
    auto* current = hit.Current();
    if (!hit.OwnerCurrent() || !current || !CSOL_HumanKickCreditCurrent(hit.credit, hit.actor.Get<CBasePlayer>())) return Result::Cancelled;
    if (!hit.finished) return Result::Ignored;
    // Factory selects ordinary behavior by skill/category, not priority.
    // Elite derives from ordinary; Boss classes can also have priority zero.
    if (dynamic_cast<const CCSOLOrdinaryMonsterBehavior*>(current->Behavior()) && current->pev->deadflag == DEAD_NO &&
        current->pev->health > 0 && current->HostState().frozenRemaining <= 0)
    {
        if (!current->Animation().LockUntil(std::max(until, current->Animation().LockedUntil())))
        { error.message = "human kick animation lock is invalid"; return Result::Failed; }
        current->HostState().humanKickLease = hit.lease;
        current->HostState().humanKickUntil = until;
        current->pev->velocity = impulse;
    }
    return Result::Applied;
}
