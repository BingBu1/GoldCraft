// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "native/csol/server/weapons/guest_cof_owner.hpp"
#include <bit>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

enginefuncs_t g_engfuncs{};
globalvars_t globals{};
globalvars_t* gpGlobals = &globals;
const Vector g_vecZero{};
CBotManager* TheBots{};
bool g_bAllowedCSBot{};
CGameRules* g_pGameRules{};

#include "sdk-abort-virtuals.inc"
void CCSEntity::FireBullets(int,Vector&,Vector&,Vector&,float,int,int,int,entvars_t*) { std::abort(); }
void CCSEntity::FireBuckshots(ULONG,Vector&,Vector&,Vector&,float,int,int,entvars_t*) { std::abort(); }
Vector CCSEntity::FireBullets3(Vector&,Vector&,float,float,int,int,int,float,entvars_t*,bool,int) { std::abort(); }

namespace
{
using namespace csol::server::cof;
using guest::cof::server::GlockOwnerFrame;
int checks{};
void CheckAt(bool value, int line)
{
    ++checks;
    if (!value) { std::cerr << "Native owner check failed line " << line << '\n'; std::exit(2); }
}
#define Check(...) CheckAt((__VA_ARGS__), __LINE__)
struct Authority
{
    bool active{true};
    bool revoke{};
    std::uint64_t map{19}, life{7};
    CBasePlayer* player{};
    GlockOwner* owner{};
};
bool Current(void* context, CBasePlayer& player, std::uint64_t map, std::uint64_t life) noexcept
{
    auto& auth = *static_cast<Authority*>(context);
    if (auth.revoke) { auth.owner->Invalidate(); return true; }
    return auth.active && auth.player == &player && auth.map == map && auth.life == life;
}
NativeOwnerMode Mode() { return {OwnerMovement::NativeCounterStrike, true, true, true, true, true}; }
NativeOwnerRules Rules() { return {true, 37, 11, 45}; }
struct Fixture
{
    edict_t entity{}, ground{};
    CBasePlayer player;
    GlockOwner owner;
    Authority authority;
    Fixture()
    {
        entity.serialnumber = 17;
        entity.pvPrivateData = &player;
        player.pev = &entity.v;
        entity.v.pContainingEntity = &entity;
        entity.v.health = 100;
        entity.v.flags = FL_ONGROUND;
        entity.v.groundentity = &ground;
        authority.player = &player; authority.owner = &owner;
        player.m_afButtonLast = 0x800;
        player.m_afButtonPressed = 0x40;
        player.m_afButtonReleased = 1;
        player.random_seed = 9123;
        Check(Bind());
    }
    bool Bind(NativeOwnerMode mode = Mode(), NativeOwnerRules rules = Rules())
    { return owner.Bind(player, {&authority, Current, authority.map, authority.life}, mode, rules, {false, true}); }
    GlockOwnerFrame Read()
    { GlockOwnerFrame frame{}; Check(owner.Read(frame)); return frame; }
};

// Generated from the actual current SDK CBasePlayer::Jump gate block. This
// verifies the acceptance stage used by the insertion contract, not PM physics.
#include "sdk-jump-gates.inc"
}

int main()
{
    Fixture a, b;
    auto frame = a.Read();
    Check(frame.activityBlock == 0 && !frame.pendingGate && frame.pendingGateTime == 0 &&
        !frame.quietAudio && !frame.infiniteAmmo && frame.eventOwner2814 == 0 &&
        frame.sourceMode == guest::cof::PistolSource::NormalMuzzle);
    Check(frame.toggleAim && frame.bulletDamage == 37 && frame.meleeDamage == 11 && frame.reserveLimit == 45);
    Check(frame.randomSeed == 9123 && frame.pressed == 0x40 && frame.released == 1 && !frame.firstDrawn && frame.lightOn);
    a.entity.v.flags = FL_IMMUNE_SLIME | FL_DUCKING | FL_ONGROUND;
    Check(a.Read().originalFlags == 0x4200 && !(a.Read().originalFlags & 0x40000));
    a.entity.v.flags = FL_ONGROUND; a.entity.v.button = IN_DUCK | IN_RUN;
    Check(a.Read().originalFlags == 0x200); // Raw duck/run do not manufacture flags.
    Check(!a.owner.BeginInput() && !a.owner.AfterMovement() && !a.owner.EndPostThink());
    for (unsigned phase = 0; phase < 3; ++phase)
    {
        Fixture cancelled;
        const auto before = cancelled.Read();
        Check(cancelled.owner.BeginPreThink());
        if (phase > 0) Check(cancelled.owner.BeginInput());
        if (phase > 1) Check(cancelled.owner.AfterMovement());
        Check(cancelled.owner.CancelFrame() && !cancelled.owner.EndPostThink());
        const auto after = cancelled.Read();
        Check(before.firstDrawn == after.firstDrawn && before.lightOn == after.lightOn &&
              before.condition3AA == after.condition3AA);
    }
    globals.time = 10;
    Check(a.owner.BeginPreThink() && !a.owner.BeginPreThink());
    Check(a.Read().pressed == 0x40 && !a.Read().condition3AA); // Spread sees prior edges/state.
    a.entity.v.button = IN_JUMP; a.player.m_afButtonPressed = IN_JUMP;
    Check(a.owner.BeginInput());
    Check(a.Read().pressed == IN_JUMP && a.Read().released == IN_ATTACK2);
    Check(SdkJumpAccepts(a.player)); Check(a.owner.AcceptedNativeJump());
    Check(a.Read().condition3AA);
    a.entity.v.flags = 0;
    Check(a.owner.AfterMovement() && a.Read().condition3AA && a.owner.EndPostThink());
    Check(!b.Read().condition3AA); // Player B never inherits A's jump.
    // Held jump cannot acquire a new edge or extend the accepted-jump deadline.
    globals.time = 12;
    Check(a.owner.BeginPreThink() && a.Read().condition3AA && a.owner.BeginInput());
    Check(a.Read().pressed == 0 && a.Read().released == 0 && !a.owner.AcceptedNativeJump());
    Check(a.owner.AfterMovement() && a.owner.EndPostThink());
    globals.time = 13;
    Check(a.owner.BeginPreThink() && a.owner.BeginInput() && a.owner.AfterMovement());
    Check(a.Read().condition3AA && a.owner.EndPostThink()); // Exact equality retains flag.
    globals.time = std::nextafter(13.0f, INFINITY);
    Check(a.owner.BeginPreThink() && a.owner.BeginInput() && a.owner.AfterMovement());
    Check(!a.Read().condition3AA && a.owner.EndPostThink());
    Check(a.owner.SetFirstDrawn(true) && a.owner.SetLight(false));
    NativeOwnerPersistentState persistent{};
    Check(a.owner.PersistentState(persistent) && persistent.firstDrawn && !persistent.lightOn);

    // Real native Jump rejects each independent gate. No input-only jump marker.
    for (int gate = 0; gate < 5; ++gate)
    {
        Fixture f;
        f.player.m_afButtonPressed = IN_JUMP; f.entity.v.button = IN_JUMP;
        switch (gate)
        {
        case 0: f.entity.v.flags |= FL_WATERJUMP; break;
        case 1: f.entity.v.waterlevel = 2; f.entity.v.watertype = -19; break;
        case 2: f.player.m_afButtonPressed = 0; f.entity.v.button = 0; break;
        case 3: f.entity.v.flags &= ~FL_ONGROUND; break;
        case 4: f.entity.v.groundentity = nullptr; break;
        }
        Check(!SdkJumpAccepts(f.player));
        Check(f.owner.BeginPreThink() && f.owner.BeginInput() && !f.owner.AcceptedNativeJump());
        Check(!f.Read().condition3AA);
    }
    for (int mode = 0; mode < 7; ++mode)
    {
        Fixture f; auto policy = Mode();
        switch (mode)
        {
        case 0: policy.movement = OwnerMovement::CompleteCryOfFear; break;
        case 1: policy.freeActivity = false; break;
        case 2: policy.phoneUnavailable = false; break;
        case 3: policy.substanceFree = false; break;
        case 4: policy.finiteAmmo = false; break;
        case 5: policy.firstPerson = false; break;
        case 6: f.authority.active = false; break;
        }
        Check(!f.Bind(policy)); GlockOwnerFrame output{}; output.bulletDamage = 8765;
        Check(!f.owner.Read(output) && output.bulletDamage == 8765);
    }
    for (int stale = 0; stale < 8; ++stale)
    {
        Fixture f;
        switch (stale)
        {
        case 0: ++f.entity.serialnumber; break;
        case 1: f.entity.pvPrivateData = nullptr; break;
        case 2: f.entity.free = true; break;
        case 3: f.entity.v.deadflag = DEAD_DEAD; break;
        case 4: ++f.authority.map; break;
        case 5: ++f.authority.life; break;
        case 6: f.authority.revoke = true; break;
        case 7: f.player.pev = &f.ground.v; break;
        }
        GlockOwnerFrame output{}; output.bulletDamage = 8765;
        Check(!f.owner.Read(output) && output.bulletDamage == 8765);
        Check(!f.owner.BeginPreThink() && !f.owner.SetLight(true));
    }
    Fixture life;
    globals.time = 20; life.entity.v.button = IN_JUMP; life.player.m_afButtonPressed = IN_JUMP;
    Check(life.owner.BeginPreThink() && life.owner.BeginInput() && life.owner.AcceptedNativeJump());
    ++life.authority.life; Check(!life.owner.Current()); Check(life.Bind());
    Check(!life.Read().condition3AA); // Explicit native-life reset, not original Spawn claim.
    globals.time = std::numeric_limits<float>::quiet_NaN(); Check(!life.owner.BeginPreThink());
    std::cout << "Native owner actual SDK checks " << checks << ", failures 0\n";
}
