// SPDX-License-Identifier: GPL-3.0-or-later
// Actual Session/player/AK47/ordinary-monster entrypoints over controlled
// engine edges. Existence fixtures do not decode models or validate audio.
#include "../csol-human-kick-damage/world_fixture.hpp"
#include "../../native/csol/server/scenario/core/human_kick.h"
#include "../../native/csol/scenario/core/human_kick.hpp"
#include "../../native/csol/server/scenario/core/scenario_gameplay.h"
#include "../../native/csol/server/scenario/core/scenario_equipment.h"
#include "../../native/csol/server/scenario/monsters/scenario_monster_ordinary.h"
#include "pm_math.h"

#include <cmath>
#include <limits>
#include <optional>
#include <string_view>

std::shared_ptr<CCSOLScenarioRuntime> CSOL_TestScenarioRuntimeForAccess(const CSOLScenarioEntityAccess* access)
{ return CSOL_ScenarioRuntimeForAccess(access); }

namespace kick_test
{
using sdk_test::Engine;
using sdk_test::Require;
namespace kick = csol::scenario::human_kick;
using Error = csol::scenario::ConfigError;
unsigned behaviorChecks{};
void Expect(bool value, const char* label)
{ ++behaviorChecks; sdk_test::Check(value, label); }

std::vector<std::string> ColdPaths()
{
    std::vector<std::string> paths{kick::FootModel, std::string("sound/") + kick::SwingSound};
    for (const auto* sound : kick::HitSounds) paths.emplace_back(std::string("sound/") + sound);
    return paths;
}
void Resources(std::map<std::string, std::string>& files)
{
    teddy_sdk_test::GroundFixtureResources(files);
    // Each named production cold consumer gets an explicit map entry. No
    // permissive size/exists fallback: individual removals must be rejected.
    for (const auto& path : ColdPaths()) files[path] = "explicit existence fixture; not decoded asset data";
}

template<std::size_t N> struct Studio
{
    studiohdr_t header{};
    std::array<mstudioseqdesc_t, N> sequences{};
    Studio()
    {
        header.length = sizeof(*this); header.numseq = static_cast<int>(N);
        header.seqindex = static_cast<int>(reinterpret_cast<char*>(sequences.data()) - reinterpret_cast<char*>(this));
        for (auto& sequence : sequences)
        { sequence.fps = 30; sequence.numframes = 43; sequence.flags = STUDIO_LOOPING; }
    }
};
struct Packet
{
    int destination{}, id{}, receiver{};
    std::vector<std::byte> bytes;
};
struct Sound
{
    int channel{}, flags{}, pitch{};
    std::string path;
    float volume{}, attenuation{};
};

struct Case
{
    inline static Case* active{};
    kick_world_test::WorldFixture f;
    CBasePlayer& player{f.Player()};
    CBasePlayerWeapon* weapon{};
    Studio<2> playerStudio;
    Studio<csol::scenario::MonsterSequenceCount> monsterStudio;
    std::vector<EHANDLE> targets;
    std::vector<Packet> packets;
    std::vector<Sound> sounds;
    std::function<void(std::string_view)> callback;
    Error error;
    Packet pending;
    int spheres{}, lines{}, deploys{}, modelReads{}, weaponAnims{};
    bool open{}, rejectDeploy{}, blocked{}, permitCallback{true};

    explicit Case(kick_world_test::WorldFixture::ResourceSetup setup = nullptr) : f(setup)
    {
        Require(!active, "one kick SDK fixture"); active = this;
        CSOL_SetHumanKickPolicy(true);
        std::snprintf(playerStudio.sequences[0].label, sizeof(playerStudio.sequences[0].label), "idle");
        std::snprintf(playerStudio.sequences[1].label, sizeof(playerStudio.sequences[1].label), "zombiekick");
        playerStudio.sequences[1].flags = 0;
        const auto names = csol::scenario::MonsterSequenceNames();
        for (std::size_t i = 0; i < names.size(); ++i)
            std::snprintf(monsterStudio.sequences[i].label, sizeof(monsterStudio.sequences[i].label), "%s", names[i].data());
        g_engfuncs.pfnGetModelPtr = [](edict_t* edict) -> void*
        {
            auto& c = *active; ++c.modelReads;
            return (edict->v.flags & FL_CLIENT) ? static_cast<void*>(&c.playerStudio) : static_cast<void*>(&c.monsterStudio);
        };
        g_engfuncs.pfnCanSkipPlayer = [](const edict_t*) { return 0; };
        g_engfuncs.pfnInfoKeyValue = [](char*, const char* key) -> char*
        { return const_cast<char*>(!std::strcmp(key, "csol_protocol") ? "1" : ""); };
        g_engfuncs.pfnMakeVectors = [](const float* angles)
        { AngleVectors(angles, gpGlobals->v_forward, gpGlobals->v_right, gpGlobals->v_up); };
        g_engfuncs.pfnRandomFloat = [](float low, float high) { return low + (high - low) * .5f; };
        g_engfuncs.pfnRandomLong = [](int32 low, int32) -> int32 { return low; };
        g_engfuncs.pfnTraceTexture = [](edict_t*, const float*, const float*) -> const char* { return "CONCRETE"; };
        g_engfuncs.pfnTraceHull = [](const float*, const float* end, int, int, edict_t*, TraceResult* trace)
        { *trace = {}; trace->flFraction = 1; trace->vecEndPos = Vector(end); };
        f.engine.onTrace = [](edict_t*, TraceResult& trace)
        { trace.flFraction = 1; };
        g_engfuncs.pfnMessageBegin = [](int destination, int id, const float*, edict_t* recipient)
        {
            auto& c = *active; Require(!c.open, "actual SDK messages are not nested"); c.open = true;
            c.pending = {destination, id, recipient ? Engine::Index(recipient) : 0, {}};
        };
        g_engfuncs.pfnWriteByte = g_engfuncs.pfnWriteChar = [](int value)
        { active->pending.bytes.push_back(std::byte(static_cast<unsigned char>(value))); };
        g_engfuncs.pfnWriteShort = g_engfuncs.pfnWriteEntity = [](int value)
        { for (unsigned shift : {0u, 8u}) active->pending.bytes.push_back(std::byte((static_cast<unsigned>(value) >> shift) & 255)); };
        g_engfuncs.pfnWriteLong = [](int value)
        { for (unsigned shift : {0u, 8u, 16u, 24u}) active->pending.bytes.push_back(std::byte((static_cast<unsigned>(value) >> shift) & 255)); };
        g_engfuncs.pfnWriteString = [](const char* value)
        { do { active->pending.bytes.push_back(std::byte(static_cast<unsigned char>(*value))); } while (*value++); };
        g_engfuncs.pfnMessageEnd = []
        {
            auto& c = *active; Require(c.open, "actual SDK message closes once"); c.open = false;
            if (c.pending.id == SVC_WEAPONANIM) ++c.weaponAnims;
            c.packets.push_back(std::move(c.pending)); c.Edge("message");
        };
        g_engfuncs.pfnEmitSound = [](edict_t*, int channel, const char* path, float volume, float attenuation, int flags, int pitch)
        { active->sounds.push_back({channel, flags, pitch, path, volume, attenuation}); active->Edge("sound"); };
        g_ReGameHookchains.m_CBasePlayerWeapon_DefaultDeploy.registerHook(Deploy, 210);

        auto* edict = Engine::Create(Engine::String("weapon_ak47"));
        edict->v.spawnflags |= SF_NORESPAWN;
        Require(f.engine.api.pfnSpawn(edict) == 0, "actual AK47 export Spawn runs");
        weapon = dynamic_cast<CBasePlayerWeapon*>(CBaseEntity::Instance(edict));
        Require(weapon && weapon->m_iId == WEAPON_AK47, "actual stock AK47 identity");
        Require(player.AddPlayerItem(weapon) != FALSE, "actual inventory accepts AK47");
        weapon->AttachToPlayer(&player);
        player.SelectItem("weapon_ak47");
        Require(player.m_pActiveItem == weapon && weapon->m_pPlayer == &player &&
            weapon->pev->owner == player.edict(), "actual select/attach establishes active owned weapon");
        // Controlled initial command-time state after normal draw has elapsed;
        // readiness still executes all production weapon, profile and model gates.
        player.m_flNextAttack = 0; player.m_bCanShoot = true; player.m_iFOV = 90;
        weapon->m_flNextPrimaryAttack = weapon->m_flNextSecondaryAttack = 0;
        weapon->m_fInReload = FALSE; weapon->m_flStartThrow = weapon->m_flReleaseThrow = 0;
        player.pev->modelindex = Engine::Model("models/player/fixture/fixture.mdl");
        player.pev->origin = g_vecZero; player.pev->view_ofs = Vector(0, 0, 28);
        player.pev->v_angle = g_vecZero; player.pev->velocity = g_vecZero;
        Require(f.players->Prepare(player),
            "actual player preparation configures owned upgrades and equipment for ordinary damage");
        Require(f.runtime->gameplay && f.runtime->gameplay->Active(), "actual gameplay occupancy activates");
        Require(CSOL_PrecacheHumanKick(f.session->Map().EntityAccess(), error), "actual kick cold resources prepare");
        Require(CSOL_UpdateHumanKick(f.runtime, player, error), "actual initial availability update");
        const auto initial = Snapshot();
        Require(initial && (initial->flags & kick::Ready), "real Ready accepts initialized active Session/player/AK47/model");
        packets.clear(); sounds.clear(); spheres = lines = deploys = weaponAnims = modelReads = 0;
        InstallQueries();
    }
    static BOOL Deploy(IReGameHook_CBasePlayerWeapon_DefaultDeploy* chain, CBasePlayerWeapon* item,
        char* viewmodel, char* playermodel, int animation, char* extension, int skip)
    {
        auto& c = *active; ++c.deploys; c.Edge("deploy");
        if (c.rejectDeploy) return FALSE;
        return chain->callNext(item, viewmodel, playermodel, animation, extension, skip);
    }
    void Edge(std::string_view name)
    { const auto function = callback; if (function && permitCallback) function(name); }
    void InstallQueries()
    {
        g_engfuncs.pfnFindEntityInSphere = [](edict_t* start, const float* point, float radius) -> edict_t*
        {
            auto& c = *active; ++c.spheres;
            Expect(radius == kick::Reach, "actual AOE query preserves original reach bits");
            for (int i = start ? Engine::Index(start) + 1 : 1; i < c.f.engine.nextEntity; ++i)
            {
                auto* edict = Engine::At(i);
                if (!edict->free && (edict->v.origin - Vector(point)).LengthSquared() <= radius * radius) return edict;
            }
            return Engine::At(0);
        };
        g_engfuncs.pfnTraceLine = [](const float*, const float* end, int flags, edict_t* skip, TraceResult* trace)
        {
            auto& c = *active; ++c.lines;
            Expect(flags == dont_ignore_monsters && skip == c.player.edict(), "actual LOS keeps native mask/self exclusion");
            *trace = {}; trace->flFraction = c.blocked ? .25f : 1; trace->vecEndPos = Vector(end);
            if (c.blocked) trace->pHit = Engine::At(0);
            c.Edge("trace");
        };
    }
    std::optional<kick::Snapshot> Snapshot() const
    {
        const auto id = f.engine.messages.find(kick::MessageName);
        if (id == f.engine.messages.end()) return {};
        for (auto it = packets.rbegin(); it != packets.rend(); ++it)
            if (it->id == id->second)
            {
                Require(it->destination == MSG_ONE && it->receiver == player.entindex(), "actual snapshot is private to exact player");
                return kick::Decode(it->bytes);
            }
        return {};
    }
    bool Try() { error = {}; return CSOL_TryHumanKick(f.runtime, player, error); }
    bool Update() { error = {}; return CSOL_UpdateHumanKick(f.runtime, player, error); }
    void AdvanceAnimation(float delta)
    {
        f.engine.globals.time += delta;
        player.StudioFrameAdvance();
    }
    CCSOLScenarioMonster& Target(const Vector& origin)
    {
        // Full production factory initialization supplies actual behavior and
        // spawn lease. No private-ready bit or substitute damage class.
        const Vector actorOrigin=player.pev->origin;player.pev->origin=Vector(1000,1000,128);
        const auto result = f.runtime->gameplay->Factory().Spawn(TERRORIST, 1, 1, 1);
        player.pev->origin=actorOrigin;
        if (result != CSOLMonsterSpawnResult::Spawned)
            std::cerr << "FACTORY " << f.runtime->gameplay->Factory().Error().message << '\n';
        Require(result == CSOLMonsterSpawnResult::Spawned, "actual ordinary factory creates a damage-ready monster");
        CCSOLScenarioMonster* resultEntity{};
        for (auto& edict : f.engine.edicts)
            if (!edict.free)
                if (auto* monster = dynamic_cast<CCSOLScenarioMonster*>(CBaseEntity::Instance(&edict));
                    monster && monster->Pool() == &f.session->Monsters() &&
                    f.session->Monsters().Borrowed(*monster) && monster->Behavior() &&
                    std::none_of(targets.begin(), targets.end(), [&](const EHANDLE& old) { return old.Get<CBaseEntity>() == monster; }))
                    resultEntity = monster;
        Require(resultEntity && dynamic_cast<CCSOLOrdinaryMonsterBehavior*>(resultEntity->Behavior()), "actual ordinary behavior is installed");
        Require(resultEntity->pev->deadflag==DEAD_NO&&resultEntity->pev->takedamage!=DAMAGE_NO,
            "actual spawned target is alive before attack selection");
        resultEntity->pev->origin = origin; resultEntity->pev->absmin = origin + Vector(-8, -8, -28);
        resultEntity->pev->absmax = origin + Vector(8, 8, 28);
        resultEntity->pev->health = 1000; resultEntity->pev->armorvalue = 0;
        targets.emplace_back(resultEntity); return *resultEntity;
    }
    ~Case()
    {
        callback = {}; CSOL_ResetHumanKick(player);
        g_ReGameHookchains.m_CBasePlayerWeapon_DefaultDeploy.unregisterHook(Deploy);
        active = nullptr;
    }
};

void Cold()
{
    Case c;
    const auto paths = ColdPaths();
    Expect(paths.size() == 8, "explicit current kick closure is model plus seven sounds");
    for (const auto& path : paths)
    {
        auto node = c.f.engine.files.extract(path); Require(!node.empty(), "individual exact cold path exists in fixture");
        c.error = {};
        Expect(!CSOL_PrecacheHumanKick(c.f.session->Map().EntityAccess(), c.error) && !c.error.message.empty(),
            "missing exact kick resource is rejected by actual cold consumer");
        c.f.engine.files.insert(std::move(node)); c.error = {};
        Require(CSOL_PrecacheHumanKick(c.f.session->Map().EntityAccess(), c.error), "restored exact closure prepares again");
    }
}
void State()
{
    Case c;
    c.f.engine.globals.time = kick::Cooldown;
    Require(c.Update(), "equality state update");
    auto state = c.Snapshot(); Require(state.has_value(), "equality snapshot decodes");
    Expect(!(state->flags & kick::Ready), "initial lastKick0 plus1.1 equals now is strictly rejected");
    Require(c.Try(), "strict equality request is a permitted rejection");
    Expect(c.weaponAnims == 0 && c.spheres == 0, "strict cooldown rejection never starts animation or query");
    c.f.engine.globals.time = std::nextafter(kick::Cooldown, std::numeric_limits<float>::infinity());
    Require(c.Try(), "first representable time after cooldown is accepted");
    state = c.Snapshot();
    Expect(state && (state->flags & kick::Active), "accepted request publishes actual Active state");
    Expect(c.player.pev->sequence == 1 && c.player.m_flFrameRate == 360 && !c.player.m_fSequenceFinished,
        "actual LookupSequence and ResetSequenceInfo establish kick animation/frame rate");
    Expect(c.player.pev->viewmodel && std::string_view(STRING(c.player.pev->viewmodel)) == kick::FootModel &&
        int(c.player.pev->weaponmodel) == 0 && c.player.m_flNextAttack > 999999,
        "actual start installs foot view and owns a large action lock");
    c.f.engine.globals.time += 10;
    Require(c.Update(), "expired cooldown while animation has not advanced");
    Expect(CSOL_PreserveHumanKickAnimation(c.player, PLAYER_WALK) && c.deploys == 0,
        "elapsed wall clock alone does not complete the real sequence");
    Expect(!CSOL_PreserveHumanKickAnimation(c.player, PLAYER_DIE), "death animation is never suppressed");
}
void Completion()
{
    Case c; Require(c.Try(), "start completion case");
    c.AdvanceAnimation(.5f); Require(c.Update(), "update before real animation completion");
    Expect(!c.player.m_fSequenceFinished && c.deploys == 0, "unfinished SDK sequence cannot Deploy");
    c.AdvanceAnimation(.3f);
    Require(c.player.m_fSequenceFinished != FALSE, "actual StudioFrameAdvance produces completion flag");
    Require(c.Update(), "completed kick recovers successfully");
    Expect(c.deploys == 1 && c.player.m_flNextAttack == .75f, "actual AK47 Deploy chooses native draw delay after releasing owned lock");
    Expect(std::string_view(STRING(c.player.pev->viewmodel)) == "models/v_ak47.mdl" &&
        int(c.player.pev->weaponmodel) != 0 && c.player.pev->weaponanim != 35,
        "actual Deploy restores native weapon models and sequence rather than opcode35");
    Expect(!CSOL_PreserveHumanKickAnimation(c.player, PLAYER_WALK), "completion releases animation preservation");
    const int deploys = c.deploys; Require(c.Update(), "repeated completed update");
    Expect(c.deploys == deploys, "completed update never repeats Deploy");
}
void DeployFailure()
{
    Case c; Require(c.Try(), "start failure case"); c.AdvanceAnimation(.8f); c.rejectDeploy = true;
    Expect(!c.Update(), "actual Deploy rejection is retained as failure");
    Expect(c.deploys == 1 && c.player.m_flNextAttack <= 0, "failed native Deploy cannot strand the kick million-second lock");
    Expect(!CSOL_PreserveHumanKickAnimation(c.player, PLAYER_WALK), "failed recovery releases active animation ownership");
}
void Targets()
{
    Case c;
    // Temporarily retain the common spawn trace while actual Factory creates
    // each creature; only the kick LOS edge uses the authored blocking world.
    const auto query = g_engfuncs.pfnTraceLine;
    g_engfuncs.pfnTraceLine = [](const float*, const float* end, int, edict_t*, TraceResult* trace)
    { *trace = {}; trace->flFraction = 1; trace->vecEndPos = Vector(end); };
    auto& first = c.Target(Vector(40, -15, 0));
    auto& second = c.Target(Vector(60, 15, 0));
    auto& behind = c.Target(Vector(-40, 0, 0));
    auto& outside = c.Target(Vector(150, 0, 0));
    g_engfuncs.pfnTraceLine = query;
    Require(c.Try(), "AOE actual monster damage completes");
    Expect(first.pev->health == 985 && second.pev->health == 985, "both forward in-range real ordinary monsters take base15");
    Expect(behind.pev->health == 1000 && outside.pev->health == 1000, "behind and outside creatures are excluded");
    Expect(first.pev->velocity.z > 0 && second.pev->velocity.z > 0 &&
        first.HostState().humanKickUntil > c.f.engine.globals.time,
        "real ordinary targets receive authoritative impulse and stagger");
}
void Blocked()
{
    Case c;
    const auto query = g_engfuncs.pfnTraceLine;
    g_engfuncs.pfnTraceLine = [](const float*, const float* end, int, edict_t*, TraceResult* trace)
    { *trace = {}; trace->flFraction = 1; trace->vecEndPos = Vector(end); };
    auto& monster = c.Target(Vector(50, 0, 0)); g_engfuncs.pfnTraceLine = query; c.blocked = true;
    Require(c.Try(), "wall-blocked request still plays its accepted action");
    Expect(monster.pev->health == 1000 && c.lines == 2, "forward material query and per-target wall LOS prevent damage");
}
void Cancel()
{
    Case c;
    Require(c.Try(), "start explicit alive cancel case");
    CSOL_ResetHumanKick(c.player);
    Expect(c.player.m_flNextAttack == .75f && c.deploys == 1,
        "explicit live reset replaces owned million-second lock with actual native Deploy delay");
    Expect(!CSOL_PreserveHumanKickAnimation(c.player, PLAYER_WALK), "explicit reset revokes animation owner");
    Require(c.Update(), "fresh state can be published after reset");
    const auto state = c.Snapshot(); Require(state.has_value(), "fresh reset state decodes");
    Expect(!(state->flags & (kick::Ready | kick::Active)), "same-life reset preserves original kick cooldown");
    // Explicit engine command-time input after the real .75 Deploy delay;
    // this component entry does not run full PlayerPostThink weapon timers.
    c.f.engine.globals.time += 1.2f; c.player.m_flNextAttack = 0;
    Require(c.Update(), "post-delay availability update");
    const auto ready = c.Snapshot();
    Expect(ready && (ready->flags & kick::Ready), "cancelled same-life kick becomes ready only after cooldown and draw delay");
}
void CallbackCancel()
{
    Case c;
    const auto query = g_engfuncs.pfnTraceLine;
    g_engfuncs.pfnTraceLine = [](const float*, const float* end, int, edict_t*, TraceResult* trace)
    { *trace = {}; trace->flFraction = 1; trace->vecEndPos = Vector(end); };
    auto& first = c.Target(Vector(40, 0, 0)); auto& second = c.Target(Vector(60, 10, 0));
    g_engfuncs.pfnTraceLine = query;
    Require(CSOL_UpdateHumanKick(c.f.runtime, c.player, c.error, true), "publish old ready state before callback cancellation");
    const auto before = c.Snapshot();
    Require(before.has_value(), "old ready revision is observed");
    bool reset{};
    c.callback = [&](std::string_view edge)
    {
        if (edge != "trace" || c.lines != 2 || reset) return;
        reset = true; CSOL_ResetHumanKick(c.player);
        Require(c.Update(), "trace callback update safely defers while outer operation owns the slot");
        Require(c.Try(), "trace callback F request safely defers while outer operation owns the slot");
    };
    c.Try(); c.callback = {};
    Expect(reset && first.pev->health == 1000 && second.pev->health == 1000,
        "trace-reset continuation cannot damage its old target or following targets");
    Require(c.Update(), "post-callback current state remains available");
    const auto value = c.Snapshot();
    Expect(value && value->life != before->life && !(value->flags & (kick::Ready | kick::Active)),
        "post-callback replacement revision preserves same-life cooldown and releases old active flag");
    Expect(c.deploys == 1 && c.player.m_flNextAttack == .75f,
        "cancel callback's real native Deploy delay survives old continuation unwind");
}
void NewLife()
{
    Case c;
    Require(CSOL_UpdateHumanKick(c.f.runtime, c.player, c.error, true), "observe original life before spawn callback");
    const auto old = c.Snapshot(); Require(old.has_value(), "original snapshot exists");
    bool spawned{};
    c.callback = [&](std::string_view edge)
    {
        if (edge != "trace" || spawned) return;
        spawned = true;
        CSOL_ResetHumanKick(c.player, false);
        c.f.players->PlayerSpawnStarted(EHANDLE(&c.player));
        ++c.player.m_iNumSpawns;
        // Controlled engine has entered the real player-life producer; model
        // and initial timers are the new spawn's explicit incoming state.
        c.player.m_flNextAttack = .25f;
        c.player.pev->viewmodel = Engine::String("models/v_new_life.mdl");
        Require(c.Update(), "new-life callback update is deferred by slot operation guard");
        Require(c.Try(), "new-life callback kick cannot reenter the old operation");
    };
    c.Try(); c.callback = {};
    Expect(spawned && c.player.m_flNextAttack == .25f &&
        std::string_view(STRING(c.player.pev->viewmodel)) == "models/v_new_life.mdl",
        "old action continuation preserves the actual new-life producer's timer and model");
    Require(c.Update(), "new-life state publishes after outer callback unwinds");
    const auto next = c.Snapshot();
    Expect(next && next->life != old->life && !(next->flags & kick::Active), "real native spawn serial creates distinct inactive kick state");
    c.player.m_flNextAttack = 0;
    Require(c.Update(), "new spawn draw input has elapsed");
    const auto ready = c.Snapshot();
    Expect(ready && (ready->flags & kick::Ready), "old life cooldown is not inherited by real new spawn serial");
}
void Policy()
{
    Case c;Require(c.Try(),"actual action starts before policy toggle");
    const auto active=c.Snapshot();Require(active&&bool(active->flags&kick::Active),"active snapshot before policy change");
    const auto packets=c.packets.size();const auto locked=c.player.m_flNextAttack;
    CSOL_SetHumanKickPolicy(false);
    Expect(c.packets.size()==packets&&c.deploys==0&&c.player.m_flNextAttack==locked,
        "policy setter only changes server state; it performs no callback or immediate player iteration");
    Require(c.Update(),"next actual player update settles disabled action");
    const auto disabled=c.Snapshot();
    Expect(disabled&&disabled->flags==0&&disabled->life==active->life&&c.deploys==1&&
        c.player.m_flNextAttack==.75f&&!CSOL_PreserveHumanKickAnimation(c.player,PLAYER_WALK),
        "disabled update restores actual weapon once, releases owned animation lock and sends Disabled");
    const auto settledPackets=c.packets.size();
    for(unsigned i=0;i<100;++i)Require(c.Update(),"disabled policy update remains valid");
    Expect(c.packets.size()==settledPackets&&c.deploys==1,"unchanged disabled policy does not repeat snapshots or Deploy");
    Require(CSOL_UpdateHumanKick(c.f.runtime,c.player,c.error,true),"ResetHUD-style forced state snapshot");
    const auto forced=c.Snapshot();
    Expect(c.packets.size()==settledPackets+1&&forced&&forced->life==disabled->life&&
        forced->remaining==disabled->remaining&&forced->flags==0,"forced snapshot retains original life and cooldown");
    CSOL_SetHumanKickPolicy(true);Require(c.Update(),"re-enable publishes current server policy");
    const auto enabled=c.Snapshot();
    Expect(enabled&&enabled->flags==kick::Enabled&&enabled->life==active->life&&
        enabled->remaining==active->remaining,"re-enable cannot reset active action cooldown");
    const auto animations=c.weaponAnims;
    for(unsigned i=0;i<100;++i)Require(c.Try(),"re-enable cannot bypass cooldown by repeated requests");
    Expect(c.weaponAnims==animations&&c.deploys==1,"toggle does not supply a second action during cooldown");
    c.f.engine.globals.time+=1.2f;c.player.m_flNextAttack=0;
    Require(c.Update(),"actual cooldown and deployment delay have elapsed");
    const auto ready=c.Snapshot();Expect(ready&&bool(ready->flags&kick::Ready),"policy re-enable becomes ready only at normal deadline");
}
void Flood()
{
    Case c; Require(c.Try(), "first flood request starts once");
    const int spheres = c.spheres, lines = c.lines, animations = c.weaponAnims;
    const auto packetCount = c.packets.size();
    for (unsigned i = 0; i < 10000; ++i) Require(c.Try(), "flood request safely rejects while active");
    Expect(c.spheres == spheres && c.lines == lines && c.weaponAnims == animations && c.packets.size() == packetCount,
        "ten thousand active requests cause no repeated scans, animation or snapshot writes");
    Expect(c.deploys == 0 && c.player.m_flNextAttack > 999999, "F flood cannot release active lock or create extra Deploy");
}
}

int main(int argc, char** argv)
{
    try
    {
        const std::string_view group = argc > 1 ? argv[1] : "state";
        using namespace kick_test;
        if (group == "cold") Cold();
        else if (group == "state") State();
        else if (group == "completion") Completion();
        else if (group == "deploy-failure") DeployFailure();
        else if (group == "targets") Targets();
        else if (group == "blocked") Blocked();
        else if (group == "cancel") Cancel();
        else if (group == "callback-cancel") CallbackCancel();
        else if (group == "new-life") NewLife();
        else if (group == "flood") Flood();
        else if (group == "policy") Policy();
        else throw std::runtime_error("unknown human kick SDK group");
    }
    catch (const std::exception& error)
    { ++sdk_test::failures; std::cerr << "ERROR " << error.what() << '\n'; }
    std::cout << "behaviorChecks=" << kick_test::behaviorChecks << " checks=" << sdk_test::checks
        << " failures=" << sdk_test::failures << '\n';
    return sdk_test::failures ? 1 : 0;
}
