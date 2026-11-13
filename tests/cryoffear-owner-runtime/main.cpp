// SPDX-License-Identifier: GPL-3.0-or-later
// Reuse the actual SDK Glock fixture in place; do not copy its source tree.
#define main OriginalGlockFixtureMain
#include "../cryoffear-glock-weapon/main.cpp"
#undef main
#include "native/csol/server/weapons/guest_cof_owner_runtime.hpp"

namespace
{
using namespace csol::server::cof;
GlockOwnerRuntime runtime;
OwnerBackendView runtimeView;
struct RuntimeAuth
{
    bool active{true}, revoke{};
    std::uint64_t map{31}, life{9}, modelGeneration{12};
} runtimeAuth;
unsigned modelReads{};
bool RuntimeAuthority(void*, CBasePlayer& actor, std::uint64_t map, std::uint64_t life,
    std::uint64_t modelGeneration, const NativeOwnerMode& mode) noexcept
{
    if (runtimeAuth.revoke) { runtime.Invalidate(19, &actor); return true; }
    return runtimeAuth.active && map == runtimeAuth.map && life == runtimeAuth.life &&
        modelGeneration == runtimeAuth.modelGeneration && mode.freeActivity && mode.phoneUnavailable &&
        mode.substanceFree && mode.finiteAmmo && mode.firstPerson &&
        mode.movement == OwnerMovement::NativeCounterStrike;
}
OwnerPreparation Request(unsigned index = 19)
{
    return {index, runtimeAuth.map, runtimeAuth.life, runtimeAuth.modelGeneration,
        {OwnerMovement::NativeCounterStrike, true, true, true, true, true},
        {true, 37, 11, 45}, {true, false}, {nullptr, RuntimeAuthority},
        PistolAnimationRig::Stock, model, _stricmp};
}
bool RuntimeEffect(void* context, CBasePlayer& actor, CBasePlayerWeapon& item, const GlockEvent& event) noexcept
{
    const auto binding = *static_cast<OwnerBinding*>(context);
    if (event.kind == GlockEffect::FirstDraw) return runtime.FirstDraw(binding, true);
    if (event.kind == GlockEffect::LightState) return runtime.Light(binding, event.enabled);
    return Effect(nullptr, actor, item, event);
}
void RuntimeInitialize()
{
    runtime.EndMap();
    Initialize();
    globals.maxEntities = 1024;
    player.m_afButtonLast = player.m_afButtonPressed = player.m_afButtonReleased = 0;
    runtimeAuth = {};
    Check(runtime.BeginMap(runtimeAuth.map));
    g_engfuncs.pfnGetModelPtr = [](edict_t*) -> void* { ++modelReads; return model.data(); };
    std::string error;
    Check(runtime.Prepare(player, Request(), runtimeView, error));
    backends = {&runtimeView.binding, GlockOwnerRuntime::BackendCurrent, GlockOwnerRuntime::BackendFrame,
        RuntimeEffect, GlockRequiredEffects, runtimeView.animation, runtimeView.animationGeneration};
    Check(weapon.Bind(player, prepared, backends) == GlockStatus::Ready && weapon.Deploy());
    player.m_rgpPlayerItems[PISTOL_SLOT] = &weapon;
    weapon.m_pNext = nullptr;
    player.pev->flags = FL_ONGROUND;
    player.pev->velocity = {20, 0, 0};
    player.pev->button = 0;
    globals.time = 10;
    modelReads = 0;
}
GlockOwnerFrame RuntimeRead(const OwnerBinding& binding)
{
    GlockOwnerFrame output{};
    Check(runtime.Read(binding, output));
    return output;
}
}

int main(int argc, char** argv)
{
    if (argc != 4) return 1;
    const auto readModel = [](const char* path)
    {
        std::ifstream input(path, std::ios::binary); Check(input.good());
        std::vector<char> bytes{std::istreambuf_iterator<char>(input), {}};
        model.resize(bytes.size()); std::memcpy(model.data(), bytes.data(), bytes.size());
    };
    readModel(argv[1]); RuntimeInitialize();
    const auto oldBinding = runtimeView.binding;
    Check(runtime.Current(oldBinding) && runtimeView.animation && runtimeView.animationGeneration.mapEpoch == 31);
    Check(runtimeView.animation->Select(runtimeView.animationGeneration, Pistol::Glock, PlayerAction::Fire, false).has_value());
    Check(runtime.FirstDraw(oldBinding, false) && runtime.Light(oldBinding, true));
    Check(!RuntimeRead(oldBinding).firstDrawn && RuntimeRead(oldBinding).lightOn);
    NativeOwnerPersistentState persistent{};
    Check(runtime.PersistentState(oldBinding, persistent) && !persistent.firstDrawn && persistent.lightOn);
    // Real native inventory includes a non-Glock head and a second Glock in
    // the same bucket. Selected Glock changes spread; unselected one cannot.
    CBasePlayerItem nativeHead;
    edict_t headEdict{}, otherEdict{};
    GlockWeapon other;
    nativeHead.pev = &headEdict.v; headEdict.v.pContainingEntity = &headEdict;
    headEdict.pvPrivateData = &nativeHead; headEdict.serialnumber = 74;
    nativeHead.m_pPlayer = &player; nativeHead.m_pNext = &weapon;
    other.pev = &otherEdict.v; otherEdict.v.pContainingEntity = &otherEdict;
    otherEdict.pvPrivateData = &other; otherEdict.serialnumber = 75;
    other.m_iPrimaryAmmoType = 1; other.m_iClip = 15;
    Check(other.Bind(player, prepared, backends) == GlockStatus::Ready);
    other.m_pNext = nullptr; weapon.m_pNext = &other;
    player.m_rgpPlayerItems[PISTOL_SLOT] = &nativeHead;
    const auto beforeSelected = weapon.Spread().scalar;
    const auto beforeOther = other.Spread().scalar;
    const auto beforeLock = weapon.ActionState().primary;
    weapon.pev->nextthink = 99; other.pev->nextthink = 98;
    Check(runtime.BeginPreThink(19, player));
    Check(weapon.Spread().scalar != beforeSelected && other.Spread().scalar == beforeOther);
    Check(weapon.ActionState().primary == beforeLock && weapon.pev->nextthink == 99 && other.pev->nextthink == 98);
    Check(runtime.BeginInput(19, player) && runtime.AfterMovement(19, player) && runtime.EndPostThink(19, player));
    Check(modelReads == 0); // No actual model lookup, parsing or preparation on these stages.
    Check(runtime.BeginMap(31) && runtime.Current(oldBinding));
    runtime.Invalidate(19, &player);
    Check(!runtime.Current(oldBinding) && weapon.pev->nextthink == 0 && other.pev->nextthink == 0);
    Check(!GlockOwnerRuntime::BackendCurrent(&runtimeView.binding));

    RuntimeInitialize(); const auto jumpBinding = runtimeView.binding;
    player.pev->button = IN_JUMP; player.m_afButtonPressed = IN_JUMP;
    player.pev->groundentity = &headEdict;
    Check(runtime.BeginPreThink(19, player) && runtime.BeginInput(19, player));
    Check(runtime.AcceptedJump(19, player));
    player.pev->flags = 0;
    Check(runtime.AfterMovement(19, player) && RuntimeRead(jumpBinding).condition3AA && runtime.EndPostThink(19, player));
    // An independently prepared second actual player slot has separate state.
    CBasePlayer secondPlayer;
    edict_t secondEdict{};
    secondPlayer.pev = &secondEdict.v; secondEdict.v.pContainingEntity = &secondEdict;
    secondEdict.pvPrivateData = &secondPlayer; secondEdict.serialnumber = 99;
    secondPlayer.pev->health = 100; secondPlayer.pev->modelindex = 1;
    for (auto& item : secondPlayer.m_rgpPlayerItems) item = nullptr;
    secondPlayer.m_afButtonLast = secondPlayer.m_afButtonPressed = secondPlayer.m_afButtonReleased = 0;
    secondPlayer.random_seed = 71;
    g_engfuncs.pfnIndexOfEdict = [](const edict_t* entity) { return entity == &ownerEdict ? 19 : 20; };
    OwnerBackendView secondView; std::string secondError;
    Check(runtime.Prepare(secondPlayer, Request(20), secondView, secondError));
    Check(!RuntimeRead(secondView.binding).condition3AA && RuntimeRead(jumpBinding).condition3AA);
    Check(runtime.BeginPreThink(20, secondPlayer) && runtime.BeginInput(20, secondPlayer) &&
        runtime.AfterMovement(20, secondPlayer) && runtime.EndPostThink(20, secondPlayer));
    runtime.Invalidate(20, &player); Check(runtime.Current(secondView.binding));
    runtime.Invalidate(20, &secondPlayer); Check(!runtime.Current(secondView.binding) && runtime.Current(jumpBinding));
    globals.time = 10.1f;
    Check(runtime.BeginPreThink(19, player) && runtime.BeginInput(19, player));
    player.pev->flags = FL_ONGROUND;
    Check(runtime.AfterMovement(19, player) && !RuntimeRead(jumpBinding).condition3AA && runtime.EndPostThink(19, player));

    // The same fixed slot must never revive a backend that captured old tokens.
    RuntimeInitialize(); const auto lifeBinding = runtimeView.binding;
    ++runtimeAuth.life; ++runtimeAuth.modelGeneration;
    std::string error; OwnerBackendView renewed;
    Check(runtime.Prepare(player, Request(), renewed, error));
    Check(!runtime.Current(lifeBinding) && runtime.Current(renewed.binding));
    Check(!RuntimeRead(renewed.binding).lightOn && RuntimeRead(renewed.binding).firstDrawn);
    Check(runtime.Backend(19, player, runtimeView) && runtimeView.binding.generation == renewed.binding.generation);
    Check(!runtime.Backend(0, player, runtimeView) && !runtime.Backend(33, player, runtimeView));

    for (int identity = 0; identity < 8; ++identity)
    {
        RuntimeInitialize(); const auto binding = runtimeView.binding;
        switch (identity)
        {
        case 0: ++ownerEdict.serialnumber; break;
        case 1: ownerEdict.pvPrivateData = nullptr; break;
        case 2: ++player.pev->modelindex; break;
        case 3: player.pev->model = 123; break;
        case 4: player.pev->deadflag = DEAD_DEAD; break;
        case 5: ++runtimeAuth.life; break;
        case 6: ++runtimeAuth.modelGeneration; break;
        case 7: runtimeAuth.active = false; break;
        }
        GlockOwnerFrame output{}; output.bulletDamage = 9876;
        Check(!runtime.Read(binding, output) && output.bulletDamage == 9876);
        Check(!runtime.BeginInput(19, player) && !runtime.Current(binding));
    }
    RuntimeInitialize();
    runtimeAuth.revoke = true;
    GlockOwnerFrame output{}; output.bulletDamage = 9876;
    Check(!runtime.Read(runtimeView.binding, output) && output.bulletDamage == 9876);
    runtimeAuth.revoke = false;
    Check(weapon.pev->nextthink == 0);

    RuntimeInitialize(); auto request = Request(); request.validatedModel = {};
    OwnerBackendView rejected = runtimeView;
    Check(!runtime.Prepare(player, request, rejected, error) && !runtime.Current(runtimeView.binding));
    RuntimeInitialize(); request = Request(); request.rig = PistolAnimationRig::CSO;
    Check(!runtime.Prepare(player, request, rejected, error)); // Stock lacks CSO nataknife; no fallback.
    RuntimeInitialize(); request = Request(); request.mode.movement = OwnerMovement::CompleteCryOfFear;
    Check(!runtime.Prepare(player, request, rejected, error));
    RuntimeInitialize(); request = Request(); request.slot = 20;
    Check(!runtime.Prepare(player, request, rejected, error)); // Actual engine index is19.
    RuntimeInitialize(); weapon.m_pNext = &weapon; globals.maxEntities = 4;
    Check(!runtime.BeginPreThink(19, player) && !runtime.Current(runtimeView.binding));
    weapon.m_pNext = nullptr;

    // Cold validated CSO models bind their explicit CSO plan, without inference.
    for (int index = 2; index <= 3; ++index)
    {
        runtime.EndMap(); readModel(argv[index]); Initialize(); globals.maxEntities = 1024;
        runtimeAuth = {}; Check(runtime.BeginMap(runtimeAuth.map));
        request = Request(); request.rig = PistolAnimationRig::CSO;
        Check(runtime.Prepare(player, request, runtimeView, error));
        Check(runtimeView.animation->Select(runtimeView.animationGeneration, Pistol::Glock, PlayerAction::WeaponMelee, true).has_value());
        Check(runtime.BeginMap(32) && !runtime.Current(runtimeView.binding));
    }
    Check(!runtime.BeginMap(0));
    std::cout << "Glock owner runtime actual SDK checks " << checks << ", failures 0\n";
}
