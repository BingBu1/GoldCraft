// SPDX-License-Identifier: GPL-3.0-or-later
// Reuse real GameRules/Session/Factory fixtures, not a replacement damage model.
#define main HumanKickFixtureMain
#include "../csol-human-kick-damage/sdk.cpp"
#undef main
#include "../csol-challenge-environment/provider_loader.hpp"
#include "../../native/csol/server/custom/gameplay_settings.h"
#include <filesystem>
#include <fstream>
extern "C" void weapon_scout(entvars_t*);

namespace
{
using ActionResult = CSOLMonsterActionDamageResult;
ActionResult Push(Case& c, ConfigError& error, float damage = 3, OwnerGuard* guard = nullptr)
{
    return CSOL_GuestCofPushbackMonsterDamage(*c.target, *c.actor, c.runtime, damage, error,
        guard ? &OwnerGuard::Current : +[](const void* context)
        { const auto& value = *static_cast<const Case*>(context); return value.runtime && !value.runtime->Closing(); },
        guard ? static_cast<const void*>(guard) : static_cast<const void*>(&c));
}
void CofDamage()
{
    for (int id : {0, 3, 18, 29, 40, 220, 901})
    {
        Case c; auto& weapon = c.Held(id); ConfigError error;
        c.target->HostState().lastHitgroup = HITGROUP_HEAD;
        c.target->HostState().flinchEnabled = true;
        CSOLWeaponPartEffect part; part.id = 17; part.kind = 1; part.modes = {1u << 15}; part.parameters = {99, 0, 0};
        Require(c.session->Weapons()->SetParts(weapon, {17, 0}) && c.session->Weapons()->ReplacePartEffects({part}),
            "actual held weapon has a large damage part");
        const auto velocity = c.target->pev->velocity;
        const auto sequence = c.target->pev->sequence;
        const auto lock = c.target->Animation().LockedUntil();
        Require(Push(c, error) == ActionResult::Applied, error.message.c_str());
        Check(c.target->pev->health == 997 && c.target->HostState().lastHitgroup == 0,
            "CoF base3 uses actual damage without held gun minimum, headgroup or parts");
        Check(c.actor->m_pActiveItem == &weapon && !Weapon::active && !Weapon::source,
            "CoF damage does not change weapon or call weapon kill callbacks");
        Check(c.target->pev->velocity == velocity && c.target->pev->sequence == sequence &&
            c.target->Animation().LockedUntil() == lock && !CSOL_HumanKickMonsterStaggered(*c.target, 100),
            "damage-only bridge does not install CSOL random flinch, impulse or fixed stun");
        const auto state = c.runtime->players->Find(EHANDLE(c.actor));
        Check(state && state->ordinaryMonsterDamage == 3 && state->monsterDamageHits == 1,
            "actual player statistics count CoF action damage");
    }
    { Case c; auto* ed=c.e.Reserve(c.e.nextEntity++,"weapon_scout");
      weapon_scout(&ed->v);
      auto* scout=dynamic_cast<CSCOUT*>(CBaseEntity::Instance(ed));
      Require(scout!=nullptr,"actual native Scout entity factory constructs the matched SDK class");
      scout->m_iId=WEAPON_SCOUT;scout->m_pPlayer=c.actor;c.actor->m_pActiveItem=scout;
      c.target->HostState().flinchEnabled = true;
      c.target->TakeDamage(c.actor->pev, c.actor->pev, 1, DMG_CLUB);
      Check(c.target->Animation().LockedUntil() == 100.1f, "ordinary weapon control reaches actual CSOL flinch lock"); }
    { Case c(0, 2); ConfigError error; Require(Push(c, error) == ActionResult::Applied, error.message.c_str());
      Check(c.target->pev->health == 994, "scenario Damage upgrade remains authoritative"); }
    { Case c; c.target->pev->armorvalue = 100; ConfigError error;
      Require(Push(c, error) == ActionResult::Applied, error.message.c_str());
      Check(c.target->pev->health == 998.5f && c.target->pev->armorvalue == 99.25f, "actual armor consumes CoF club damage"); }
    { Case c; ConfigError error; Require(Push(c, error, 27) == ActionResult::Applied, error.message.c_str());
      Check(c.target->pev->health == 973, "caller supplies scenario base damage without a guessed adapter default"); }
    { Case c; c.actor->m_iTeam = TERRORIST; ConfigError error;
      Check(Push(c, error) == ActionResult::Ignored && c.target->pev->health == 1000, "same-team rejection is retained"); }
    for (float damage : {0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
    { Case c; ConfigError error; Check(Push(c, error, damage) == ActionResult::Failed && c.target->pev->health == 1000,
        "invalid CoF damage fails before mutation"); }
}
void CofDeath()
{
    for (bool deferred : {false, true}) for (bool respawn : {false, true})
    {
        Case c; c.Held(WEAPON_KNIFE); c.target->pev->health = 2; ConfigError error;
        auto& requests = c.runtime->gameplay->Deaths().Requests();
        auto scope = deferred ? requests.BeginBehavior(*c.target) : std::optional<CCSOLMonsterDeathRequests::BehaviorScope>{};
        Require(!deferred || scope.has_value(), "open actual deferred CoF death scope");
        Require(Push(c, error) == ActionResult::Killed, error.message.c_str());
        if (respawn && deferred) c.runtime->players->PlayerSpawnStarted(EHANDLE(c.actor));
        if (scope) Require(requests.EndBehavior(*scope), requests.Error().c_str());
        Check(c.target->pev->deadflag == DEAD_DEAD, "actual death owner completes CoF lethal damage");
        const auto records = c.runtime->deathServices->Records();
        Check(std::count_if(records.begin(), records.end(), [](const auto& r)
            { return r.kind == CSOLScenarioDeathRecord::Kind::GuestCofPushbackKill; }) == (respawn && deferred ? 0 : 1),
            "CoF journal uses independent action credit and revokes replaced actor life");
        Check(std::none_of(records.begin(), records.end(), [](const auto& r)
            { return r.kind == CSOLScenarioDeathRecord::Kind::HumanKickKill; }), "CoF kills never masquerade as CSOL kick");
        Check(!Weapon::active && !Weapon::source && sizeof(CSOLMonsterDeathContext) == 32,
            "CoF kills bypass held gun callbacks and preserve original death context ABI");
    }
}
void CofReentry()
{
    for (bool nestedAction : {false, true})
    {
        Case c; c.Held(WEAPON_SCOUT); ConfigError error; bool nested{};
        c.e.onOffset = [&](const edict_t*)
        {
            if (nested) return; nested = true;
            if (nestedAction) Require(c.Kick(error) == Result::Applied, error.message.c_str());
            else c.target->TakeDamage(c.actor->pev, c.actor->pev, 1, DMG_CLUB);
        };
        Require(Push(c, error) == ActionResult::Applied, error.message.c_str()); c.e.onOffset = {};
        Check(nested && c.target->pev->health == (nestedAction ? 982 : 772),
            "nested same-actor target hit cannot steal pending CoF identity");
    }
    { Case c; ConfigError error; bool nested{};
      c.e.onOffset = [&](const edict_t*)
      { if (nested) return; nested = true; Require(Push(c, error) == ActionResult::Applied, error.message.c_str()); };
      Require(c.Kick(error) == Result::Applied, error.message.c_str()); c.e.onOffset = {};
      Check(c.target->pev->health == 982 && CSOL_HumanKickMonsterStaggered(*c.target, 100),
          "nested CoF hit restores outer human kick identity and motion"); }
}
void CofCancellation()
{
    for (int operation : {0, 1, 2})
    {
        Case c; ConfigError error; bool fired{};
        c.e.onOffset = [&](const edict_t*)
        {
            if (fired || c.target->pev->health == 1000) return; fired = true;
            if (operation == 0) c.runtime->players->PlayerSpawnStarted(EHANDLE(c.actor));
            else if (operation == 1) c.runtime->RequestClose();
            else c.session->Monsters().Free(*c.target);
        };
        const auto result = Push(c, error); c.e.onOffset = {};
        Check(fired && result == ActionResult::Cancelled, "real engine callback cancels replaced CoF actor/map/pool lease");
    }
}
void CofOwnerGuard()
{
    for (int phase : {0, 1, 2})
    {
        Case c; ConfigError error; OwnerGuard guard; bool revoked{};
        struct Restore
        { decltype(g_engfuncs.pfnRandomFloat) previous = g_engfuncs.pfnRandomFloat;
          ~Restore() { rewardDrawOwner = nullptr; g_engfuncs.pfnRandomFloat = previous; } } restore;
        const auto velocity = c.target->pev->velocity;
        if (phase == 0) guard.active = false;
        if (phase == 1) { rewardDrawOwner = &guard; g_engfuncs.pfnRandomFloat = RevokeRewardDrawOwner; }
        if (phase == 2) c.e.onOffset = [&](const edict_t*)
        { if (!revoked && c.target->pev->health < 1000) { revoked = true; guard.active = false; } };
        const auto result = Push(c, error, 3, &guard); c.e.onOffset = {};
        Check(result == ActionResult::Cancelled && c.target->pev->health == (phase == 2 ? 997 : 1000),
            "CoF owner revocation preserves committed health but cancels further work");
        Check(c.target->pev->velocity == velocity && !CSOL_HumanKickMonsterStaggered(*c.target, 100),
            "cancelled CoF never adds kick motion");
    }
    {
        Case c; ConfigError error; c.target->pev->health = 2;
        auto& requests = c.runtime->gameplay->Deaths().Requests();
        const auto scope = requests.BeginBehavior(*c.target); Require(scope.has_value(), "defer CoF death past owner guard lifetime");
        { OwnerGuard guard; Require(Push(c, error, 3, &guard) == ActionResult::Killed, error.message.c_str()); }
        Require(requests.EndBehavior(*scope), requests.Error().c_str());
        const auto records = c.runtime->deathServices->Records();
        Check(c.target->pev->deadflag == DEAD_DEAD && std::count_if(records.begin(), records.end(), [](const auto& r)
            { return r.kind == CSOLScenarioDeathRecord::Kind::GuestCofPushbackKill; }) == 1,
            "deferred CoF credit has no dangling stack guard callback");
    }
}
}
int main(int argc, char** argv)
{
    try
    {
        const std::string group = argc > 1 ? argv[1] : "cof-damage";
        const auto data=std::filesystem::absolute(argv[0]).parent_path()/("data-"+group);
        Require(!std::filesystem::exists(data), "fresh isolated per-group SDK game directory");
        std::filesystem::create_directories(data/"cstrike/addons/goldcraft/config");
        std::filesystem::current_path(data);
        namespace custom=csol::scenario::custom;
        custom::Draft draft;custom::Error settingError;custom::SnapshotPtr settings;
        Require(draft.Set(custom::Feature::TimeCycle,true,settingError)&&draft.Finish(settings,settingError),
            "actual typed TimeCycle settings remain enabled in damage fixture");
        { std::ofstream out(data/"cstrike/addons/goldcraft/config/gameplay.json",std::ios::binary);
          out<<custom::Serialize(*settings);Require(bool(out),"write actual settings only in isolated test data"); }
        environment_test::LoadProvider();
        if (group == "cof-damage") CofDamage();
        else if (group == "cof-death") CofDeath();
        else if (group == "cof-reentry") CofReentry();
        else if (group == "cof-cancel") CofCancellation();
        else if (group == "cof-owner") CofOwnerGuard();
        else if (group == "kick-damage") Damage();
        else if (group == "kick-stagger") Stagger();
        else if (group == "kick-death") Death();
        else if (group == "kick-reentry") Reentry();
        else if (group == "kick-cancel") Cancellation();
        else if (group == "kick-owner") OwnerGuardCases();
        else throw std::runtime_error("unknown action damage group");
    }
    catch (const std::exception& e) { ++failures; std::cerr << "EXCEPTION " << e.what() << '\n'; }
    std::cout << "behaviorChecks=" << checks << " checks=" << checks << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
