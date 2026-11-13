// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/client/runtime.hpp"
#include "../../native/csol/client/scenario/core/human_kick.hpp"
#include <iostream>
#include <limits>
#include <cstring>
#include <cstdio>

namespace csol::client
{
metahook_api_t hostApi{};
metahook_api_t* api = &hostApi;
cl_enginefunc_t engine{};
engine_studio_api_t studio{};
int activeWeapon = 28;
bool kicking{};
int ActiveWeaponHudId() { return activeWeapon; }
bool HumanKickActive() noexcept { return kicking; }
}
using namespace csol::client;
namespace kick = csol::scenario::human_kick;
namespace
{
int checks{}, sequence{}, animations{};
float started{}, now = 10;
bool symbols = true;
model_t weapon{}, foot{}, replacement{};
cl_entity_t viewmodel{};
std::string command;
void Check(bool value, const char* message)
{
    ++checks;
    if (!value) throw std::runtime_error(message);
}
void Setup()
{
    std::snprintf(weapon.name, sizeof(weapon.name), "models/v_ak47.mdl");
    std::snprintf(foot.name, sizeof(foot.name), "%s", kick::FootModel);
    std::snprintf(replacement.name, sizeof(replacement.name), "models/v_knife.mdl");
    hostApi.GetEngineBase = []() -> void* { return reinterpret_cast<void*>(0x10000000); };
    hostApi.ResolveGameSymbol = [](void*, const char* name, mh_gamesymbol_kind_t kind, void** result)
    {
        Check(kind == MH_GAMESYMBOL_KIND_GLOBAL, "matched global symbol contract");
        *result = !symbols ? nullptr : !std::strcmp(name, "cl_weaponsequence") ? static_cast<void*>(&sequence) :
            !std::strcmp(name, "cl_weaponstarttime") ? static_cast<void*>(&started) : nullptr;
        return *result ? MH_GAMESYMBOL_OK : MH_GAMESYMBOL_SYMBOL_NOT_FOUND;
    };
    engine.GetClientTime = [] { return now; };
    engine.GetViewModel = [] { return &viewmodel; };
    engine.pfnClientCmd = [](const char* value) { command = value; return 1; };
    engine.pfnWeaponAnim = [](int value, int)
    { ++animations; sequence = value; started = 0; };
    engine.hudGetModelByIndex = [](int index) -> model_t*
    { return index == 10 ? &weapon : index == 11 ? &foot : index == 12 ? &replacement : nullptr; };
    InitializeHumanKickAnimation();
    Check(command == "setinfo csol_kick_phase 1\n", "only a matched clock backend advertises phase support");
}
void Start(std::uint32_t life = 1)
{
    viewmodel.model = &weapon; activeWeapon = 28;
    sequence = 3; started = 5; now = 10; animations = 0;
    HumanKickAnimationSnapshot({life, kick::Enabled | kick::Ready, 0}, {life, kick::Enabled | kick::Active, 0});
    // Actual SVC semantics: the foot sequence overwrites the global sequence
    // and sets the global start to zero, before the next model render.
    sequence = 1; started = 0; viewmodel.model = &foot; kicking = true;
    Check(!HumanKickWeaponPresentationAllowed(28, 11), "server foot owns presentation while prediction still simulates");
}
void End(int nextSequence = 3, bool resume = true, std::uint32_t life = 1)
{
    sequence = nextSequence; started = 0; now = 11;
    HumanKickAnimationSnapshot({life, kick::Enabled | kick::Active, 0},
        {life, std::uint8_t(kick::Enabled | kick::Ready | (resume ? kick::ResumePhase : 0)), 0});
    if (resume)
        HumanKickAnimationSnapshot({life, kick::Enabled | kick::Ready | kick::ResumePhase, 0},
            {life, kick::Enabled | kick::Ready, 0});
    kicking = false;
}
void Tests()
{
    Start(); End();
    Check(started == 5, "restoring an old attack retains its elapsed original timeline, not frame zero");
    Check(!HumanKickWeaponPresentationAllowed(28, 11), "stale foot prediction cannot overwrite a completed restoration");
    Check(HumanKickWeaponPresentationAllowed(28, 10), "actual restored weapon prediction remains enabled");
    ReconcileWeaponAnimation(28, 10, 3, 0);
    Check(!animations && started == 5 && sequence == 3, "passive model reconciliation cannot replay the previous attack");
    HumanKickPredictionFinished(28, 11, false);
    ReconcileWeaponAnimation(28, 10, 3, 0);
    Check(!animations && started == 5, "silent stale prediction cannot consume the pending phase lease");
    HumanKickPredictionFinished(28, 10, true);
    ReconcileWeaponAnimation(28, 10, 3, 0);
    Check(animations == 1 && started == 0, "later genuine reconciliation is no longer bound to the old kick");
    Start(); End();
    ReconcileWeaponAnimation(28, 10, 3, 0, true);
    Check(animations == 1 && started == 0, "a genuine new same-sequence attack still begins from frame zero");
    HumanKickPredictionFinished(28, 10, true);
    Start(); End(4);
    ReconcileWeaponAnimation(28, 10, 4, 0);
    Check(animations == 1 && sequence == 4 && started == 0, "a new reload sequence never inherits the preceding attack clock");
    Start(); End(3, false);
    Check(started == 0 && HumanKickWeaponPresentationAllowed(28, 11), "cancelled handoff drops the old phase");
    Start(); activeWeapon = 29; End();
    Check(started == 0, "replacement active weapon does not inherit attack time");
    Start(); End(3, true, 2);
    Check(started == 0, "new life cannot restore a retired life's phase");
    Start(); ResetHumanKickAnimation(); End();
    Check(started == 0, "map/resources/reset retire the phase before dereferencing old model identity");
    Start(); End();
    ReconcileWeaponAnimation(28, 12, 3, 0);
    Check(animations == 1 && started == 0, "model replacement is an ordinary new presentation");
    HumanKickPredictionFinished(29, 12, true);
    ReconcileWeaponAnimation(28, 10, 3, 0);
    Check(animations == 2 && started == 0, "a later switch back cannot reactivate a retired phase ticket");
    Start(); End();
    HumanKickAnimationSnapshot({1, kick::Enabled | kick::Ready, 0}, {1, kick::Enabled | kick::Active, 0});
    sequence = 1; started = 0;
    End();
    Check(started == 5, "back-to-back kicks retain weapon identity even before renderer replaces foot");
    Start(); started = std::numeric_limits<float>::quiet_NaN(); ResetHumanKickAnimation();
    viewmodel.model = &weapon;
    HumanKickAnimationSnapshot({2, kick::Enabled | kick::Ready, 0}, {2, kick::Enabled | kick::Active, 0});
    End(3, true, 2);
    Check(started == 0, "nonfinite saved clocks never become a presentation override");
    symbols = false; InitializeHumanKickAnimation();
    Check(command == "setinfo csol_kick_phase 0\n", "unmatched engine removes any previous phase capability claim");
}
}
int main()
{
    try { Setup(); Tests(); }
    catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
    std::cout << "kick animation host: " << checks << " checks passed\n";
}
