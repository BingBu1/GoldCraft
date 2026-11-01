// SPDX-License-Identifier: GPL-3.0-or-later
// Full production skill-entity and PlayerRuntime translation units. No model
// entity classes, replacement player layouts, copied method bodies or offsets.
#include "trap_fixture.hpp"
#include <cmath>

namespace
{
using namespace trap_test;
void Baseline()
{
    Fixture f; auto& player = f.Player(); const auto trap = f.Trap();
    auto* entity = trap.Get<CBaseEntity>();
    Check(entity->pev->movetype == MOVETYPE_PUSHSTEP && entity->pev->solid == SOLID_TRIGGER &&
        entity->pev->renderamt == 120 && entity->pev->rendermode == kRenderTransAlpha,
        "real trap Spawn installs original move collision and alpha states");
    f.Touch(trap, player);
    const auto state = f.State(player);
    Check(state->monsterTrapUntil == 15 && !state->monsterTrapPending &&
        state->monsterTrapMovementApplied && player.pev->gravity == 10 &&
        player.pev->maxspeed == 1 && player.pev->velocity == g_vecZero,
        "real Touch commits effect17 and applies actual movement consumer");
    Check(entity->pev->solid == SOLID_NOT && entity->m_pfnTouch == nullptr &&
        entity->pev->sequence == 1 && entity->pev->rendermode == kRenderNormal,
        "successful capture retires collision and starts hold animation");
    f.engine.globals.time = 15;
    Check(f.players->UpdatePlayerBuffs(player, 15), "deadline equality updates real player owner");
    Check(f.players->MonsterTrapActive(EHANDLE(&player), 15) && state->monsterTrapUntil == 15 &&
        !state->monsterTrapMovementApplied && player.pev->gravity == .75f,
        "equality retains ring entry but restores saved gravity and removes movement restriction");
    f.engine.globals.time = std::nextafter(15.0f, 16.0f);
    Check(f.players->UpdatePlayerBuffs(player, f.engine.globals.time) &&
        !f.players->MonsterTrapActive(EHANDLE(&player), f.engine.globals.time),
        "strictly later cleanup removes expired trap entry");
}

void RandomCancellation()
{
    for (int kind = 0; kind < 5; ++kind)
    {
        Fixture f; auto& player = f.Player(1, 1); const EHANDLE playerHandle(&player);
        const auto trap = f.Trap(); const auto state = f.State(player);
        auto oldPlayers = f.players;
        f.onRandom = [&]
        {
            f.onRandom = {};
            if (kind == 0) f.Destroy(trap);
            if (kind == 1) f.runtime->players = std::make_shared<CCSOLScenarioPlayerRuntime>(f.session);
            if (kind == 2) f.runtime->session = std::make_shared<CCSOLScenarioSession>();
            if (kind == 3) Engine::Remove(playerHandle.Get());
            if (kind == 4)
            {
                Require(f.players->ResetMonsterSkillState(playerHandle), "RNG callback starts next player life through real monster reset");
                player.m_iNumSpawns += 1; player.pev->gravity = .6f; player.pev->velocity = Vector(3, 4, 5);
            }
        };
        f.Touch(trap, player);
        Check(f.randomCalls == 1 && !state->monsterTrapPending && state->monsterTrapUntil == 0,
            "RNG cancellation cannot commit a trap deadline or leave reservation pending");
        Check(f.resetCalls == 0, "RNG cancellation never reaches speed reset");
        if (kind != 3)
            Check(player.pev->gravity == (kind == 4 ? .6f : .75f) &&
                player.pev->velocity == (kind == 4 ? Vector(3, 4, 5) : Vector(9, 8, 7)),
                "RNG cancellation preserves target and replacement-life movement");
        // Reattach only for fixture teardown after the completed cancellation.
        f.runtime->players = oldPlayers; f.runtime->session = f.session;
    }
}

void SpeedCallbackCancellation()
{
    for (int kind = 0; kind < 4; ++kind)
    {
        Fixture f; auto& player = f.Player(); const EHANDLE playerHandle(&player);
        const auto trap = f.Trap(); const auto state = f.State(player);
        const auto life = state->monsterLifeSerial;
        f.onReset = [&](CBasePlayer& current)
        {
            f.onReset = {};
            if (kind == 0) f.Destroy(trap);
            if (kind == 1)
            {
                current.pev->deadflag = DEAD_DEAD;
                Require(f.players->ResetMonsterSkillState(playerHandle), "death consumer clears current life inside speed callback");
                current.pev->deadflag = DEAD_NO; current.m_iNumSpawns += 1;
                current.pev->gravity = .4f; current.pev->velocity = Vector(101, 102, 103);
            }
            if (kind == 2)
            {
                f.players->Remove(playerHandle);
                current.pev->gravity = .5f; current.pev->velocity = Vector(111, 112, 113);
                CSOLScenarioPlayerProfile next; next.equipment.classModsComplete = true;
                Require(f.players->SetProfile(current, next), "speed callback replaces actual player generation");
            }
            if (kind == 3) Engine::Remove(playerHandle.Get());
        };
        f.Touch(trap, player);
        Check(!state->monsterTrapPending, "speed callback cancellation releases pending reservation");
        if (kind != 3)
            Check(state->monsterTrapUntil == 0,
                "speed callback cancellation clears only the retired trap transaction");
        if (kind == 0)
            Check(player.pev->gravity == .75f && player.pev->velocity == Vector(9, 8, 7),
                "trap deletion during speed reset restores gravity without cancelling existing velocity");
        if (kind == 1)
            Check(state->monsterLifeSerial > life && player.pev->gravity == .4f &&
                player.pev->velocity == Vector(101, 102, 103), "old trap cannot write into the next life");
        if (kind == 2)
            Check(f.State(player) != state && player.pev->gravity == .5f &&
                player.pev->velocity == Vector(111, 112, 113), "old trap cannot write into replacement generation");
        if (kind == 3)
        {
            // Raw engine removal is not ClientDisconnect: CBasePlayer's real
            // OnDestroy does not retire PlayerRuntime. Never require writes
            // through the invalid old handle merely to clear unreachable data.
            Check(!playerHandle.Get() && state->monsterTrapUntil == 15,
                "raw deletion leaves only inaccessible old state and performs no stale player write");
            auto& replacement = f.Player();
            Check(f.State(replacement) != state && f.State(replacement)->monsterTrapUntil == 0 &&
                !f.players->ApplyMonsterTrapMovement(replacement, 11),
                "serial-reused player slot receives a fresh generation without old trap movement");
        }
    }
}

void EntityRelease()
{
    Fixture f;
    const auto baseline = f.session.use_count();
    for (int iteration = 0; iteration < 3; ++iteration)
    {
        const auto trap = f.Trap();
        Check(f.session.use_count() == baseline + 1, "live trap retains exactly its session owner");
        bool sawOnDestroy{};
        const auto ed = trap.Get();
        f.engine.afterFreeCallback = [&](edict_t* freed)
        {
            if (freed != ed) return;
            sawOnDestroy = true;
            Check(f.session.use_count() == baseline,
                "real OnDestroy releases shared_ptr before raw private storage is freed");
        };
        f.Destroy(trap); f.engine.afterFreeCallback = {};
        Check(sawOnDestroy && !trap.Get() && f.session.use_count() == baseline,
            "engine private free needs no C++ entity destructor and retains no session leak");
    }
}

void LifeAndExpiry()
{
    for (int kind = 0; kind < 3; ++kind)
    {
        Fixture f; auto& player = f.Player(); const auto trap = f.Trap();
        const EHANDLE handle(&player); f.Touch(trap, player);
        const auto state = f.State(player); const auto life = state->monsterLifeSerial;
        if (kind == 0)
        {
            player.pev->deadflag = DEAD_DEAD;
            Check(f.players->ResetMonsterSkillState(handle), "death reset retires trap with same profile");
            Check(state->monsterLifeSerial > life && state->monsterTrapUntil == 0 &&
                !state->monsterTrapMovementApplied && player.pev->gravity == .75f,
                "death life reset clears deadline and restores gravity");
            player.pev->deadflag = DEAD_NO; ++player.m_iNumSpawns;
            Check(!f.players->ApplyMonsterTrapMovement(player, 11), "next spawn cannot inherit old trap movement");
        }
        if (kind == 1)
        {
            f.players->Remove(handle);
            Check(state->monsterTrapUntil == 0 && !state->monsterTrapMovementApplied && player.pev->gravity == .75f,
                "disconnect retirement releases trap movement before profile detachment");
        }
        if (kind == 2)
        {
            f.engine.globals.time = 15;
            f.onReset = [&](CBasePlayer& current)
            {
                f.onReset = {};
                Require(f.players->ResetMonsterSkillState(handle), "expiry reset callback creates a new life");
                current.pev->gravity = .33f; current.pev->velocity = Vector(91, 92, 93);
                CSOLScenarioStatBuff nextLifeBuff; nextLifeBuff.expires = 14; nextLifeBuff.source = 22;
                Require(f.players->InstallStatBuff(handle, nextLifeBuff) != 0, "new life inserts a sentinel buff");
            };
            Check(!f.players->UpdatePlayerBuffs(player, 15), "expiry callback life change cancels remaining old-life traversal");
            Check(state->monsterLifeSerial > life && state->buffs.size() == 1 &&
                player.pev->gravity == .33f && player.pev->velocity == Vector(91, 92, 93),
                "old expiry loop cannot remove new-life buff or overwrite movement");
        }
    }
}

void ReservationAndResistance()
{
    {
        Fixture f(50); auto& player = f.Player(1, 1); const auto trap = f.Trap();
        f.randomValue = 49; f.Touch(trap, player);
        Check(f.State(player)->monsterTrapUntil == 0 && f.resetCalls == 0 &&
            trap.Get<CBaseEntity>()->pev->solid == SOLID_TRIGGER,
            "resistance rejection keeps trap collidable and creates no player restriction");
        f.randomValue = 50; f.Touch(trap, player);
        Check(f.State(player)->monsterTrapUntil == 15 && f.randomCalls == 2,
            "resistance equality permits capture with original strict greater comparison");
    }
    {
        Fixture f; auto& player = f.Player(1, 1); const auto first = f.Trap(); const auto second = f.Trap();
        f.onRandom = [&] { f.onRandom = {}; f.Touch(second, player); };
        f.Touch(first, player);
        Check(f.randomCalls == 1 && f.resetCalls == 1 && !f.State(player)->monsterTrapPending,
            "nested second trap cannot acquire the same pending player life");
        Check(first.Get<CBaseEntity>()->pev->solid == SOLID_NOT &&
            second.Get<CBaseEntity>()->pev->solid == SOLID_TRIGGER,
            "only the accepted trap transitions to hold");
    }
}

void MessageCancellation()
{
    for (int message = 0; message < 2; ++message)
    for (int kind = 0; kind < 2; ++kind)
    {
        Fixture f; auto& player = f.Player(); const EHANDLE handle(&player);
        const auto state = f.State(player); const auto trap = f.Trap();
        bool delivered{};
        const int trigger = message == 0 ? gmsgShake : f.engine.messages.at(csol::monster_effects::Message);
        f.onMessage = [&](int id)
        {
            if (id != trigger) return;
            f.onMessage = {}; delivered = true;
            if (kind == 0) Engine::Remove(handle.Get());
            else
            {
                Require(f.players->ResetMonsterSkillState(handle), "shake callback retires old monster life");
                ++player.m_iNumSpawns;
                player.pev->gravity = .42f;
                player.pev->velocity = Vector(6, 7, 8);
            }
        };
        f.Touch(trap, player);
        Check(delivered && f.directionMessages == (message == 0 ? 0 : 1),
            "message callback deletion or new life blocks stale capture continuation");
        Check(!trap.Get() || (trap.Get<CBaseEntity>()->pev->flags & FL_KILLME),
            "cancelled capture retires its touchless trap instead of leaving it seeking for thirty seconds");
        if (kind == 0)
            Check(!handle.Get(), "shake callback raw deletion returns without reading deleted player private data");
        else
            Check(state->monsterTrapUntil == 0 && player.pev->gravity == .42f &&
                player.pev->velocity == Vector(6, 7, 8), "notification tail preserves new life movement");
    }
}
}

int main(int argc, char** argv)
{
    try
    {
        const std::string group = argc > 1 ? argv[1] : "all";
        if (group == "all" || group == "baseline") Baseline();
        if (group == "all" || group == "random") RandomCancellation();
        if (group == "all" || group == "speed") SpeedCallbackCancellation();
        if (group == "all" || group == "release") EntityRelease();
        if (group == "all" || group == "life") LifeAndExpiry();
        if (group == "all" || group == "resistance") ReservationAndResistance();
        if (group == "all" || group == "message") MessageCancellation();
    }
    catch (const std::exception& error) { ++sdk_test::failures; std::cerr << "EXCEPTION " << error.what() << '\n'; }
    std::cout << "monster-trap-lifecycle " << sdk_test::checks << " checks " << sdk_test::failures << " failures\n";
    return sdk_test::failures ? 1 : 0;
}
