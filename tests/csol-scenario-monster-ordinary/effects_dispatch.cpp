// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"
#include "../../native/csol/server/scenario_monster_effects.h"
#include "../../native/csol/server/weapons.h"

extern "C" void dummy_inflictor(entvars_t*);

namespace ordinary_test
{
namespace
{
constexpr int DotBits = 0x08000000;
struct Receipt
{
    CCSOLScenarioMonster* target{};
    entvars_t* inflictor{};
    entvars_t* attacker{};
    float damage{};
    int bits{}, sourceId{}, remaining{};
    CSOLMonsterDeathContext context;
};
struct DispatchState
{
    std::vector<Receipt> hits;
    std::vector<std::string> sdk;
    std::function<void(CCSOLScenarioMonster&, const Receipt&)> onHit;
    std::function<void(entvars_t*, entvars_t*)> beforeApply;
    std::function<void()> afterClear;
};
// This is an explicit last-stage consumer observer, not a damage owner:
// no health, armor, rules, rewards or hit formula is implemented here.
// Pool dispatch, context selection, TraceAttack, dummy/source resolution
// and all three SDK MultiDamage functions remain the production code.
struct Receiver final : CCSOLMonsterBehavior
{
    DispatchState& state;
    explicit Receiver(DispatchState& value) : state(value) {}
    void Think(CCSOLScenarioMonster&) override { throw std::runtime_error("unexpected observer Think"); }
    void Killed(CCSOLScenarioMonster&, entvars_t*, int) override { throw std::runtime_error("full death outside dispatch observer"); }
    BOOL TakeDamage(CCSOLScenarioMonster&, entvars_t*, entvars_t*, float, int) override
    { throw std::runtime_error("context-aware pool entry must reach explicit receiver"); }
    BOOL TakeDamageWithContext(CCSOLScenarioMonster& monster, entvars_t* inflictor, entvars_t* attacker,
        float damage, int bits, const CSOLMonsterDeathContext& context) override
    {
        auto* source = CBaseEntity::Instance(inflictor);
        auto* player = dynamic_cast<CBasePlayer*>(CBaseEntity::Instance(attacker));
        const int index = player ? ENTINDEX(player->edict()) : -1;
        const Receipt receipt{&monster, inflictor, attacker, damage, bits, CSOL_DamageWeaponId(source, player),
            index >= 1 && index <= 32 ? monster.DamageEffects()->At(index).count : -1, context};
        state.hits.push_back(receipt);
        const auto action = state.onHit;
        if (action) action(monster, receipt);
        return TRUE;
    }
};
struct DispatchFixture
{
    inline static DispatchFixture* active{};
    DispatchState state;
    Fixture f{true};
    CCSOLScenarioMonster* monster{};
    DispatchFixture()
    {
        Require(!active, "one DOT dispatch fixture"); active = this;
        f.engine.globals.maxClients = 32;
        monster = &f.Spawn(); Observe(*monster);
        g_engfuncs.pfnCreateNamedEntity = Create;
        g_ReGameHookchains.m_ClearMultiDamage.registerHook(Clear, 150);
        g_ReGameHookchains.m_AddMultiDamage.registerHook(Add, 150);
        g_ReGameHookchains.m_ApplyMultiDamage.registerHook(Apply, 150);
    }
    ~DispatchFixture()
    {
        state.onHit = {}; state.beforeApply = {}; state.afterClear = {};
        g_ReGameHookchains.m_ClearMultiDamage.unregisterHook(Clear);
        g_ReGameHookchains.m_AddMultiDamage.unregisterHook(Add);
        g_ReGameHookchains.m_ApplyMultiDamage.unregisterHook(Apply);
        ClearMultiDamage(); active = nullptr;
    }
    void Observe(CCSOLScenarioMonster& target)
    {
        Require(target.InstallBehavior(std::make_unique<Receiver>(state)), "install explicit final consumer observer");
        target.pev->takedamage = DAMAGE_YES;
    }
    CSOLMonsterSpawnCall Call()
    { return {f.map, f.pool, f.deathOwner->Requests(), *monster}; }
    static CSOLMonsterEffectGuard Guard(CSOLMonsterSpawnCall& call)
    { return {[](const void* value) { return static_cast<const CSOLMonsterSpawnCall*>(value)->Current(); }, &call}; }
    void Install(CBasePlayer& player, int count = 2, int hitgroup = -1, int source = 725,
        const Vector& offset = {}, std::function<void()> notify = {})
    {
        auto call = Call();
        Require(monster->DamageEffects()->Install(Guard(call), player.pev, 10, count, 0, source, hitgroup, offset, std::move(notify)),
            "install DOT through real owned API");
    }
    bool Tick()
    {
        auto call = Call(); ConfigError error;
        const bool result = monster->DamageEffects()->Tick(call, Guard(call), error);
        Check(error.message.empty(), "DOT dispatch has no production error");
        return result;
    }
    CBaseEntity* Dummy()
    { auto call = Call(); return f.pool.DamageInflictor(call); }
    static edict_t* Create(int name)
    {
        if (std::strcmp(STRING(name), "dummy_inflictor") != 0)
            return death_events_test::EventEngine::CreateMixed(name);
        auto& engine = active->f.engine;
        auto* ed = engine.Raw("dummy_inflictor", 20000 + ++engine.creates);
        engine.namedPending.insert(ed);
        dummy_inflictor(&ed->v); // Real export, RTTI and source-ID ownership.
        return ed; // Existing engine seam calls actual entity Spawn.
    }
    static void Clear(IReGameHook_ClearMultiDamage* chain)
    {
        active->state.sdk.emplace_back("clear"); chain->callNext();
        const auto action = active->state.afterClear; if (action) action();
    }
    static void Add(IReGameHook_AddMultiDamage* chain, entvars_t* source, CBaseEntity* target, float damage, int bits)
    { active->state.sdk.emplace_back("add"); chain->callNext(source, target, damage, bits); }
    static void Apply(IReGameHook_ApplyMultiDamage* chain, entvars_t* source, entvars_t* attacker)
    {
        active->state.sdk.emplace_back("apply");
        const auto action = active->state.beforeApply; if (action) action(source, attacker);
        chain->callNext(source, attacker);
    }
};
CSOLMonsterDeathContext Expected(bool traced)
{
    CSOLMonsterDeathContext context; context.bytes[7] = 1;
    if (traced) context.bytes[5] = context.bytes[6] = 1;
    return context;
}
}
void DamageEffectDispatchCases()
{
    for (int index : {1, 32}) for (bool traced : {false, true})
    {
        DispatchFixture f; auto& player = f.f.engine.Client(index);
        f.monster->pev->origin = Vector(100,200,300); f.monster->pev->angles.y = 90;
        f.Install(player, 2, traced ? HITGROUP_CHEST : -1, 725, Vector(10,0,3));
        f.monster->pev->angles.y = 180;
        Require(f.Tick(), "real direct/traced DOT traversal succeeds");
        Require(f.state.hits.size() == 1, "one installed source produces one actual pool damage dispatch");
        const auto& hit = f.state.hits.front();
        Check(hit.target == f.monster && hit.attacker == player.pev && hit.damage == (traced ? 15 : 10) &&
            hit.bits == DotBits && hit.sourceId == 725 && hit.remaining == 1 && hit.context.bytes == Expected(traced).bytes,
            "source1 and source32 decrement before actual damage and preserve source ID bits and context");
        Check(CSOL_DamageWeaponId(f.Dummy(), &player) == 0, "real dummy source ID clears to zero after dispatch");
        Check(f.state.sdk == (traced ? std::vector<std::string>{"clear","add","apply"} : std::vector<std::string>{}),
            "traced path calls actual SDK clear add apply while direct path bypasses accumulator");
        if (traced)
        {
            Require(f.f.traceAttacks.size() == 1, "one real TraceAttack call observed");
            const auto& trace = f.f.traceAttacks.front();
            Check(trace.attacker == player.pev && trace.damage == 10 && trace.direction == Vector(0,0,-1) &&
                trace.trace.pHit == f.monster->edict() && trace.trace.iHitgroup == HITGROUP_CHEST &&
                trace.trace.vecPlaneNormal == Vector(0,0,1) && Near(trace.trace.vecEndPos.x,100) &&
                Near(trace.trace.vecEndPos.y,210) && trace.trace.vecEndPos.z == 303,
                "real trace keeps world hit offset separate from downward direction and normal");
        }
        else Check(f.f.traceAttacks.empty(), "direct DOT does not synthesize TraceAttack");
    }
    {
        DispatchFixture f; auto& player = f.f.engine.Client(32); f.Install(player, 1);
        f.state.onHit = [&](CCSOLScenarioMonster& target, const Receipt& receipt)
        {
            if (f.state.hits.size() == 1)
                target.TakeDamage(receipt.inflictor, receipt.attacker, 2, DotBits);
        };
        Require(f.Tick(), "same-target recursion completes");
        Require(f.state.hits.size() == 2, "recursive damage reaches the actual pool entry twice");
        Check(f.state.hits[0].context.bytes == Expected(false).bytes &&
            f.state.hits[1].context.bytes == CSOLMonsterDeathContext{}.bytes,
            "consumed direct context does not leak to recursive same-target identical-argument damage");
    }
    {
        DispatchFixture f; auto& player = f.f.engine.Client(32); f.Install(player, 2, HITGROUP_CHEST);
        bool injected{};
        f.state.beforeApply = [&](entvars_t* source, entvars_t* attacker)
        {
            if (injected) return; injected = true;
            f.monster->TakeDamage(source, attacker, 2, DotBits);
        };
        Require(f.Tick(), "pre-Apply hook damage and original accumulator dispatch both complete");
        Require(f.state.hits.size() == 2, "hook injected hit precedes actual original Apply hit");
        Check(f.state.hits[0].damage == 2 && f.state.hits[0].context.bytes == CSOLMonsterDeathContext{}.bytes &&
            f.state.hits[1].damage == 15 && f.state.hits[1].context.bytes == Expected(true).bytes,
            "same-parameter TakeDamage in Apply pre-hook cannot consume pending traced context");
    }
    {
        DispatchFixture f; auto& player = f.f.engine.Client(1); f.Install(player, 2, HITGROUP_CHEST);
        auto& other = f.f.Spawn(); f.Observe(other); f.f.factoryServices.last = f.monster;
        f.state.afterClear = [&] { AddMultiDamage(player.pev, &other, 4, DotBits); };
        Require(f.Tick(), "other-target accumulator flush completes");
        Require(f.state.hits.size() == 2, "actual SDK Add flushes other target before current Apply");
        Check(f.state.hits[0].target == &other && f.state.hits[0].damage == 4 &&
            f.state.hits[0].context.bytes == CSOLMonsterDeathContext{}.bytes &&
            f.state.hits[1].target == f.monster && f.state.hits[1].context.bytes == Expected(true).bytes,
            "other-target flush never inherits pending target DOT context");
    }
    {
        int oldNotifications{}, replacementNotifications{};
        DispatchFixture f; auto& player = f.f.engine.Client(1);
        f.Install(player, 1, -1, 725, {}, [&] { ++oldNotifications; });
        f.state.onHit = [&](CCSOLScenarioMonster&, const Receipt&)
        {
            f.state.onHit = {};
            f.Install(player, 3, -1, 726, {}, [&] { ++replacementNotifications; });
        };
        Require(f.Tick(), "damage callback replaces expiring current record");
        Check(oldNotifications == 1 && replacementNotifications == 0 && f.monster->DamageEffects()->At(1).count == 3,
            "post-hit cleanup rereads replacement count instead of expiring entry snapshot");
        auto call = f.Call(); Require(f.monster->DamageEffects()->ClearAll(DispatchFixture::Guard(call)), "release replacement callback");
        Check(replacementNotifications == 1, "current replacement is notified only on later actual clear");
    }
    {
        DispatchFixture f; auto& player = f.f.engine.Client(1); f.Install(player, 2);
        auto& other = f.f.Spawn(); f.Observe(other); f.f.factoryServices.last = f.monster;
        CSOLMonsterSpawnCall inner(f.f.map, f.f.pool, f.f.deathOwner->Requests(), other);
        Require(other.DamageEffects()->Install(DispatchFixture::Guard(inner), player.pev, 5, 2, 0, 726, -1, {}, {}),
            "second host installs independent nested DOT source");
        bool innerZeroed{};
        f.state.onHit = [&](CCSOLScenarioMonster& target, const Receipt& receipt)
        {
            if (&target != f.monster) return;
            ConfigError error;
            Require(other.DamageEffects()->Tick(inner, DispatchFixture::Guard(inner), error), "nested host uses shared real dummy");
            innerZeroed = CSOL_DamageWeaponId(CBaseEntity::Instance(receipt.inflictor), &player) == 0;
        };
        Require(f.Tick(), "outer host resumes after nested DOT");
        Require(f.state.hits.size() == 2, "two real hosts receive distinct DOT dispatches");
        Check(f.state.hits[0].sourceId == 725 && f.state.hits[1].sourceId == 726 && innerZeroed &&
            f.state.hits[0].context.bytes == Expected(false).bytes && f.state.hits[1].context.bytes == Expected(false).bytes,
            "nested dummy writes clear to zero rather than restoring the outer source ID");
    }
    {
        int notified{};
        DispatchFixture f; auto& player = f.f.engine.Client(32);
        f.Install(player, 2, -1, 725, {}, [&] { ++notified; }); f.monster->pev->health = 0;
        Require(f.Tick(), "zero-health host still executes pending hit before cleanup");
        Check(f.state.hits.size() == 1 && notified == 1 && f.monster->DamageEffects()->At(32).count == 0,
            "DOT has no invented positive-health precondition and clears after hit");
    }
    {
        DispatchFixture f; auto& player = f.f.engine.Client(32); f.Install(player, 2);
        ++player.edict()->serialnumber;
        Require(f.Tick(), "stale source skips actual damage safely");
        Check(f.state.hits.empty() && f.monster->DamageEffects()->At(32).count == 2,
            "serial mismatch cannot attribute old DOT to recycled player slot or consume its positive count");
    }
    {
        DispatchFixture f; auto& player = f.f.engine.Client(1); f.Install(player, 2);
        CBaseEntity* dummy{};
        f.state.onHit = [&](CCSOLScenarioMonster&, const Receipt& receipt)
        {
            dummy = CBaseEntity::Instance(receipt.inflictor);
            f.f.deathOwner->Requests().Reset(); // Cancel session without freeing the still-observable dummy.
        };
        Check(!f.Tick(), "dispatcher generation reset cancels remaining DOT traversal");
        Check(dummy && CSOL_DamageWeaponId(dummy, &player) == 0,
            "shared source-ID cell zeros even when the old session has been invalidated");
    }
}
}
